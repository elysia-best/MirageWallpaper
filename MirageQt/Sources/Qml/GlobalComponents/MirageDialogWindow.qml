import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import FluentUI

FluWindow {
    id: dialog

    property string message: ""
    property string neutralText: "完成"
    property string negativeText: "取消"
    property string positiveText: "确定"
    property int buttonFlags: FluContentDialogType.NegativeButton | FluContentDialogType.PositiveButton
    property Component contentDelegate
    property int contentMargin: 24
    property bool closeOnPositive: true

    signal opened
    signal neutralClicked
    signal negativeClicked
    signal positiveClicked

    autoVisible: false
    autoDestroy: false
    autoCenter: true
    fixSize: true
    modality: Qt.ApplicationModal
    showMinimize: false
    showMaximize: false
    width: 460
    height: 320

    onVisibleChanged: {
        if (visible)
            opened();
    }

    function open() {
        show();
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: dialog.contentMargin
        spacing: 16

        Flickable {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: bodyColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: FluScrollBar {
                policy: ScrollBar.AsNeeded
            }

            ColumnLayout {
                id: bodyColumn
                width: body.width
                spacing: 12

                FluText {
                    Layout.fillWidth: true
                    visible: dialog.message.length > 0
                    text: dialog.message
                    wrapMode: Text.WordWrap
                }

                Loader {
                    id: contentLoader
                    Layout.fillWidth: true
                    sourceComponent: dialog.visible ? dialog.contentDelegate : undefined
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: (dialog.buttonFlags & FluContentDialogType.NeutralButton) || (dialog.buttonFlags & FluContentDialogType.NegativeButton) || (dialog.buttonFlags & FluContentDialogType.PositiveButton)
            spacing: 10

            Item {
                Layout.fillWidth: true
            }

            FluButton {
                visible: (dialog.buttonFlags & FluContentDialogType.NeutralButton) !== 0
                text: dialog.neutralText
                Layout.preferredWidth: 96
                onClicked: {
                    dialog.neutralClicked();
                    dialog.close();
                }
            }

            FluButton {
                visible: (dialog.buttonFlags & FluContentDialogType.NegativeButton) !== 0
                text: dialog.negativeText
                Layout.preferredWidth: 96
                onClicked: {
                    dialog.negativeClicked();
                    dialog.close();
                }
            }

            FluFilledButton {
                visible: (dialog.buttonFlags & FluContentDialogType.PositiveButton) !== 0
                text: dialog.positiveText
                Layout.preferredWidth: 112
                onClicked: {
                    dialog.positiveClicked();
                    if (dialog.closeOnPositive)
                        dialog.close();
                }
            }
        }
    }
}
