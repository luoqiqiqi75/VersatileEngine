// node_service.h — Node access servers: HTTP, WebSocket, TCP, UDP
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
    explicit NodeHttpServer(Node* root, uint16_t port = 5080);
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
    explicit NodeWsServer(Node* root, uint16_t port = 5081);
    ~NodeWsServer();

    bool start();
    void stop();
    bool isRunning() const;
    int  connectionCount() const;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

class VE_API NodeTcpServer
{
public:
    explicit NodeTcpServer(Node* root, uint16_t port = 5082);
    ~NodeTcpServer();

    bool start();
    void stop();
    bool isRunning() const;
    int  connectionCount() const;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

class VE_API NodeUdpServer
{
public:
    explicit NodeUdpServer(Node* root, uint16_t port = 5083);
    ~NodeUdpServer();

    bool start();
    void stop();
    bool isRunning() const;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

} // namespace service
} // namespace ve
