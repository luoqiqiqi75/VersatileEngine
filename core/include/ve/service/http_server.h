// http_server.h — ve::HttpServer: REST-like Node tree access over HTTP
#pragma once

#include "ve/global.h"
#include <cstdint>
#include <memory>

namespace ve {

class Node;

class VE_API HttpServer
{
public:
    explicit HttpServer(Node* root, uint16_t port = 8080);
    ~HttpServer();

    bool start();
    void stop();
    bool isRunning() const;

private:
    struct Private;
    std::unique_ptr<Private> _p;
};

} // namespace ve
