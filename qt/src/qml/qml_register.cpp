#include "ve/qt/qml/qml_register.h"
#include "ve/qt/qml/quick_node.h"

#include <QtQml>

namespace ve {

void registerQuickNodeQml()
{
    static bool once = false;
    if (once) {
        return;
    }
    once = true;

    qmlRegisterType<QuickNode>("VEQuickNode", 1, 0, "VEData");
    qmlRegisterSingletonType<QuickRootNode>(
        "VEQuickNode",
        1,
        0,
        "VENode",
        [](QQmlEngine*, QJSEngine*) -> QObject* { return new QuickRootNode(); });
}

void registerQuickNodeQml(QQmlEngine* engine)
{
    Q_UNUSED(engine);
    registerQuickNodeQml();
}

} // namespace ve
