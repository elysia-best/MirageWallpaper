import QtQuick
import QtQuick.Layouts
import FluentUI

ColumnLayout {
    required property var host

                        spacing: 12
                        FluText {
                            text: "播放规则"
                            font: FluTextStyle.BodyStrong
                        }
                        Repeater {
                            model: host.playbackOptions
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                FluText {
                                    Layout.fillWidth: true
                                    text: modelData.label
                                }
                                FluComboBox {
                                    Layout.preferredWidth: 180
                                    model: host.playbackModes.map(function (mode) {
                                        return mode.label;
                                    })
                                    currentIndex: host.playbackModes.map(function (mode) {
                                        return mode.key;
                                    }).indexOf(host.settingsDraft[modelData.key])
                                    onActivated: host.setSetting(modelData.key, host.playbackModes[currentIndex].key)
                                }
                            }
                        }
                        FluDivider {
                            Layout.fillWidth: true
                        }
                        FluText {
                            text: "渲染质量"
                            font: FluTextStyle.BodyStrong
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Repeater {
                                model: ["低", "中", "高", "极致"]
                                delegate: FluButton {
                                    required property string modelData
                                    Layout.fillWidth: true
                                    text: modelData
                                    onClicked: {
                                        if (modelData === "低") {
                                            host.setSetting("antiAliasing", "none");
                                            host.setSetting("textureResolution", "highPerformance");
                                        } else if (modelData === "中") {
                                            host.setSetting("antiAliasing", "msaa_x2");
                                            host.setSetting("textureResolution", "automatic");
                                        } else if (modelData === "高") {
                                            host.setSetting("antiAliasing", "msaa_x4");
                                            host.setSetting("textureResolution", "original");
                                        } else {
                                            host.setSetting("antiAliasing", "msaa_x8");
                                            host.setSetting("textureResolution", "original");
                                        }
                                    }
                                }
                            }
                        }
                        FluDivider {
                            Layout.fillWidth: true
                        }
                        FluText {
                            text: "渲染"
                            font: FluTextStyle.BodyStrong
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            FluText {
                                text: "帧率"
                            }
                            FluSlider {
                                Layout.fillWidth: true
                                from: 10
                                to: 120
                                stepSize: 1
                                value: Number(host.settingsDraft.fps || 30)
                                onMoved: host.setSetting("fps", Math.round(value))
                            }
                            FluText {
                                text: String(Math.round(host.settingsDraft.fps || 30))
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            FluText {
                                text: "抗锯齿"
                            }
                            FluComboBox {
                                Layout.fillWidth: true
                                model: ["无", "MSAA 2x", "MSAA 4x", "MSAA 8x"]
                                currentIndex: ["none", "msaa_x2", "msaa_x4", "msaa_x8"].indexOf(host.settingsDraft.antiAliasing)
                                onActivated: host.setSetting("antiAliasing", ["none", "msaa_x2", "msaa_x4", "msaa_x8"][currentIndex])
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            FluText {
                                text: "纹理分辨率"
                            }
                            FluComboBox {
                                Layout.fillWidth: true
                                model: ["原始", "自动", "高性能"]
                                currentIndex: ["original", "automatic", "highPerformance"].indexOf(host.settingsDraft.textureResolution)
                                onActivated: host.setSetting("textureResolution", ["original", "automatic", "highPerformance"][currentIndex])
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            FluText {
                                Layout.fillWidth: true
                                text: "后处理"
                            }
                            FluComboBox {
                                Layout.preferredWidth: 180
                                model: ["关闭", "启用"]
                                currentIndex: host.settingsDraft.postProcessing === "enabled" ? 1 : 0
                                onActivated: host.setSetting("postProcessing", currentIndex === 1 ? "enabled" : "disabled")
                            }
                        }
                        FluToggleSwitch {
                            text: "反射"
                            checked: !!host.settingsDraft.reflections
                            clickListener: function () {
                                host.setSetting("reflections", !checked);
                            }
                        }
                        FluToggleSwitch {
                            text: "从内存加载壁纸"
                            checked: host.settingsDraft.wallpaperLoadSource === "memory"
                            clickListener: function () {
                                host.setSetting("wallpaperLoadSource", !checked ? "memory" : "disk");
                            }
                        }
                        FluToggleSwitch {
                            text: "自动刷新创意工坊内容"
                            checked: !!host.settingsDraft.autoRefresh
                            clickListener: function () {
                                host.setSetting("autoRefresh", !checked);
                            }
                        }
                        FluToggleSwitch {
                            text: "启用音频频谱（场景与网页壁纸）"
                            checked: !!host.settingsDraft.enableSpectrum
                            clickListener: function () {
                                host.setSetting("enableSpectrum", !checked);
                            }
                        }
                    }
