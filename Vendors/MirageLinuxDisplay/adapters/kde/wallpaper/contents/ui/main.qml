import QtQuick
import org.kde.plasma.plasmoid

WallpaperItem {
    id: root
    property bool initialized: false
    // WallpaperItem creates configuration after the package component is
    // constructed.  Do not instantiate the native display item until that
    // object exists; this avoids transient null bindings during package
    // installation and wallpaper replacement.
    readonly property bool configurationReady: root.configuration !== null

    Rectangle {
        anchors.fill: parent
        color: surfaceLoader.status === Loader.Ready
            ? surfaceLoader.item.clearColor
            : "black"
    }

    WindowModel {
        id: windowModel
        screenGeometry: Qt.rect(Screen.virtualX, Screen.virtualY,
                                Screen.width, Screen.height)
    }

    Loader {
        id: surfaceLoader
        anchors.fill: parent
        // Native QML module construction can require scene-graph resources;
        // asynchronous loading keeps Plasma's shell event loop responsive
        // while those resources become available after startup.
        asynchronous: true
        active: root.initialized && root.configurationReady
        source: "MirageSurface.qml"
        onLoaded: {
            item.configuredDisplayName = Qt.binding(function() {
                return root.configuration.DisplayName || "";
            });
            item.configuredSocketPath = Qt.binding(function() {
                return root.configuration.SocketPath || "";
            });
            item.configuredPointerForwarding = Qt.binding(function() {
                return root.configuration.MouseForward;
            });
            item.configuredWindowStateFlags = Qt.binding(function() {
                return windowModel.flags;
            });
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        visible: root.configurationReady && root.configuration.ShowDiagnostics
            && surfaceLoader.status === Loader.Ready
        color: Qt.rgba(0, 0, 0, 0.62)
        radius: 4
        width: diagnostics.implicitWidth + 16
        height: diagnostics.implicitHeight + 12

        Text {
            id: diagnostics
            anchors.centerIn: parent
            color: "white"
            font.family: "monospace"
            font.pixelSize: 12
            text: {
                const item = surfaceLoader.item
                if (!item) return ""
                let value = item.rendererBackendName + "  "
                    + (item.connected ? "connected" : "disconnected")
                value += "\noutput=" + item.outputId + " generation=" + item.importedGeneration
                value += " frames=" + item.framesReceived
                if (item.lastError.length > 0) value += "\n" + item.lastError
                return value
            }
        }
    }

    Component.onCompleted: {
        root.initialized = true
        root.loading = false
    }
}
