// http_server.h — ve::HttpServer: REST-like Node tree access + static file serving over HTTP
#pragma once

#include "ve/global.h"
#include <cstdint>
#include <memory>
#include <string>

namespace ve {

class Node;

class VE_API HttpServer
{
public:
    explicit HttpServer(Node* root, uint16_t port = 8080);
    ~HttpServer();

    void setStaticRoot(const std::string& dirPath);
    void setDefaultFile(const std::string& filename);

    bool start();
    void stop();
    bool isRunning() const;

private:
    struct Private;
    std::unique_ptr<Private> _p;
};

} // namespace ve
