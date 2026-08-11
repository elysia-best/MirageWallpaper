#include "Services/SteamCMDPtySession.h"

#include <QSocketNotifier>
#include <QTimer>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <vector>

namespace Mirage {

SteamCMDPtySession::SteamCMDPtySession(QObject* parent)
    : QObject(parent) {}

SteamCMDPtySession::~SteamCMDPtySession() {
    if (m_pid > 0) {
        ::kill(m_pid, SIGKILL);
        int status = 0;
        ::waitpid(m_pid, &status, WNOHANG);
    }
    if (m_notifier) m_notifier->setEnabled(false);
    if (m_fd >= 0) ::close(m_fd);
}

bool SteamCMDPtySession::start(const QString& program, const QStringList& arguments,
                               const QString& workingDirectory, const QString& homeDirectory,
                               QString* error) {
    if (m_pid > 0) return false;

    int master = -1;
    const pid_t pid = ::forkpty(&master, nullptr, nullptr, nullptr);
    if (pid < 0) {
        if (error) *error = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }

    if (pid == 0) {
        ::chdir(workingDirectory.toLocal8Bit().constData());
        ::setenv("HOME", homeDirectory.toLocal8Bit().constData(), 1);
        ::setenv("STEAMEXE", program.toLocal8Bit().constData(), 1);
        termios attributes{};
        if (::tcgetattr(STDIN_FILENO, &attributes) == 0) {
            attributes.c_lflag &= ~tcflag_t(ECHO | ECHONL);
            ::tcsetattr(STDIN_FILENO, TCSANOW, &attributes);
        }

        std::vector<QByteArray> args;
        args.reserve(arguments.size() + 1);
        args.push_back(program.toLocal8Bit());
        for (const QString& argument : arguments) args.push_back(argument.toLocal8Bit());
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (QByteArray& argument : args) argv.push_back(argument.data());
        argv.push_back(nullptr);

        if (program.endsWith(QStringLiteral(".sh"))) {
            std::vector<char*> bashArgv;
            bashArgv.reserve(argv.size() + 2);
            bashArgv.push_back(const_cast<char*>("/bin/bash"));
            for (char* argument : argv) bashArgv.push_back(argument);
            ::execv("/bin/bash", bashArgv.data());
        } else {
            ::execv(program.toLocal8Bit().constData(), argv.data());
        }
        _exit(127);
    }

    m_pid = pid;
    m_fd = master;
    const int flags = ::fcntl(m_fd, F_GETFL, 0);
    ::fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this](auto) { readAvailable(); });
    return true;
}

bool SteamCMDPtySession::isRunning() const {
    return m_pid > 0 && !m_finished;
}

bool SteamCMDPtySession::write(const QByteArray& data) {
    if (m_fd < 0 || !isRunning()) return false;
    qsizetype offset = 0;
    while (offset < data.size()) {
        const ssize_t result = ::write(m_fd, data.constData() + offset, size_t(data.size() - offset));
        if (result > 0) {
            offset += result;
            continue;
        }
        if (result < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

void SteamCMDPtySession::terminate() {
    if (m_pid <= 0 || m_finished) return;
    ::kill(m_pid, SIGTERM);
    const pid_t pid = m_pid;
    QTimer::singleShot(5000, this, [pid] {
        if (::kill(pid, 0) == 0) ::kill(pid, SIGKILL);
    });
}

void SteamCMDPtySession::readAvailable() {
    QByteArray data;
    char buffer[8192];
    while (true) {
        const ssize_t count = ::read(m_fd, buffer, sizeof(buffer));
        if (count > 0) {
            data.append(buffer, int(count));
            continue;
        }
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (count == 0 || (count < 0 && errno == EIO)) {
            if (m_notifier) m_notifier->setEnabled(false);
            finish();
        }
        break;
    }
    if (!data.isEmpty() && onData) onData(data);
    if (m_pid > 0 && !m_finished) {
        int status = 0;
        if (::waitpid(m_pid, &status, WNOHANG) == m_pid) finishStatus(status);
    }
}

void SteamCMDPtySession::finish() {
    if (m_finished) return;
    int status = 0;
    const pid_t result = ::waitpid(m_pid, &status, WNOHANG);
    if (result == 0) return;
    if (result < 0 && errno != ECHILD) return;
    finishStatus(status);
}

void SteamCMDPtySession::finishStatus(int status) {
    if (m_finished) return;
    m_finished = true;
    const int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    const bool normal = WIFEXITED(status);
    if (m_notifier) m_notifier->setEnabled(false);
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_pid = -1;
    if (onFinished) onFinished(exitCode, normal);
}

} // namespace Mirage
