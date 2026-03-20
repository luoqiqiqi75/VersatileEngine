// tbs_qt.h — QDataStream / QByteArray helpers for TcpBin (Var bin payload)
#pragma once

#include "ve/global.h"

#include <QtGlobal>

class QByteArray;
class QDataStream;
class QVariant;

namespace ve::qt::tbs {

VE_API QByteArray encodeFrame(uint8_t flag, const QVariant& payload);

VE_API bool decodePayload(const QByteArray& body, QVariant& out);

/// Writes [quint8 flag][quint32 len LE][raw bin body] using QDataStream::LittleEndian for len.
VE_API void writeFrame(QDataStream& ds, uint8_t flag, const QVariant& payload);

/// Reads one TBS-style frame from the stream (same layout as writeFrame).
VE_API bool readFrame(QDataStream& ds, uint8_t& flag, QVariant& payload);

} // namespace ve::qt::tbs
