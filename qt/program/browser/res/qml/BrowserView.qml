import QtQuick 2.15
import QtWebView
import VEQuickNode 1.0

Item {
    id: root

    property string path: "browser"
    property alias canGoBack: webView.canGoBack
    property alias canGoForward: webView.canGoForward
    property alias currentUrl: webView.url

    function goBack()    { webView.goBack() }
    function goForward() { webView.goForward() }
    function reload()    { webView.reload() }
    function loadUrl(u)  { webView.url = u }

    VEData {
        id: urlNode
        onChanged: function(value) {
            if (value === undefined || value === null) return
            var s = String(value)
            if (s.length > 0) webView.url = s
        }
    }

    VEData {
        id: refreshNode
        onChanged: { reload() }
    }

    VEData {
        id: runJsNode
        onChanged: function(value) {
            if (value !== undefined && value !== null && String(value).length > 0) {
                webView.runJavaScript(String(value))
            }
        }
    }

    WebView {
        id: webView
        anchors.fill: parent

        settings.javaScriptEnabled: true
        settings.localContentCanAccessFileUrls: true
        settings.localStorageEnabled: true

        onLoadingChanged: function(loadRequest) {
            if (loadRequest.status === WebView.LoadSucceededStatus || loadRequest.status === 2) {
                var js = VENode.get(root.path + "/javascript/global")
                if (js !== undefined && js !== null && String(js).length > 0) {
                    webView.runJavaScript(String(js))
                }
            } else if (loadRequest.status === WebView.LoadFailedStatus) {
                console.warn("[BrowserView] load failed:", loadRequest.errorString)
            }
        }
    }

    Component.onCompleted: {
        urlNode.path = root.path + "/url"
        refreshNode.path = root.path + "/refresh"
        runJsNode.path = root.path + "/javascript/run"
        urlNode.trigger()
    }
}
