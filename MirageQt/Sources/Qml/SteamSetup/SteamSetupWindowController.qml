import QtQuick

QtObject {
    id: controller

    property var window
    property var viewModel

    function invoke(name) {
        var fn = mirage[name];
        if (typeof fn !== "function")
            return false;
        var args = Array.prototype.slice.call(arguments, 1);
        fn.apply(mirage, args);
        return true;
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
