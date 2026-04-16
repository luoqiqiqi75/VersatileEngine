// schema_qt_register.cpp — Register Qt schema formats for terminal commands
#include "ve/qt/schema_qt.h"

#include <QJsonDocument>

namespace ve::schema {
namespace {

const bool qt_schema_formats_registered = []() {
    // "qjson" — QJsonS via QJsonDocument string serialization (for runtime string-based registry)
    registerSchemaFormat("qjson", {
        [](const Node* node) -> std::string {
            const QJsonValue jv = exportAs<QJsonS>(node);
            QJsonDocument doc;
            if (jv.isObject()) {
                doc = QJsonDocument(jv.toObject());
            } else if (jv.isArray()) {
                doc = QJsonDocument(jv.toArray());
            }
            const QByteArray ba = doc.toJson(QJsonDocument::Indented);
            return std::string(ba.constData(), static_cast<std::size_t>(ba.size()));
        },
        [](Node* node, const std::string& data) -> bool {
            const QJsonDocument doc = QJsonDocument::fromJson(
                QByteArray::fromRawData(data.data(), static_cast<int>(data.size())));
            QJsonValue jv;
            if (doc.isObject()) {
                jv = doc.object();
            } else if (doc.isArray()) {
                jv = doc.array();
            }
            return importAs<QJsonS>(node, jv);
        }
    });

    // "qvariant" — QVariantS via JSON string for runtime registry
    registerSchemaFormat("qvariant", {
        [](const Node* node) -> std::string {
            // Export as JSON string (QVariant has no canonical string form)
            return exportAs<JsonS>(node);
        },
        [](Node* node, const std::string& data) -> bool {
            return importAs<JsonS>(node, data);
        }
    });

    return true;
}();

} // namespace
} // namespace ve::schema
