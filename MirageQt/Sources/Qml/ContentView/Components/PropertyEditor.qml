import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    id: root
    required property var host
    property var properties: []

    Repeater {
        model: root.properties
        delegate: ColumnLayout {
            required property var modelData
            property var propertyData: modelData
                Layout.fillWidth: true
                visible: root.host.propertyConditionVisible(propertyData.condition)
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    visible: propertyData.type === "bool"
                    spacing: 8
                    FluText {
                        Layout.fillWidth: true
                        text: root.host.propertyLabel(propertyData)
                        wrapMode: Text.WordWrap
                    }
                    FluToggleSwitch {
                        text: ""
                        checked: root.host.propertyBoolValue(propertyData.value)
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        clickListener: function () {
                            mirage.setSelectedProperty(propertyData.key, !checked);
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: propertyData.type === "slider"
                    spacing: 3
                    property real lowerBound: propertyData.hasMin ? Number(propertyData.min) : 0
                    property real upperBound: propertyData.hasMax ? Number(propertyData.max) : lowerBound + 1
                    RowLayout {
                        Layout.fillWidth: true
                        FluText {
                            Layout.fillWidth: true
                            text: root.host.propertyLabel(propertyData)
                            wrapMode: Text.WordWrap
                        }
                        FluText {
                            Layout.alignment: Qt.AlignRight | Qt.AlignTop
                            text: propertyData.fraction ? Number(propertyData.value).toFixed(2) : String(Math.round(Number(propertyData.value)))
                        }
                    }
                    FluSlider {
                        Layout.fillWidth: true
                        from: parent.lowerBound
                        to: Math.max(parent.upperBound, parent.lowerBound + 1)
                        stepSize: propertyData.hasStep ? Number(propertyData.step) : (propertyData.fraction ? 0.01 : 1)
                        value: Math.max(from, Math.min(to, Number(propertyData.value)))
                        onMoved: mirage.setSelectedProperty(propertyData.key, propertyData.fraction ? value : Math.round(value))
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: propertyData.type === "color"
                    spacing: 4
                    FluText {
                        Layout.fillWidth: true
                        text: root.host.propertyLabel(propertyData)
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Item {
                            Layout.fillWidth: true
                        }
                        FluColorPicker {
                            current: root.host.propertyColor(propertyData.value)
                            onAccepted: mirage.setSelectedProperty(propertyData.key, root.host.propertyColorValue(current))
                        }
                    }
                }

                ColumnLayout {
                    id: comboRow
                    Layout.fillWidth: true
                    visible: propertyData.type === "combo"
                    property var optionItems: root.host.propertyOptionItems(propertyData.options)
                    spacing: 4
                    FluText {
                        Layout.fillWidth: true
                        text: root.host.propertyLabel(propertyData)
                        wrapMode: Text.WordWrap
                    }
                    FluComboBox {
                        Layout.fillWidth: true
                        model: comboRow.optionItems
                        textRole: "label"
                        currentIndex: root.host.propertyOptionIndex(comboRow.optionItems, propertyData.value)
                        onActivated: {
                            if (currentIndex >= 0 && currentIndex < comboRow.optionItems.length) {
                                mirage.setSelectedProperty(propertyData.key, comboRow.optionItems[currentIndex].value);
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: propertyData.type === "textinput"
                    spacing: 3
                    FluText {
                        Layout.fillWidth: true
                        text: root.host.propertyLabel(propertyData)
                        wrapMode: Text.WordWrap
                    }
                    FluTextBox {
                        Layout.fillWidth: true
                        text: String(propertyData.value || "")
                        onCommit: mirage.setSelectedProperty(propertyData.key, text)
                    }
                }

                FluText {
                    Layout.fillWidth: true
                    visible: propertyData.type === "text"
                    text: root.host.propertyLabel(propertyData)
                    wrapMode: Text.WordWrap
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: propertyData.type === "group"
                    spacing: 3
                    FluText {
                        Layout.fillWidth: true
                        text: root.host.propertyLabel(propertyData)
                        wrapMode: Text.WordWrap
                        font: FluTextStyle.BodyStrong
                    }
                    FluDivider {
                        Layout.fillWidth: true
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: propertyData.type === "file" || propertyData.type === "directory" || propertyData.type === "scenetexture"
                    spacing: 3
                    FluText {
                        Layout.fillWidth: true
                        text: root.host.propertyLabel(propertyData)
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        FluText {
                            Layout.fillWidth: true
                            text: root.host.propertyPathLabel(propertyData.value)
                            elide: Text.ElideMiddle
                        }
                        FluIconButton {
                            text: "清除文件选择"
                            iconSource: FluentIcons.Clear
                            visible: String(propertyData.value || "").length > 0
                            onClicked: mirage.setSelectedProperty(propertyData.key, "")
                        }
                        FluButton {
                            text: "选择"
                            onClicked: {
                                root.host.openPropertyPicker(propertyData.key,
                                                             propertyData.type === "directory");
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: propertyData.type === "usershortcut"
                    spacing: 4
                    FluText {
                        Layout.fillWidth: true
                        text: root.host.propertyLabel(propertyData)
                        wrapMode: Text.WordWrap
                    }
                    FluTextBox {
                        Layout.fillWidth: true
                        placeholderText: "快捷方式"
                        text: String(propertyData.value || "")
                        onCommit: mirage.setSelectedProperty(propertyData.key, text)
                    }
                }

        }
    }
}
