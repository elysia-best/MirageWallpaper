#pragma once

#include <QByteArray>
#include <QObject>
#include <QStringList>

#include <functional>
#include <sys/types.h>

class QSocketNotifier;

namespace Mirage {

// SteamCMD 的 PTY 会话封装：forkpty + exec 启动子进程，以非阻塞方式
// 读取输出并回调。由 SteamCMDManager 内嵌类独立而来，职责单一
// （子进程生命周期与 IO），无任何业务状态。
class SteamCMDPtySession final : public QObject {
    Q_OBJECT
public:
    explicit SteamCMDPtySession(QObject* parent = nullptr);
    ~SteamCMDPtySession() override;

    // 子进程输出数据回调。
    std::function<void(const QByteArray&)> onData;
    // 子进程退出回调：(exitCode, 是否正常退出)。
    std::function<void(int, bool)> onFinished;

    // 启动程序（含参数），在 workingDirectory 下以 homeDirectory 为 HOME 运行；
    // .sh 脚本经 /bin/bash 执行。失败时返回 false 并填充 error。
    bool start(const QString& program, const QStringList& arguments, const QString& workingDirectory,
               const QString& homeDirectory, QString* error);
    bool isRunning() const;
    // 向子进程 stdin 写入数据，失败返回 false。
    bool write(const QByteArray& data);
    // 请求终止：先 SIGTERM，5 秒后仍未退出则 SIGKILL。
    void terminate();

private:
    void readAvailable();
    void finish();
    void finishStatus(int status);

    pid_t m_pid = -1;
    int m_fd = -1;
    bool m_finished = false;
    QSocketNotifier* m_notifier = nullptr;
};

} // namespace Mirage
