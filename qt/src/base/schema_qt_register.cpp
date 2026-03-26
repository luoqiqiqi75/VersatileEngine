// schema_qt_register.cpp — Register Qt schema formats for terminal
#include "ve/core/schema.h"
#include "ve/core/node.h"
#include "ve/qt/schema_qt.h"
#include <QJsonDocument>
#include <QByteArray>

namespace ve::qt {

static void registerQtSchemaFormats()
{
    // Register qjson format
    schema::registerSchemaFormat("qjson", {
        [](const Node* node) -> std::string {
            auto doc = nodeToQJsonDoc(node);
            auto ba = doc.toJson(QJsonDocument::Indented);
            return std::string(ba.constData(), ba.size());
        },
        [](Node* node, const std::string& data) -> bool {
            auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(data));
            return importQJsonInto(node, doc);
        }
    });
}

// Auto-register on module load
static struct QtSchemaFormatRegistrar {
    QtSchemaFormatRegistrar() { registerQtSchemaFormats(); }
} _qt_schema_format_registrar;

} // namespace ve::qt
