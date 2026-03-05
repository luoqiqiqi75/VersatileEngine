#pragma once

namespace imol {
namespace xservice {

enum ErrorCode : int {
    PREFIX              = -0x01010000,
    ERR_JSON_FORMAT     = -0x01010001,
    ERR_NO_DATA_KEY     = -0x01010002,
    ERR_NO_CMD_KEY      = -0x01010003,
    ERR_G_NO_KEY        = -0x01010100,
    ERR_S_DEFAULT       = -0x01010200,
    ERR_S_MERGE         = -0x01010201,
    ERR_C_NOT_FINISHED  = -0x01010300,
    ERR_INPUT_INVALID   = -0x01010400
};

} // namespace xservice
} // namespace imol
