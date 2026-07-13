//
//  Mirage Wallpaper
//
//  Copyright © 2026 王孝慈. All rights reserved.
//

import Foundation
import Darwin

/// Process state is guarded by `processLock`; all published state is written on
/// the main queue. SteamCMD itself must run on background queues.
final class SteamCMDManager: ObservableObject, @unchecked Sendable {
    static let shared = SteamCMDManager()

    static let bootstrapURL = URL(string: "https://steamcdn-a.akamaihd.net/client/installer/steamcmd_osx.tar.gz")!
    static let bootstrapDomain = "steamcdn-a.akamaihd.net"

    @Published private(set) var steamCMDPath: URL?
    @Published private(set) var isLoggedIn = false
    @Published private(set) var authenticationState: SteamServiceState = .unknown
    @Published private(set) var diagnosticEvents: [SteamDiagnosticEvent] = []

    private let fm = FileManager.default
    private let processLock = NSLock()
    private var downloadProcesses: [String: Process] = [:]
    private var activeLoginProcess: Process?
    private var activeLoginMasterFD: Int32?
    private var activeLoginWaitingForGuard = false
    private var activeLoginCancelled = false
    private var installationInProgress = false
    private var bootstrapDownloadTask: URLSessionDownloadTask?
    private var hasRefreshedSession = false

    private let steamCMDDir: URL = {
        let dir = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appending(path: "Mirage/steamcmd")
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }()

    private let usernameKey = "SteamCMDUsername"
    private let pathKey = "SteamCMDPath"

    var savedUsername: String {
        get { UserDefaults.standard.string(forKey: usernameKey) ?? "" }
        set { UserDefaults.standard.set(newValue, forKey: usernameKey) }
    }

    var steamCMDContentDirectory: URL {
        steamCMDDir.appending(path: "steamapps/workshop/content/431960")
    }

    init() {
        if let path = UserDefaults.standard.string(forKey: pathKey), !path.isEmpty {
            let url = URL(fileURLWithPath: path)
            if fm.isExecutableFile(atPath: url.path) {
                steamCMDPath = url
            }
        }
    }

    // MARK: - Redacted support log

    func diagnosticReport() -> String {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        let lines = diagnosticEvents.map {
            "[\(formatter.string(from: $0.timestamp))] [\($0.category.rawValue)] [\($0.domain)] \($0.message)"
        }
        return ([
            "Mirage Steam 创意工坊支持报告（已脱敏）",
            "生成时间：\(formatter.string(from: Date()))",
            "系统：\(ProcessInfo.processInfo.operatingSystemVersionString)",
            "SteamCMD：\(steamCMDPath?.path ?? "未安装")",
            ""
        ] + lines).joined(separator: "\n")
    }

    private func record(_ category: SteamDiagnosticCategory, domain: String, _ message: String, secrets: [String] = []) {
        let event = SteamDiagnosticEvent(
            timestamp: Date(),
            category: category,
            domain: domain,
            message: redact(message, secrets: secrets)
        )
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.diagnosticEvents.append(event)
            if self.diagnosticEvents.count > 500 {
                self.diagnosticEvents.removeFirst(self.diagnosticEvents.count - 500)
            }
        }
    }

    private func redact(_ text: String, secrets: [String] = []) -> String {
        var result = text
        for secret in secrets where !secret.isEmpty {
            result = result.replacingOccurrences(of: secret, with: "[已隐藏]")
        }
        let patterns = [
            "(?i)(key|api[_-]?key|token|access[_-]?token|refresh[_-]?token|password)\\s*[=:]\\s*[^\\s&]+",
            "(?i)([?&](?:key|token|access_token|password)=)[^&\\s]+"
        ]
        for pattern in patterns {
            guard let regex = try? NSRegularExpression(pattern: pattern) else { continue }
            let range = NSRange(result.startIndex..., in: result)
            result = regex.stringByReplacingMatches(in: result, range: range, withTemplate: "$1[已隐藏]")
        }
        return result
    }

    // MARK: - Detect

    func detectSteamCMD() -> URL? {
        let candidates = [
            steamCMDDir.appending(path: "steamcmd").path,
            "/opt/homebrew/bin/steamcmd",
            "/usr/local/bin/steamcmd",
            NSHomeDirectory() + "/steamcmd/steamcmd.sh",
        ]

        for path in candidates where fm.isExecutableFile(atPath: path) {
            return saveSteamCMDPath(URL(fileURLWithPath: path))
        }

        if let whichResult = try? runShellSync("/usr/bin/which", arguments: ["steamcmd"]),
           whichResult.status == 0 {
            let path = whichResult.output.trimmingCharacters(in: .whitespacesAndNewlines)
            if fm.isExecutableFile(atPath: path) {
                return saveSteamCMDPath(URL(fileURLWithPath: path))
            }
        }

        return nil
    }

    private func saveSteamCMDPath(_ url: URL) -> URL {
        if Thread.isMainThread {
            steamCMDPath = url
        } else {
            DispatchQueue.main.sync { [weak self] in self?.steamCMDPath = url }
        }
        UserDefaults.standard.set(url.path, forKey: pathKey)
        return url
    }

    // MARK: - Install

    func installSteamCMD(onProgress: @escaping (SteamCMDInstallState) -> Void) {
        processLock.lock()
        let canStart = !installationInProgress
        if canStart { installationInProgress = true }
        processLock.unlock()

        guard canStart else {
            onProgress(.failed("SteamCMD 正在安装，请等待当前任务结束"))
            return
        }

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self else { return }
            defer {
                self.processLock.lock()
                self.installationInProgress = false
                self.processLock.unlock()
            }

            self.record(.steamCMDInstall, domain: Self.bootstrapDomain, "开始下载 SteamCMD 官方 bootstrap")
            DispatchQueue.main.async { onProgress(.downloading(0)) }

            do {
                try self.ensureSufficientDiskSpace(minimumBytes: 150 * 1024 * 1024)
                let archive = try self.downloadBootstrap { progress in
                    DispatchQueue.main.async { onProgress(.downloading(progress)) }
                }
                defer { try? self.fm.removeItem(at: archive) }

                DispatchQueue.main.async { onProgress(.extracting) }
                try self.validateArchive(at: archive)
                try self.extractArchive(at: archive)

                let execPath = self.steamCMDDir.appending(path: "steamcmd")
                guard self.fm.isExecutableFile(atPath: execPath.path) else {
                    throw SteamCMDError.installFailed("解压完成后未找到可执行的 steamcmd")
                }
                _ = self.saveSteamCMDPath(execPath)
                try self.ensureSteamCMDCanRun(at: execPath)

                DispatchQueue.main.async { onProgress(.initializing) }
                let health = self.runWithPTY(arguments: ["+quit"], onLine: { line in
                    self.record(.steamCMDInstall, domain: "SteamCMD", line)
                }, timeout: 90)
                guard health.status == 0, !health.timedOut else {
                    throw SteamCMDError.installFailed(
                        health.timedOut ? "SteamCMD 首次初始化超时，请检查网络后重试" : "SteamCMD 首次初始化失败 (exit \(health.status))"
                    )
                }

                self.record(.steamCMDInstall, domain: Self.bootstrapDomain, "SteamCMD 安装并完成首次初始化")
                DispatchQueue.main.async { onProgress(.installed(execPath.path)) }
            } catch {
                self.record(.steamCMDInstall, domain: Self.bootstrapDomain, "安装失败：\(error.localizedDescription)")
                DispatchQueue.main.async { onProgress(.failed(error.localizedDescription)) }
            }
        }
    }

    func cancelSteamCMDInstallation() {
        processLock.lock()
        let task = bootstrapDownloadTask
        processLock.unlock()
        task?.cancel()
        if task != nil {
            record(.steamCMDInstall, domain: Self.bootstrapDomain, "用户取消 SteamCMD bootstrap 下载")
        }
    }

    private func ensureSufficientDiskSpace(minimumBytes: Int64) throws {
        let values = try steamCMDDir.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey])
        if let available = values.volumeAvailableCapacityForImportantUsage, available < minimumBytes {
            throw SteamCMDError.installFailed("可用磁盘空间不足，请至少预留 150 MB 后重试")
        }
    }

    private func downloadBootstrap(onProgress: @escaping (Double) -> Void) throws -> URL {
        let delegate = SteamCMDDownloadDelegate(onProgress: onProgress)
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 20
        configuration.timeoutIntervalForResource = 180
        let session = URLSession(configuration: configuration, delegate: delegate, delegateQueue: nil)
        let task = session.downloadTask(with: Self.bootstrapURL)
        processLock.lock(); bootstrapDownloadTask = task; processLock.unlock()
        task.resume()
        delegate.semaphore.wait()
        processLock.lock(); bootstrapDownloadTask = nil; processLock.unlock()

        if let error = delegate.error { throw SteamCMDError.downloadFailed(error.localizedDescription) }
        guard let response = delegate.response as? HTTPURLResponse,
              (200...299).contains(response.statusCode),
              let tempURL = delegate.downloadedURL else {
            throw SteamCMDError.downloadFailed("服务器未返回有效的 SteamCMD 安装包")
        }
        guard let length = response.value(forHTTPHeaderField: "Content-Length"), Int64(length) ?? 0 > 0 else {
            throw SteamCMDError.downloadFailed("SteamCMD 安装包长度无效")
        }

        let archive = steamCMDDir.appending(path: "steamcmd_osx.tar.gz")
        if fm.fileExists(atPath: archive.path) { try fm.removeItem(at: archive) }
        try fm.moveItem(at: tempURL, to: archive)
        self.record(.steamCMDInstall, domain: Self.bootstrapDomain, "下载完成（\(length) bytes）")
        return archive
    }

    private func validateArchive(at archive: URL) throws {
        let listing = try runShellSync("/usr/bin/tar", arguments: ["-tzf", archive.path])
        guard listing.status == 0 else {
            throw SteamCMDError.installFailed("SteamCMD 安装包无法校验：\(listing.output.prefix(200))")
        }
        let paths = listing.output.split(separator: "\n").map(String.init)
        guard paths.contains(where: { $0 == "steamcmd" || $0.hasSuffix("/steamcmd") }),
              !paths.contains(where: { $0.hasPrefix("/") || $0.split(separator: "/").contains("..") }) else {
            throw SteamCMDError.installFailed("SteamCMD 安装包内容异常")
        }
    }

    private func extractArchive(at archive: URL) throws {
        let result = try runShellSync("/usr/bin/tar", arguments: ["-xzf", archive.path, "-C", steamCMDDir.path])
        guard result.status == 0 else {
            throw SteamCMDError.installFailed("解压 SteamCMD 失败：\(result.output.prefix(200))")
        }
    }

    private func ensureSteamCMDCanRun(at executable: URL) throws {
        let machine = (try? runShellSync("/usr/bin/uname", arguments: ["-m"]))?.output
            .trimmingCharacters(in: .whitespacesAndNewlines)
        guard machine == "arm64" else { return }
        let architectures = (try? runShellSync("/usr/bin/lipo", arguments: ["-archs", executable.path]))?.output ?? ""
        guard architectures.contains("x86_64"), !architectures.contains("arm64") else { return }
        let rosetta = try runShellSync("/usr/bin/arch", arguments: ["-x86_64", "/usr/bin/true"])
        guard rosetta.status == 0 else {
            throw SteamCMDError.installFailed("当前 SteamCMD 需要 Rosetta。请先在 macOS 中安装 Rosetta 后重试")
        }
    }

    // MARK: - Session

    func refreshSessionIfNeeded() {
        processLock.lock()
        let shouldRefresh = !hasRefreshedSession && steamCMDPath != nil && !savedUsername.isEmpty && activeLoginProcess == nil
        if shouldRefresh { hasRefreshedSession = true }
        processLock.unlock()
        guard shouldRefresh else { return }

        DispatchQueue.global(qos: .utility).async { [weak self] in
            guard let self else { return }
            self.setAuthenticationState(.checking)
            let result = self.runWithPTY(arguments: ["+login", self.savedUsername, "+quit"], onLine: { line in
                self.record(.authentication, domain: "Steam 登录服务", line, secrets: [self.savedUsername])
            }, timeout: 35)
            let loggedIn = self.isLoginSuccessful(result.output) && result.status == 0
            DispatchQueue.main.async {
                self.isLoggedIn = loggedIn
                self.authenticationState = loggedIn
                    ? .available("会话有效")
                    : .needsAction(result.timedOut ? "会话检测超时，需要重新登录" : "需要重新登录")
            }
            self.record(.authentication, domain: "Steam 登录服务", loggedIn ? "已恢复本机 SteamCMD 会话" : "本机 SteamCMD 会话不可用")
        }
    }

    /// SteamCMD receives the password through the PTY, never through process arguments.
    /// The same PTY stays open for Steam Guard, so entering a code cannot race a second login process.
    func login(username: String, password: String,
               onLog: @escaping (String) -> Void,
               onResult: @escaping (SteamLoginState) -> Void) {
        guard steamCMDPath != nil else {
            onResult(.failed("SteamCMD 未安装"))
            return
        }
        guard beginLoginSession() else {
            onResult(.failed("已有 Steam 登录正在进行，请先取消或完成它"))
            return
        }

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self else { return }
            self.setAuthenticationState(.checking)
            DispatchQueue.main.async { onResult(.loggingIn) }
            self.record(.authentication, domain: "Steam 登录服务", "开始 SteamCMD 登录（密码不会写入命令行或日志）")

            var masterFD: Int32 = 0
            var slaveFD: Int32 = 0
            guard openpty(&masterFD, &slaveFD, nil, nil, nil) == 0 else {
                _ = self.finishLoginSession(process: nil)
                self.setAuthenticationState(.unavailable("无法创建安全登录终端"))
                DispatchQueue.main.async { onResult(.failed("无法创建伪终端")) }
                return
            }
            self.disableTerminalEcho(slaveFD: slaveFD)

            guard let cmdPath = self.steamCMDPath else {
                close(masterFD); close(slaveFD)
                _ = self.finishLoginSession(process: nil)
                DispatchQueue.main.async { onResult(.failed("SteamCMD 未安装")) }
                return
            }

            let process = Process()
            process.executableURL = cmdPath
            process.arguments = ["+login", username, "+quit"]
            process.currentDirectoryURL = self.steamCMDDir
            process.standardInput = FileHandle(fileDescriptor: slaveFD, closeOnDealloc: false)
            process.standardOutput = FileHandle(fileDescriptor: slaveFD, closeOnDealloc: false)
            process.standardError = FileHandle(fileDescriptor: slaveFD, closeOnDealloc: false)
            let masterHandle = FileHandle(fileDescriptor: masterFD, closeOnDealloc: false)

            do {
                try process.run()
                close(slaveFD)
                self.attachLoginSession(process: process, masterFD: masterFD)

                var output = ""
                var passwordSent = false
                self.readPTY(masterHandle: masterHandle, onChunk: { chunk in
                    output += chunk
                    if !passwordSent && self.isPasswordPrompt(chunk) {
                        passwordSent = self.writeToPTY(password + "\n", masterFD: masterFD)
                        if !passwordSent { process.terminate() }
                    }
                    if let type = self.guardType(for: chunk), self.markWaitingForGuard(process: process) {
                        DispatchQueue.main.async { onResult(.waitingForGuard(type)) }
                    }
                }, onLine: { line in
                    if !passwordSent && self.isPasswordPrompt(line) {
                        passwordSent = self.writeToPTY(password + "\n", masterFD: masterFD)
                        if !passwordSent { process.terminate() }
                    }
                    let safeLine = self.redact(line, secrets: [username, password])
                    self.record(.authentication, domain: "Steam 登录服务", safeLine)
                    DispatchQueue.main.async { onLog(safeLine) }
                    if let type = self.guardType(for: line), self.markWaitingForGuard(process: process) {
                        DispatchQueue.main.async { onResult(.waitingForGuard(type)) }
                    }
                })
                process.waitUntilExit()
                close(masterFD)

                let wasCancelled = self.finishLoginSession(process: process)
                let success = !wasCancelled && passwordSent && process.terminationStatus == 0 && self.isLoginSuccessful(output)
                DispatchQueue.main.async {
                    if success {
                        self.isLoggedIn = true
                        self.savedUsername = username
                        self.authenticationState = .available("已登录 \(username)")
                        onResult(.success)
                    } else if wasCancelled {
                        self.isLoggedIn = false
                        self.authenticationState = .needsAction("登录已取消")
                        onResult(.failed("登录已取消"))
                    } else if self.containsGuardRequest(output) {
                        self.isLoggedIn = false
                        self.authenticationState = .needsAction("Steam Guard 验证未完成")
                        onResult(.failed("Steam Guard 验证未完成或已超时，请重试登录"))
                    } else {
                        self.isLoggedIn = false
                        self.authenticationState = .needsAction("登录失败，需要重新验证")
                        let message = passwordSent
                            ? self.loginFailureMessage(output, status: process.terminationStatus)
                            : "SteamCMD 未请求密码，已拒绝复用旧会话；请重新登录"
                        onResult(.failed(message))
                    }
                }
            } catch {
                close(masterFD)
                _ = Darwin.close(slaveFD)
                _ = self.finishLoginSession(process: process)
                self.setAuthenticationState(.unavailable("SteamCMD 登录运行失败"))
                self.record(.authentication, domain: "Steam 登录服务", "登录运行失败：\(error.localizedDescription)")
                DispatchQueue.main.async { onResult(.failed(error.localizedDescription)) }
            }
        }
    }

    func submitGuardCode(_ code: String) -> Bool {
        guard !code.isEmpty else { return false }
        processLock.lock()
        let process = activeLoginProcess
        let masterFD = activeLoginMasterFD
        let canSubmit = activeLoginWaitingForGuard && process?.isRunning == true && masterFD != nil
        if canSubmit { activeLoginWaitingForGuard = false }
        processLock.unlock()
        guard canSubmit, let masterFD else { return false }

        let sent = writeToPTY(code + "\n", masterFD: masterFD)
        record(.authentication, domain: "Steam 登录服务", sent ? "已安全提交 Steam Guard 验证码" : "提交 Steam Guard 验证码失败")
        return sent
    }

    func cancelLogin() {
        processLock.lock()
        let process = activeLoginProcess
        if process != nil { activeLoginCancelled = true }
        processLock.unlock()
        process?.terminate()
    }

    func logout(completion: @escaping (Result<Void, Error>) -> Void) {
        cancelLogin()
        DispatchQueue.global(qos: .utility).async { [weak self] in
            guard let self else { return }
            let result: SteamCMDRunResult
            if self.steamCMDPath != nil {
                self.record(.authentication, domain: "Steam 登录服务", "请求 SteamCMD 注销本机会话")
                result = self.runWithPTY(arguments: ["+logout", "+quit"], onLine: { line in
                    self.record(.authentication, domain: "Steam 登录服务", line)
                }, timeout: 25)
            } else {
                result = SteamCMDRunResult(status: 0, output: "SteamCMD 未安装，无需远端注销", timedOut: false)
            }

            self.clearLocalSessionArtifacts()
            DispatchQueue.main.async {
                self.savedUsername = ""
                self.isLoggedIn = false
                self.authenticationState = .needsAction("未登录")
                if result.status == 0 && !result.timedOut {
                    self.record(.authentication, domain: "Steam 登录服务", "已注销并清除 Mirage 本机 SteamCMD 会话")
                    completion(.success(()))
                } else {
                    let error = SteamCMDError.installFailed(result.timedOut ? "注销请求超时；本机会话已清除" : "SteamCMD 注销失败 (exit \(result.status))；本机会话已清除")
                    self.record(.authentication, domain: "Steam 登录服务", error.localizedDescription)
                    completion(.failure(error))
                }
            }
        }
    }

    private func clearLocalSessionArtifacts() {
        // SteamCMD can keep login state in either its root config directory or
        // the nested Steam runtime directory, depending on its bootstrap version.
        for relativePath in ["config", "steam/config", "steam/userdata"] {
            try? fm.removeItem(at: steamCMDDir.appending(path: relativePath))
        }
        for directory in [steamCMDDir, steamCMDDir.appending(path: "steam")] {
            let contents = (try? fm.contentsOfDirectory(at: directory, includingPropertiesForKeys: nil, options: .skipsHiddenFiles)) ?? []
            for item in contents where item.lastPathComponent.hasPrefix("ssfn") {
                try? fm.removeItem(at: item)
            }
        }
    }

    // MARK: - Workshop downloads

    func downloadItem(workshopId: String, expectedFileSize: Int64 = 0, onProgress: @escaping (DownloadState) -> Void) {
        guard steamCMDPath != nil else {
            onProgress(.failed("SteamCMD 未安装"))
            return
        }
        guard isLoggedIn, !savedUsername.isEmpty else {
            onProgress(.failed("Steam 会话未验证，请重新登录后再下载"))
            return
        }

        processLock.lock()
        let hasActiveDownload = !downloadProcesses.isEmpty
        processLock.unlock()
        guard !hasActiveDownload else {
            onProgress(.failed("SteamCMD 一次只能安全处理一个下载，请等待当前下载完成"))
            return
        }

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self, let cmdPath = self.steamCMDPath else { return }
            DispatchQueue.main.async { onProgress(.starting) }
            self.record(.workshopDownload, domain: "Steam 内容 CDN", "开始下载创意工坊项目 \(workshopId)")

            var masterFD: Int32 = 0
            var slaveFD: Int32 = 0
            guard openpty(&masterFD, &slaveFD, nil, nil, nil) == 0 else {
                DispatchQueue.main.async { onProgress(.failed("无法创建伪终端")) }
                return
            }

            let process = Process()
            process.executableURL = cmdPath
            process.arguments = ["+login", self.savedUsername, "+workshop_download_item", "431960", workshopId, "+quit"]
            process.currentDirectoryURL = self.steamCMDDir
            process.standardOutput = FileHandle(fileDescriptor: slaveFD, closeOnDealloc: false)
            process.standardError = FileHandle(fileDescriptor: slaveFD, closeOnDealloc: false)
            let masterHandle = FileHandle(fileDescriptor: masterFD, closeOnDealloc: false)
            self.storeDownloadProcess(process, workshopId: workshopId)

            let downloadDir = self.steamCMDContentDirectory.appending(path: workshopId)
            let polling = LockedFlag(initialValue: true)
            DispatchQueue.global(qos: .utility).async {
                var lastReported = 0.0
                while polling.value {
                    Thread.sleep(forTimeInterval: 0.75)
                    guard polling.value else { break }
                    guard expectedFileSize > 0,
                          let size = try? downloadDir.directoryTotalAllocatedSize(includingSubfolders: true), size > 0 else { continue }
                    let progress = min(Double(size) / Double(expectedFileSize), 0.95)
                    if progress > lastReported {
                        lastReported = progress
                        DispatchQueue.main.async { onProgress(.downloading(percent: progress)) }
                    }
                }
            }

            do {
                try process.run()
                close(slaveFD)
                var output = ""
                var receivedSuccess = false
                var receivedFailure = false
                self.readPTY(masterHandle: masterHandle, onChunk: { output += $0 }, onLine: { line in
                    self.record(.workshopDownload, domain: "Steam 内容 CDN", line, secrets: [self.savedUsername])
                    let lower = line.lowercased()
                    if lower.contains("downloading item") {
                        DispatchQueue.main.async { onProgress(.downloading(percent: 0.02)) }
                    }
                    if (lower.contains("download item") && lower.contains("ok")) || lower.contains("success. downloaded item") {
                        receivedSuccess = true
                        DispatchQueue.main.async { onProgress(.validating) }
                    }
                    if lower.contains("error!") || lower.contains("failed") || lower.contains("access denied") {
                        receivedFailure = true
                    }
                })
                process.waitUntilExit()
                close(masterFD)
                polling.value = false
                self.removeDownloadProcess(workshopId: workshopId)

                let hasProject = self.fm.fileExists(atPath: downloadDir.appending(path: "project.json").path)
                let succeeded = process.terminationStatus == 0 && receivedSuccess && !receivedFailure && hasProject
                if succeeded {
                    self.record(.workshopDownload, domain: "Steam 内容 CDN", "项目 \(workshopId) 已完成并通过本地 project.json 校验")
                    DispatchQueue.main.async { onProgress(.completed) }
                } else {
                    let message = self.downloadFailureMessage(output, status: process.terminationStatus, hasProject: hasProject)
                    self.record(.workshopDownload, domain: "Steam 内容 CDN", message)
                    DispatchQueue.main.async { onProgress(.failed(message)) }
                }
            } catch {
                polling.value = false
                close(masterFD)
                _ = Darwin.close(slaveFD)
                self.removeDownloadProcess(workshopId: workshopId)
                self.record(.workshopDownload, domain: "Steam 内容 CDN", "下载运行失败：\(error.localizedDescription)")
                DispatchQueue.main.async { onProgress(.failed(error.localizedDescription)) }
            }
        }
    }

    func cancelDownload(workshopId: String) {
        processLock.lock()
        let process = downloadProcesses[workshopId]
        processLock.unlock()
        process?.terminate()
        record(.workshopDownload, domain: "Steam 内容 CDN", "用户取消下载项目 \(workshopId)")
    }

    // MARK: - PTY helpers

    private func runWithPTY(arguments: [String], onLine: @escaping (String) -> Void, timeout: TimeInterval? = nil) -> SteamCMDRunResult {
        guard let cmdPath = steamCMDPath else { return SteamCMDRunResult(status: -1, output: "", timedOut: false) }

        var masterFD: Int32 = 0
        var slaveFD: Int32 = 0
        guard openpty(&masterFD, &slaveFD, nil, nil, nil) == 0 else {
            return SteamCMDRunResult(status: -2, output: "", timedOut: false)
        }

        let process = Process()
        process.executableURL = cmdPath
        process.arguments = arguments
        process.currentDirectoryURL = steamCMDDir
        process.standardInput = FileHandle(fileDescriptor: slaveFD, closeOnDealloc: false)
        process.standardOutput = FileHandle(fileDescriptor: slaveFD, closeOnDealloc: false)
        process.standardError = FileHandle(fileDescriptor: slaveFD, closeOnDealloc: false)
        let masterHandle = FileHandle(fileDescriptor: masterFD, closeOnDealloc: false)
        let timeoutLock = NSLock()
        var timedOut = false

        do {
            try process.run()
            close(slaveFD)
            let timeoutWorkItem = DispatchWorkItem {
                if process.isRunning {
                    timeoutLock.lock(); timedOut = true; timeoutLock.unlock()
                    process.terminate()
                }
            }
            if let timeout {
                DispatchQueue.global(qos: .utility).asyncAfter(deadline: .now() + timeout, execute: timeoutWorkItem)
            }

            var output = ""
            readPTY(masterHandle: masterHandle, onChunk: { output += $0 }, onLine: onLine)
            process.waitUntilExit()
            timeoutWorkItem.cancel()
            timeoutLock.lock(); let didTimeout = timedOut; timeoutLock.unlock()
            close(masterFD)
            return SteamCMDRunResult(status: process.terminationStatus, output: output, timedOut: didTimeout)
        } catch {
            close(masterFD)
            _ = Darwin.close(slaveFD)
            return SteamCMDRunResult(status: -3, output: error.localizedDescription, timedOut: false)
        }
    }

    private func readPTY(masterHandle: FileHandle, onChunk: (String) -> Void, onLine: (String) -> Void) {
        var pendingLine = ""
        while true {
            let data = masterHandle.availableData
            if data.isEmpty { break }
            guard let chunk = String(data: data, encoding: .utf8) else { continue }
            onChunk(chunk)
            let segments = (pendingLine + chunk).components(separatedBy: .newlines)
            pendingLine = segments.last ?? ""
            for raw in segments.dropLast() {
                let line = raw.replacingOccurrences(of: "\r", with: "").trimmingCharacters(in: .whitespaces)
                if !line.isEmpty { onLine(line) }
            }
        }
        let line = pendingLine.replacingOccurrences(of: "\r", with: "").trimmingCharacters(in: .whitespaces)
        if !line.isEmpty { onLine(line) }
    }

    private func disableTerminalEcho(slaveFD: Int32) {
        var attributes = termios()
        guard tcgetattr(slaveFD, &attributes) == 0 else { return }
        attributes.c_lflag &= ~tcflag_t(ECHO)
        _ = tcsetattr(slaveFD, TCSANOW, &attributes)
    }

    private func writeToPTY(_ text: String, masterFD: Int32) -> Bool {
        guard let data = text.data(using: .utf8) else { return false }
        return data.withUnsafeBytes { bytes in
            guard let base = bytes.baseAddress else { return false }
            return Darwin.write(masterFD, base, bytes.count) == bytes.count
        }
    }

    // MARK: - Process state

    private func beginLoginSession() -> Bool {
        processLock.lock()
        defer { processLock.unlock() }
        guard activeLoginProcess == nil else { return false }
        activeLoginWaitingForGuard = false
        activeLoginCancelled = false
        // A non-nil sentinel prevents a second tap before the background process has spawned.
        activeLoginProcess = Process()
        return true
    }

    private func attachLoginSession(process: Process, masterFD: Int32) {
        processLock.lock()
        let wasCancelled = activeLoginCancelled
        activeLoginProcess = process
        activeLoginMasterFD = masterFD
        processLock.unlock()
        if wasCancelled { process.terminate() }
    }

    private func finishLoginSession(process: Process?) -> Bool {
        processLock.lock()
        let wasCancelled = activeLoginCancelled
        if process == nil || activeLoginProcess === process || activeLoginProcess?.isRunning != true {
            activeLoginProcess = nil
            activeLoginMasterFD = nil
            activeLoginWaitingForGuard = false
            activeLoginCancelled = false
        }
        processLock.unlock()
        return wasCancelled
    }

    private func markWaitingForGuard(process: Process) -> Bool {
        processLock.lock()
        defer { processLock.unlock() }
        guard activeLoginProcess === process, !activeLoginWaitingForGuard else { return false }
        activeLoginWaitingForGuard = true
        return true
    }

    private func storeDownloadProcess(_ process: Process, workshopId: String) {
        processLock.lock(); downloadProcesses[workshopId] = process; processLock.unlock()
    }

    private func removeDownloadProcess(workshopId: String) {
        processLock.lock(); downloadProcesses.removeValue(forKey: workshopId); processLock.unlock()
    }

    private func setAuthenticationState(_ state: SteamServiceState) {
        DispatchQueue.main.async { [weak self] in self?.authenticationState = state }
    }

    // MARK: - Output interpretation

    private func isLoginSuccessful(_ output: String) -> Bool {
        let lower = output.lowercased()
        let hasSuccess = output.contains("Logged in OK") || output.contains("Login Successful")
        let hasFailure = lower.contains("login failure") || lower.contains("invalid password") ||
            lower.contains("failed") || lower.contains("access denied")
        return hasSuccess && !hasFailure
    }

    private func isPasswordPrompt(_ output: String) -> Bool {
        let lower = output.lowercased()
        guard !lower.contains("invalid password") else { return false }
        return lower.contains("password:") || lower.contains("enter your password")
    }

    private func containsGuardRequest(_ output: String) -> Bool {
        guardType(for: output) != nil
    }

    private func guardType(for output: String) -> SteamGuardType? {
        let lower = output.lowercased()
        if lower.contains("please confirm the login in the steam mobile app") || lower.contains("confirm the login in the steam mobile") {
            return .mobileConfirm
        }
        guard lower.contains("steam guard") || lower.contains("two-factor") || lower.contains("two factor") ||
                (lower.contains("code") && (lower.contains("email") || lower.contains("mobile"))) else {
            return nil
        }
        return (lower.contains("email") || lower.contains("mail")) ? .email : .mobile
    }

    private func loginFailureMessage(_ output: String, status: Int32) -> String {
        let lower = output.lowercased()
        if lower.contains("invalid password") || lower.contains("login failure") {
            return "登录失败，请检查用户名和密码"
        }
        if lower.contains("access denied") || lower.contains("no subscription") {
            return "该 Steam 账号没有 Wallpaper Engine 的可用所有权"
        }
        if lower.contains("network") || lower.contains("connection") || lower.contains("timeout") {
            return "无法连接 Steam 登录服务，请检查网络后重试"
        }
        return "Steam 登录失败 (exit \(status))"
    }

    private func downloadFailureMessage(_ output: String, status: Int32, hasProject: Bool) -> String {
        let lower = output.lowercased()
        if lower.contains("no subscription") || lower.contains("access denied") {
            return "下载被 Steam 拒绝：请确认该账号拥有 Wallpaper Engine 并有权访问此项目"
        }
        if lower.contains("not logged") || lower.contains("steam guard") || lower.contains("login") {
            return "Steam 会话已失效，请重新登录后再试"
        }
        if lower.contains("network") || lower.contains("connection") || lower.contains("timeout") {
            return "下载网络连接失败，请检查网络后重试"
        }
        if !hasProject {
            return "下载未完成：未找到有效的 project.json (exit \(status))"
        }
        return "SteamCMD 未确认下载成功 (exit \(status))"
    }

    // MARK: - Shell helper

    private func runShellSync(_ executable: String, arguments: [String]) throws -> ShellResult {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = pipe
        try process.run()
        process.waitUntilExit()
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        return ShellResult(status: process.terminationStatus, output: String(data: data, encoding: .utf8) ?? "")
    }
}

private struct SteamCMDRunResult {
    let status: Int32
    let output: String
    let timedOut: Bool
}

private struct ShellResult {
    let status: Int32
    let output: String
}

private final class LockedFlag {
    private let lock = NSLock()
    private var storage: Bool

    init(initialValue: Bool) { storage = initialValue }

    var value: Bool {
        get { lock.lock(); defer { lock.unlock() }; return storage }
        set { lock.lock(); storage = newValue; lock.unlock() }
    }
}

private final class SteamCMDDownloadDelegate: NSObject, URLSessionDownloadDelegate {
    let semaphore = DispatchSemaphore(value: 0)
    let onProgress: (Double) -> Void
    var downloadedURL: URL?
    var response: URLResponse?
    var error: Error?

    init(onProgress: @escaping (Double) -> Void) {
        self.onProgress = onProgress
    }

    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask,
                    didWriteData bytesWritten: Int64, totalBytesWritten: Int64, totalBytesExpectedToWrite: Int64) {
        guard totalBytesExpectedToWrite > 0 else { return }
        onProgress(min(max(Double(totalBytesWritten) / Double(totalBytesExpectedToWrite), 0), 1))
    }

    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didFinishDownloadingTo location: URL) {
        downloadedURL = location
        response = downloadTask.response
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        self.error = error
        if response == nil { response = task.response }
        semaphore.signal()
    }
}

enum SteamCMDError: LocalizedError {
    case downloadFailed(String)
    case installFailed(String)

    var errorDescription: String? {
        switch self {
        case .downloadFailed(let message): return "SteamCMD 下载失败：\(message)"
        case .installFailed(let message): return "SteamCMD 安装失败：\(message)"
        }
    }
}
