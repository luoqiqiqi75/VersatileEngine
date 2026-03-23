#include "ve/service/bin_service.h"
#include "ve/core/impl/bin.h"

namespace ve {
namespace service {
namespace bin {

Bytes encodeFrame(uint8_t flag, const Var& payload)
{
    Bytes body;
    ve::impl::bin::writeVar(payload, body);

    Bytes frame;
    frame.reserve(FRAME_HEADER_SIZE + body.size());
    frame.push_back(flag);

    uint32_t len = static_cast<uint32_t>(body.size());
    frame.push_back(static_cast<uint8_t>(len));
    frame.push_back(static_cast<uint8_t>(len >> 8));
    frame.push_back(static_cast<uint8_t>(len >> 16));
    frame.push_back(static_cast<uint8_t>(len >> 24));

    frame.insert(frame.end(), body.begin(), body.end());
    return frame;
}

bool tryPopFrame(Bytes& buf, uint8_t& flag, Var& outVar)
{
    if (buf.size() < FRAME_HEADER_SIZE) {
        return false;
    }
    flag = buf[0];
    uint32_t len = static_cast<uint32_t>(buf[1])
                 | (static_cast<uint32_t>(buf[2]) << 8)
                 | (static_cast<uint32_t>(buf[3]) << 16)
                 | (static_cast<uint32_t>(buf[4]) << 24);

    if (buf.size() < FRAME_HEADER_SIZE + len) {
        return false;
    }

    const uint8_t* payload = buf.data() + FRAME_HEADER_SIZE;
    const uint8_t* payEnd  = payload + len;
    outVar = ve::impl::bin::readVar(payload, payEnd);

    buf.erase(buf.begin(), buf.begin() + FRAME_HEADER_SIZE + len);
    return true;
}

} // namespace bin_tcp
} // namespace service
} // namespace ve
