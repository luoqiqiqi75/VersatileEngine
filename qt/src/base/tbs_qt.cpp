#include "ve/qt/tbs_qt.h"

#include "ve/qt/var_qt.h"
#include "ve/service/tcp_bin_frame.h"
#include "ve/core/impl/bin.h"

#include <QByteArray>
#include <QDataStream>
#include <QVariant>

namespace ve::qt::tbs {

QByteArray encodeFrame(uint8_t flag, const QVariant& payload)
{
    Bytes b = ve::tcp_bin::encodeFrame(flag, qVariantToVar(payload));
    return QByteArray(reinterpret_cast<const char*>(b.data()), int(b.size()));
}

bool decodePayload(const QByteArray& body, QVariant& out)
{
    if (body.isEmpty()) {
        return false;
    }
    const uint8_t* p = reinterpret_cast<const uint8_t*>(body.constData());
    const uint8_t* e = p + body.size();
    Var v = bin::readVar(p, e);
    if (p != e) {
        return false;
    }
    out = varToQVariant(v);
    return true;
}

void writeFrame(QDataStream& ds, uint8_t flag, const QVariant& payload)
{
    Var v = qVariantToVar(payload);
    Bytes body;
    bin::writeVar(v, body);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << quint8(flag) << quint32(static_cast<quint32>(body.size()));
    if (!body.empty()) {
        ds.writeRawData(reinterpret_cast<const char*>(body.data()), int(body.size()));
    }
}

bool readFrame(QDataStream& ds, uint8_t& flag, QVariant& payload)
{
    ds.setByteOrder(QDataStream::LittleEndian);
    quint8 f = 0;
    quint32 len = 0;
    ds >> f >> len;
    if (ds.status() != QDataStream::Ok) {
        return false;
    }
    flag = static_cast<uint8_t>(f);
    QByteArray body(int(len), Qt::Uninitialized);
    if (len > 0u) {
        int n = ds.readRawData(body.data(), int(len));
        if (n != int(len)) {
            return false;
        }
    }
    return decodePayload(body, payload);
}

} // namespace ve::qt::tbs
