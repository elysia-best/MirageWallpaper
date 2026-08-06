import QtQuick

Image {
    property url imageUrl
    source: imageUrl
    asynchronous: true
    fillMode: Image.PreserveAspectCrop
}
