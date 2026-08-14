#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

// Bridges QtWebEngine frames to the Linux desktop protocol. The producer and
// Vulkan objects are owned by the worker instance; submitted sync FDs are
// consumed by md_producer_submit_frame on every path.
class ProtocolWebRenderer final : public QObject {
    Q_OBJECT
public:
    struct Config {
        QString socketPath;
        QString outputId;
        std::function<void(float, float)> pointerEnter;
        std::function<void(float, float)> pointerMotion;
        std::function<void(float, float, uint32_t, bool)> pointerButton;
        std::function<void(float, float, float, float)> pointerAxis;
        std::function<void(uint32_t, uint32_t)> outputSizeChanged;
    };

    explicit ProtocolWebRenderer(Config config, QObject* parent = nullptr);
    ~ProtocolWebRenderer() override;

    bool start(QString* error);
    void stop();
    void submitFrame(const QImage& image);

signals:
    void failed(const QString& message);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
