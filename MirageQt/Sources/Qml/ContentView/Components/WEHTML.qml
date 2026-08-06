import QtQuick
import FluentUI

FluText {
    property string html: ""
    text: html.replace(/<[^>]*>/g, "")
    wrapMode: Text.WordWrap
}
