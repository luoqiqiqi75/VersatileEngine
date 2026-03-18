// service.h — ve::service: unified start/stop for all services
#pragma once

#include "ve/global.h"
#include <cstdint>

namespace ve {

class Node;

namespace service {

VE_API void startAll(Node* root,
                     uint16_t http_port = 8080,
                     uint16_t ws_port   = 8081,
                     uint16_t tcp_port  = 5061);
VE_API void stopAll();

} // namespace service
} // namespace ve
