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
    explicit NodeHttpServer(const Node* config_n);
    ~NodeHttpServer();

    bool start();
    void stop(bool wait = true);
    bool isRunning() const;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

class VE_API NodeWsServer
{
public:
    explicit NodeWsServer(const Node* config_n);
    ~NodeWsServer();

    bool start();
    void stop(bool wait = true);
    bool isRunning() const;
    int  connectionCount() const;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

class VE_API NodeTcpServer
{
public:
    explicit NodeTcpServer(const Node* config_n);
    ~NodeTcpServer();

    bool start();
    void stop(bool wait = true);
    bool isRunning() const;
    int  connectionCount() const;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

class VE_API NodeUdpServer
{
public:
    explicit NodeUdpServer(const Node* config_n);
    ~NodeUdpServer();

    bool start();
    void stop(bool wait = true);
    bool isRunning() const;

private:
    VE_DECLARE_UNIQUE_PRIVATE
};

} // namespace service
} // namespace ve
