// node_http_service.h — HTTP access to the Node tree + optional static files
#pragma once

#include "ve/global.h"
#include <cstdint>
#include <memory>
#include <string>

namespace ve {

class Node;

namespace service {

class VE_API NodeHttpServer
{
public:
    explicit NodeHttpServer(Node* root, uint16_t port = 8080);
    ~NodeHttpServer();

    void setStaticRoot(const std::string& dirPath);
    void setDefaultFile(const std::string& filename);

    bool start();
    void stop();
    bool isRunning() const;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

class VE_API NodeWsServer
{
public:
    explicit NodeWsServer(Node* root, uint16_t port = 8081);
    ~NodeWsServer();

    bool start();
    void stop();
    bool isRunning() const;
    int  connectionCount() const;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

} // namespace service
} // namespace ve
