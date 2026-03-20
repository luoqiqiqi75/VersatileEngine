// tcp_bin_frame.h — shared TcpBin (TBS) frame codec for server and client
//
// Frame: [flag:1][len:4 LE][payload] with payload = bin::writeVar(Var).
#pragma once

#include "ve/global.h"
#include "ve/core/var.h"

#include <cstdint>
#include <vector>

namespace ve {
namespace tcp_bin {

constexpr uint8_t FLAG_REQUEST  = 0x00;
constexpr uint8_t FLAG_RESPONSE = 0x40;
constexpr uint8_t FLAG_NOTIFY   = 0x80;
constexpr uint8_t FLAG_ERROR    = 0xC0;
constexpr uint8_t FLAG_TYPE_MASK = 0xC0;

constexpr std::size_t FRAME_HEADER_SIZE = 5;

VE_API Bytes encodeFrame(uint8_t flag, const Var& payload);

/// If buf holds a full frame, removes it and sets flag and outVar; otherwise returns false.
VE_API bool tryPopFrame(Bytes& buf, uint8_t& flag, Var& outVar);

} // namespace tcp_bin
} // namespace ve
