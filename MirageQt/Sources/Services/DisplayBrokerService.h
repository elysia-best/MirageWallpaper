// DisplayBrokerService — hosts the mirage-display broker socket so desktop
// environment adapters (e.g. KDE Plasma) can receive wallpaper frames. The
// dispatch loop runs on a worker thread; MirageQt itself never paints into
// desktop windows.

#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <cstdint>
#include <thread>

typedef struct md_broker md_broker_t;

namespace Mirage {

class DisplayBrokerService final : public QObject {
    Q_OBJECT

public:
    explicit DisplayBrokerService(QObject* parent = nullptr);
    ~DisplayBrokerService() override;

    bool start(QString* error = nullptr);
    void stop();

    [[nodiscard]] static QString defaultSocketPath();

signals:
    // Desktop window facts reported by a display adapter (stable output id +
    // WINDOW_STATE flags). Emitted from the broker dispatch thread; receivers
    // on the main thread get a queued delivery.
    void windowStateChanged(const QString& stableId, quint32 flags);

private:
    static void onWindowState(void* userData, const char* stableId, uint32_t flags);

    md_broker_t* m_broker = nullptr;
    QString m_socketPath;
    std::atomic_bool m_running { false };
    std::thread m_thread;
};

} // namespace Mirage
