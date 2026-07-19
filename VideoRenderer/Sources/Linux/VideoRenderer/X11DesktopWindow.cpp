#include "X11DesktopWindow.h"

#include <QGuiApplication>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QWidget>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

namespace {

struct MotifWmHints {
    unsigned long flags;
    unsigned long functions;
    unsigned long decorations;
    long inputMode;
    unsigned long status;
};

struct MonitorGeometry {
    int x = 0;
    int y = 0;
    unsigned int width = 0;
    unsigned int height = 0;
};

constexpr unsigned long motifDecorationsHint = 1u << 1u;
constexpr unsigned long allDesktops = 0xFFFFFFFFu;

Atom atom(Display* display, const char* name) {
    return XInternAtom(display, name, False);
}

MonitorGeometry monitorGeometry(const QRect& geometry) {
    return {
        .x = geometry.x(),
        .y = geometry.y(),
        .width = static_cast<unsigned int>(std::max(0, geometry.width())),
        .height = static_cast<unsigned int>(std::max(0, geometry.height())),
    };
}

bool windowRootOrigin(Display* display, Window root, Window window, int& x, int& y) {
    if (window == root) {
        x = 0;
        y = 0;
        return true;
    }
    Window child = 0;
    return XTranslateCoordinates(display, window, root, 0, 0, &x, &y, &child) != 0;
}

bool intersects(const MonitorGeometry& first, const MonitorGeometry& second) {
    const int firstRight = first.x + static_cast<int>(first.width);
    const int firstBottom = first.y + static_cast<int>(first.height);
    const int secondRight = second.x + static_cast<int>(second.width);
    const int secondBottom = second.y + static_cast<int>(second.height);
    return first.x < secondRight && firstRight > second.x && first.y < secondBottom &&
           firstBottom > second.y;
}

bool windowHasDesktopType(Display* display, Window window, Atom windowType, Atom desktopType) {
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char* data = nullptr;
    const int status = XGetWindowProperty(display, window, windowType, 0, 16, False, XA_ATOM,
                                          &actualType, &actualFormat, &itemCount, &bytesAfter,
                                          &data);
    bool found = false;
    if (status == Success && actualType == XA_ATOM && actualFormat == 32 && data) {
        const Atom* types = reinterpret_cast<const Atom*>(data);
        found = std::find(types, types + itemCount, desktopType) != types + itemCount;
    }
    if (data) XFree(data);
    return found;
}

bool windowIntersectsMonitor(Display* display, Window root, Window window,
                             const MonitorGeometry& monitor) {
    XWindowAttributes attributes {};
    if (XGetWindowAttributes(display, window, &attributes) == 0 ||
        attributes.map_state != IsViewable || attributes.width <= 0 || attributes.height <= 0) {
        return false;
    }
    int x = 0;
    int y = 0;
    if (!windowRootOrigin(display, root, window, x, y)) return false;
    return intersects(monitor, {
        .x = x,
        .y = y,
        .width = static_cast<unsigned int>(attributes.width),
        .height = static_cast<unsigned int>(attributes.height),
    });
}

std::optional<Window> findDesktopContainerRecursive(Display* display, Window root, Window window,
                                                    Window excluded, Atom windowType,
                                                    Atom desktopType,
                                                    const MonitorGeometry& monitor, int depth) {
    if (depth > 8) return std::nullopt;
    if (window != root && window != excluded &&
        windowHasDesktopType(display, window, windowType, desktopType) &&
        windowIntersectsMonitor(display, root, window, monitor)) {
        return window;
    }

    Window queryRoot = 0;
    Window queryParent = 0;
    Window* children = nullptr;
    unsigned int childCount = 0;
    if (XQueryTree(display, window, &queryRoot, &queryParent, &children, &childCount) == 0) {
        return std::nullopt;
    }

    std::optional<Window> result;
    for (unsigned int index = 0; index < childCount && !result; ++index) {
        result = findDesktopContainerRecursive(display, root, children[index], excluded,
                                                windowType, desktopType, monitor, depth + 1);
    }
    if (children) XFree(children);
    return result;
}

std::optional<Window> findDesktopContainer(Display* display, Window root, Window excluded,
                                           const MonitorGeometry& monitor) {
    return findDesktopContainerRecursive(display, root, root, excluded,
                                          atom(display, "_NET_WM_WINDOW_TYPE"),
                                          atom(display, "_NET_WM_WINDOW_TYPE_DESKTOP"), monitor, 0);
}

void setProperty(Display* display, Window window, Atom property, Atom type,
                 const unsigned char* data, int count) {
    XChangeProperty(display, window, property, type, 32, PropModeReplace, data, count);
}

bool applyNativeHints(QWidget* widget, const QRect& screenGeometry, QString* error) {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        if (error) *error = QStringLiteral("cannot open the X11 display");
        return false;
    }

    const int screen = DefaultScreen(display);
    const Window root = RootWindow(display, screen);
    const Window window = static_cast<Window>(widget->winId());
    const MonitorGeometry monitor = monitorGeometry(screenGeometry);

    Window parent = root;
    if (const auto desktop = findDesktopContainer(display, root, window, monitor)) {
        parent = *desktop;
    }

    int parentX = 0;
    int parentY = 0;
    if (!windowRootOrigin(display, root, parent, parentX, parentY)) {
        XCloseDisplay(display);
        if (error) *error = QStringLiteral("cannot determine the X11 desktop container geometry");
        return false;
    }
    const int localX = monitor.x - parentX;
    const int localY = monitor.y - parentY;
    XReparentWindow(display, window, parent, localX, localY);
    XMoveResizeWindow(display, window, localX, localY, monitor.width, monitor.height);

    const Atom desktopType = atom(display, "_NET_WM_WINDOW_TYPE_DESKTOP");
    setProperty(display, window, atom(display, "_NET_WM_WINDOW_TYPE"), XA_ATOM,
                reinterpret_cast<const unsigned char*>(&desktopType), 1);

    const std::array<Atom, 4> states {
        atom(display, "_NET_WM_STATE_BELOW"),
        atom(display, "_NET_WM_STATE_STICKY"),
        atom(display, "_NET_WM_STATE_SKIP_TASKBAR"),
        atom(display, "_NET_WM_STATE_SKIP_PAGER"),
    };
    setProperty(display, window, atom(display, "_NET_WM_STATE"), XA_ATOM,
                reinterpret_cast<const unsigned char*>(states.data()),
                static_cast<int>(states.size()));

    const unsigned long desktop = allDesktops;
    setProperty(display, window, atom(display, "_NET_WM_DESKTOP"), XA_CARDINAL,
                reinterpret_cast<const unsigned char*>(&desktop), 1);

    const Atom motifAtom = atom(display, "_MOTIF_WM_HINTS");
    const MotifWmHints motifHints {
        .flags = motifDecorationsHint,
        .functions = 0,
        .decorations = 0,
        .inputMode = 0,
        .status = 0,
    };
    setProperty(display, window, motifAtom, motifAtom,
                reinterpret_cast<const unsigned char*>(&motifHints), 5);

    XWMHints* wmHints = XAllocWMHints();
    if (wmHints) {
        wmHints->flags = InputHint;
        wmHints->input = False;
        XSetWMHints(display, window, wmHints);
        XFree(wmHints);
    }

    int eventBase = 0;
    int errorBase = 0;
    if (!XFixesQueryExtension(display, &eventBase, &errorBase)) {
        if (error) *error = QStringLiteral("XFixes is required for a click-through wallpaper window");
        XCloseDisplay(display);
        return false;
    }
    const XserverRegion emptyRegion = XFixesCreateRegion(display, nullptr, 0);
    XFixesSetWindowShapeRegion(display, window, ShapeInput, 0, 0, emptyRegion);
    XFixesDestroyRegion(display, emptyRegion);

    XLowerWindow(display, window);
    XFlush(display);
    XCloseDisplay(display);
    return true;
}

} // namespace

bool VRConfigureX11DesktopWindow(QWidget* window, int screenIndex, QString* error) {
    if (!window) {
        if (error) *error = QStringLiteral("invalid wallpaper window");
        return false;
    }
    if (QGuiApplication::platformName() != QStringLiteral("xcb")) {
        if (error) *error = QStringLiteral("VideoWallpaper supports X11 sessions only");
        return false;
    }

    const QList<QScreen*> screens = QGuiApplication::screens();
    if (screens.isEmpty()) {
        if (error) *error = QStringLiteral("no screen available");
        return false;
    }
    if (screenIndex < 0 || screenIndex >= screens.size()) screenIndex = 0;
    QScreen* screen = screens.at(screenIndex);
    const QRect initialGeometry = screen->geometry();

    window->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool |
                           Qt::WindowStaysOnBottomHint | Qt::WindowDoesNotAcceptFocus |
                           Qt::WindowTransparentForInput);
    window->setAttribute(Qt::WA_ShowWithoutActivating);
    window->setAttribute(Qt::WA_TransparentForMouseEvents);
    window->setFocusPolicy(Qt::NoFocus);
    window->setGeometry(initialGeometry);
    window->winId();

    if (!applyNativeHints(window, initialGeometry, error)) return false;

    QObject::connect(screen, &QScreen::geometryChanged, window,
                     [window](const QRect& geometry) {
                         window->resize(geometry.size());
                         QString ignored;
                         applyNativeHints(window, geometry, &ignored);
                     });
    const QPointer<QWidget> guardedWindow(window);
    QTimer::singleShot(0, window, [guardedWindow, initialGeometry] {
        if (!guardedWindow) return;
        QString ignored;
        applyNativeHints(guardedWindow, initialGeometry, &ignored);
    });
    return true;
}
