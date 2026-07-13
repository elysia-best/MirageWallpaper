//
//  Mirage Wallpaper
//
//  Copyright © 2026 王孝慈. All rights reserved.
//

import SwiftUI

class SteamSetupViewModel: ObservableObject {
    @Published var currentStep: Int = 0
    @Published var steamCMDPath: URL?
    @Published var steamCMDInstallState: SteamCMDInstallState = .detecting
    @Published var loginState: SteamLoginState = .idle

    @Published var username: String = ""
    @Published var password: String = ""
    @Published var guardCode: String = ""
    @Published var errorMessage: String?

    @Published var loginLog: [String] = []

    let totalSteps = 4

    var canProceed: Bool {
        switch currentStep {
        case 0:
            return true
        case 1:
            switch steamCMDInstallState {
            case .found, .installed: return true
            default: return false
            }
        case 2:
            return loginState == .success
        case 3:
            return true
        default:
            return false
        }
    }

    init() {
        username = SteamCMDManager.shared.savedUsername
    }

    func detectSteamCMD() {
        steamCMDInstallState = .detecting
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let found = SteamCMDManager.shared.detectSteamCMD()
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                if let path = found {
                    self?.steamCMDPath = path
                    self?.steamCMDInstallState = .found(path.path)
                } else {
                    self?.steamCMDInstallState = .notFound
                }
            }
        }
    }

    func installSteamCMD() {
        SteamCMDManager.shared.installSteamCMD { [weak self] state in
            self?.steamCMDInstallState = state
            if case .installed(let path) = state {
                self?.steamCMDPath = URL(fileURLWithPath: path)
            }
        }
    }

    func cancelSteamCMDInstallation() {
        SteamCMDManager.shared.cancelSteamCMDInstallation()
    }

    func login() {
        guard !username.isEmpty, !password.isEmpty else {
            errorMessage = "请输入用户名和密码"
            return
        }
        errorMessage = nil
        loginLog.removeAll()
        SteamCMDManager.shared.login(
            username: username,
            password: password,
            onLog: { [weak self] line in
                self?.loginLog.append(line)
            }
        ) { [weak self] state in
            self?.loginState = state
            if case .failed(let msg) = state {
                self?.errorMessage = msg
            }
            if case .success = state {
                self?.password = ""
                self?.guardCode = ""
            }
            if case .waitingForGuard = state {
                self?.errorMessage = nil
            }
        }
    }

    func submitGuardCode() {
        guard !guardCode.isEmpty else {
            errorMessage = "请输入验证码"
            return
        }
        guard SteamCMDManager.shared.submitGuardCode(guardCode) else {
            errorMessage = "Steam Guard 会话已结束，请重新登录"
            loginState = .failed("Steam Guard 会话已结束")
            return
        }
        guardCode = ""
        errorMessage = nil
        loginState = .loggingIn
    }

    func cancelLogin() {
        SteamCMDManager.shared.cancelLogin()
        password = ""
        guardCode = ""
        loginState = .idle
        errorMessage = nil
    }

    func completeSetup() {
        SteamCMDManager.shared.savedUsername = username
    }

    func nextStep() {
        guard currentStep < totalSteps - 1 else { return }
        currentStep += 1
    }

    func previousStep() {
        guard currentStep > 0 else { return }
        currentStep -= 1
    }
}
