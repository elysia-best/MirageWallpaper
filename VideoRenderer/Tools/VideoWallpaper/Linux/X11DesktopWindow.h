#pragma once

#include <QString>

class QWidget;

[[nodiscard]] bool VRConfigureX11DesktopWindow(
    QWidget* window, int screenIndex, QString* error = nullptr);
