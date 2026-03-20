#include "ve/qt/var_qt.h"

#include <QJsonArray>
#include <QtGlobal>
#include <limits>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  include <QMetaType>
#endif

namespace ve::qt {

QVariant varToQVariant(const Var& v)
{
    switch (v.type()) {
        case Var::NONE: return QVariant();
        case Var::BOOL: return QVariant(v.toBool());
        case Var::INT: return QVariant(static_cast<qlonglong>(v.toInt64(0)));
        case Var::DOUBLE: return QVariant(v.toDouble(0.0));
        case Var::STRING: return QVariant(utf8ToQString(v.toString()));
        case Var::BIN: {
            const Bytes& b = v.toBin();
            return QVariant(QByteArray(reinterpret_cast<const char*>(b.data()), int(b.size())));
        }
        case Var::LIST: {
            QVariantList out;
            for (const Var& it : v.toList()) {
                out.append(varToQVariant(it));
            }
            return QVariant(out);
        }
        case Var::DICT: {
            QVariantMap out;
            const Var::DictV& d = v.toDict();
            for (const std::string& k : d.keys()) {
                out.insert(utf8ToQString(k), varToQVariant(d.value(k)));
            }
            return QVariant(out);
        }
        case Var::POINTER:
        case Var::CUSTOM:
        default: return QVariant();
    }
}

Var qVariantToVar(const QVariant& q)
{
    if (!q.isValid()) {
        return Var();
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    {
        const int tid = q.typeId();
        if (tid == QMetaType::fromType<QVariantList>().id()) {
            Var::ListV list;
            for (const QVariant& it : q.toList()) {
                list.push_back(qVariantToVar(it));
            }
            return Var(std::move(list));
        }
        if (tid == QMetaType::fromType<QVariantMap>().id()) {
            Var::DictV dict;
            const QVariantMap m = q.toMap();
            for (auto it = m.begin(); it != m.end(); ++it) {
                dict[qStringToUtf8(it.key())] = qVariantToVar(it.value());
            }
            return Var(std::move(dict));
        }
        if (tid == QMetaType::fromType<QVariantHash>().id()) {
            Var::DictV dict;
            const QVariantHash h = q.toHash();
            for (auto it = h.begin(); it != h.end(); ++it) {
                dict[qStringToUtf8(it.key())] = qVariantToVar(it.value());
            }
            return Var(std::move(dict));
        }
        switch (tid) {
            case QMetaType::Bool: return Var(q.toBool());
            case QMetaType::Int: return Var(q.toInt());
            case QMetaType::UInt: return Var(static_cast<std::int64_t>(q.toUInt()));
            case QMetaType::LongLong: return Var(static_cast<std::int64_t>(q.toLongLong()));
            case QMetaType::ULongLong: return Var(static_cast<std::int64_t>(q.toULongLong()));
            case QMetaType::Double: return Var(q.toDouble());
            case QMetaType::QString: return Var(qStringToUtf8(q.toString()));
            case QMetaType::QByteArray: {
                QByteArray b = q.toByteArray();
                Bytes bytes(b.begin(), b.end());
                return Var(std::move(bytes));
            }
            case QMetaType::QStringList: {
                Var::ListV list;
                for (const QString& s : q.toStringList()) {
                    list.push_back(Var(qStringToUtf8(s)));
                }
                return Var(std::move(list));
            }
            default: return Var(qStringToUtf8(q.toString()));
        }
    }
#else
    switch (static_cast<int>(q.type())) {
        case QVariant::Bool: return Var(q.toBool());
        case QVariant::Int: return Var(q.toInt());
        case QVariant::UInt: return Var(static_cast<std::int64_t>(q.toUInt()));
        case QVariant::LongLong: return Var(static_cast<std::int64_t>(q.toLongLong()));
        case QVariant::ULongLong: return Var(static_cast<std::int64_t>(q.toULongLong()));
        case QVariant::Double: return Var(q.toDouble());
        case QVariant::String: return Var(qStringToUtf8(q.toString()));
        case QVariant::ByteArray: {
            QByteArray b = q.toByteArray();
            Bytes bytes(b.begin(), b.end());
            return Var(std::move(bytes));
        }
        case QVariant::StringList: {
            Var::ListV list;
            for (const QString& s : q.toStringList()) {
                list.push_back(Var(qStringToUtf8(s)));
            }
            return Var(std::move(list));
        }
        case QVariant::List: {
            Var::ListV list;
            for (const QVariant& it : q.toList()) {
                list.push_back(qVariantToVar(it));
            }
            return Var(std::move(list));
        }
        case QVariant::Map: {
            Var::DictV dict;
            const QVariantMap m = q.toMap();
            for (auto it = m.begin(); it != m.end(); ++it) {
                dict[qStringToUtf8(it.key())] = qVariantToVar(it.value());
            }
            return Var(std::move(dict));
        }
        case QVariant::Hash: {
            Var::DictV dict;
            const QVariantHash h = q.toHash();
            for (auto it = h.begin(); it != h.end(); ++it) {
                dict[qStringToUtf8(it.key())] = qVariantToVar(it.value());
            }
            return Var(std::move(dict));
        }
        default: return Var(qStringToUtf8(q.toString()));
    }
#endif
}

QJsonValue varToQJsonValue(const Var& v)
{
    switch (v.type()) {
        case Var::NONE: return QJsonValue(QJsonValue::Null);
        case Var::BOOL: return QJsonValue(v.toBool());
        case Var::INT: return QJsonValue(static_cast<double>(v.toInt64(0)));
        case Var::DOUBLE: return QJsonValue(v.toDouble(0.0));
        case Var::STRING: return QJsonValue(utf8ToQString(v.toString()));
        case Var::BIN: {
            const Bytes& b = v.toBin();
            return QJsonValue(QString::fromLatin1(
                QByteArray(reinterpret_cast<const char*>(b.data()), int(b.size())).toBase64()));
        }
        case Var::LIST: {
            QJsonArray a;
            for (const Var& it : v.toList()) {
                a.append(varToQJsonValue(it));
            }
            return QJsonValue(a);
        }
        case Var::DICT: {
            QJsonObject o;
            const Var::DictV& d = v.toDict();
            for (const std::string& k : d.keys()) {
                o.insert(utf8ToQString(k), varToQJsonValue(d.value(k)));
            }
            return QJsonValue(o);
        }
        case Var::POINTER:
        case Var::CUSTOM:
        default: return QJsonValue(QJsonValue::Undefined);
    }
}

Var qJsonValueToVar(const QJsonValue& j)
{
    if (j.isNull() || j.isUndefined()) {
        return Var();
    }
    if (j.isBool()) {
        return Var(j.toBool());
    }
    if (j.isDouble()) {
        double d = j.toDouble();
        std::int64_t i = static_cast<std::int64_t>(d);
        if (static_cast<double>(i) == d && d >= static_cast<double>(std::numeric_limits<int>::min())
            && d <= static_cast<double>(std::numeric_limits<int>::max())) {
            return Var(static_cast<int>(i));
        }
        return Var(d);
    }
    if (j.isString()) {
        return Var(qStringToUtf8(j.toString()));
    }
    if (j.isArray()) {
        Var::ListV list;
        for (const QJsonValue& it : j.toArray()) {
            list.push_back(qJsonValueToVar(it));
        }
        return Var(std::move(list));
    }
    if (j.isObject()) {
        Var::DictV dict;
        const QJsonObject o = j.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) {
            dict[qStringToUtf8(it.key())] = qJsonValueToVar(it.value());
        }
        return Var(std::move(dict));
    }
    return Var();
}

} // namespace ve::qt
