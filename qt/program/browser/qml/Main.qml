import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Controls.Material 2.15
import QtQuick.Layouts 1.15
import VEQuickNode 1.0

ApplicationWindow {
    id: root
    width: 1280
    height: 800
    visible: true
    Material.theme: Material.Dark
    Material.accent: Material.Blue

    VEData {
        id: toolbarVisibleNode
        onChanged: function(value) {
            navbar.visible = !!value
        }
    }

    header: ToolBar {
        id: navbar
        visible: false
        Material.elevation: 2
        height: visible ? implicitHeight : 0

        RowLayout {
            anchors.fill: parent
            spacing: 2

            ToolButton {
                text: "\u2190"
                font.pixelSize: 18
                enabled: browserView.canGoBack
                onClicked: browserView.goBack()
                ToolTip.visible: hovered
                ToolTip.text: "Back"
            }

            ToolButton {
                text: "\u2192"
                font.pixelSize: 18
                enabled: browserView.canGoForward
                onClicked: browserView.goForward()
                ToolTip.visible: hovered
                ToolTip.text: "Forward"
            }

            ToolButton {
                text: "\u21BB"
                font.pixelSize: 18
                onClicked: browserView.reload()
                ToolTip.visible: hovered
                ToolTip.text: "Reload"
            }

            TextField {
                id: urlField
                Layout.fillWidth: true
                placeholderText: "Enter URL..."
                font.pixelSize: 13
                selectByMouse: true
                text: browserView.currentUrl ? String(browserView.currentUrl) : ""

                onAccepted: {
                    var u = text.trim()
                    if (u.length > 0) {
                        if (u.indexOf("://") < 0 && u.indexOf("localhost") < 0) {
                            u = "http://" + u
                        }
                        browserView.loadUrl(u)
                    }
                }

                Connections {
                    target: browserView
                    function onCurrentUrlChanged() {
                        if (!urlField.activeFocus) {
                            urlField.text = browserView.currentUrl ? String(browserView.currentUrl) : ""
                        }
                    }
                }
            }
        }
    }

    BrowserView {
        id: browserView
        anchors.fill: parent
    }

    Component.onCompleted: {
        toolbarVisibleNode.path = "browser/toolbar/visible"
    }
}
