import QtQuick
import "../MirageBridge.js" as MirageBridge

QtObject {
    id: controller

    property var window
    property var viewModel

    function invoke(name) {
        return MirageBridge.invoke(mirage, name, Array.prototype.slice.call(arguments, 1));
    }

    function open() {
        if (!window)
            return;
        window.show();
        window.raise();
        window.requestActivate();
    }

    function close() {
        if (viewModel && typeof viewModel.cancelPendingWork === "function")
            viewModel.cancelPendingWork();
        if (window)
            window.close();
    }
}
