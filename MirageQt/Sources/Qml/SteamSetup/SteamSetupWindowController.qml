import QtQuick

QtObject {
    property var window

    function open() {
        if (window)
            window.show();
    }

    function close() {
        if (window)
            window.close();
    }
}
