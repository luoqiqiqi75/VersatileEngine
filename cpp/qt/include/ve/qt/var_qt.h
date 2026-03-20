// ----------------------------------------------------------------------------
// var_qt.h — Var / QVariant / QJsonValue bridging (UTF-8 for QString)
// ----------------------------------------------------------------------------
#pragma once

#include "ve/core/var.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVariant>

namespace ve::qt {

inline QString utf8ToQString(const std::string& s) { return QString::fromUtf8(s.data(), int(s.size())); }

inline std::string qStringToUtf8(const QString& s)
{
    QByteArray b = s.toUtf8();
    return std::string(b.constData(), static_cast<std::size_t>(b.size()));
}

VE_API QVariant varToQVariant(const Var& v);
VE_API Var qVariantToVar(const QVariant& q);

VE_API QJsonValue varToQJsonValue(const Var& v);
VE_API Var qJsonValueToVar(const QJsonValue& j);

inline QString varToQString(const Var& v) { return varToQVariant(v).toString(); }

} // namespace ve::qt

namespace ve::convert {

template<> inline bool parse(const QString& from, std::string& out)
{
    out = ve::qt::qStringToUtf8(from);
    return true;
}

template<> inline bool parse(const std::string& from, QString& out)
{
    out = ve::qt::utf8ToQString(from);
    return true;
}

} // namespace ve::convert
