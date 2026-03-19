// service.h — ve::service: unified start/stop for all services/transports
#pragma once

#include "ve/global.h"
#include <cstdint>

namespace ve {

class Node;

namespace service {

struct Config {
    Node*    root     = nullptr;

    struct { uint16_t port = 8080; bool enabled = true;  } http;
    struct { uint16_t port = 8081; bool enabled = true;  } ws;
    struct { uint16_t port = 5061; bool enabled = true;  } tcp_text;
    struct { uint16_t port = 5065; bool enabled = false; } tcp_bin;
};

VE_API void startAll(const Config& cfg);
VE_API void stopAll();

VE_API void startAll(Node* root,
                     uint16_t http_port = 8080,
                     uint16_t ws_port   = 8081,
                     uint16_t tcp_port  = 5061);

} // namespace service
} // namespace ve
