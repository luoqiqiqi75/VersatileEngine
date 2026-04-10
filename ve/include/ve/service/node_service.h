// node_service.h — Node access servers: HTTP, WebSocket, TCP, UDP
#pragma once

#include "ve/global.h"
#include <cstdint>

namespace ve {

class Node;

namespace service {

class VE_API NodeHttpServer
{
public:
    explicit NodeHttpServer(Node* root, uint16_t port);
    ~NodeHttpServer();

    void setStaticRoot(const std::string& dirPath);
    void setDefaultFile(const std::string& filename);
    void addProxyRule(const std::string& prefix, const std::string& target);

    bool start();
    void stop();
    bool isRunning() const;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

class VE_API NodeWsServer
{
public:
    explicit NodeWsServer(Node* root, uint16_t port);
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
    explicit NodeTcpServer(Node* root, uint16_t port);
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
    explicit NodeUdpServer(Node* root, uint16_t port);
    ~NodeUdpServer();

    bool start();
    void stop();
    bool isRunning() const;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

} // namespace service
} // namespace ve
