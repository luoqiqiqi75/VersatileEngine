// Per-peer execution and transport adapters for the native node protocol.
#pragma once

#include "node_commands.h"
#include "server_util.h"
#include "ve/core/pipeline.h"
#include "ve/core/schema.h"

namespace ve {
namespace service {

struct NodeConnection : ExecutionConnection
{
    std::shared_ptr<Session> session;
    std::string recvBuf;
    Bytes binBuf;

    void waitAsync() { session->waitAsync(); }
};

template<typename Server>
void bindNodeConnections(Server& server, ConnectionLoops<NodeConnection>& connections,
                         Node* root, bool notifications = true, std::string delimiter = {})
{
    server.bind_connect([&connections, root, notifications, delimiter](auto& socket) {
        socket->set_disconnect_timeout(std::chrono::seconds(2));
        auto state = std::make_shared<NodeConnection>();
        std::weak_ptr<NodeConnection> weak_state = state;
        std::weak_ptr<typename std::decay_t<decltype(socket)>::element_type> weak_socket = socket;
        Session::SendFn send;
        if (notifications) {
            send = [weak_state, weak_socket, delimiter](std::string message) {
                sendConnection(weak_state, weak_socket, message + delimiter);
            };
        }
        state->session = std::make_shared<Session>(root, root, std::move(send));
        if (!connections.add(connectionKey(socket), std::move(state))) socket->stop();
    });
    server.bind_disconnect([&connections](auto& socket) {
        connections.remove(connectionKey(socket));
    });
}

template<typename Socket>
void processNodeJson(const std::shared_ptr<NodeConnection>& state,
                     const std::weak_ptr<Socket>& weak_socket, const std::string& data,
                     const std::string& delimiter = {})
{
    std::weak_ptr<NodeConnection> weak_state = state;
    auto reply = [weak_state, weak_socket, delimiter](const Node& node) {
        sendConnection(weak_state, weak_socket,
            schema::fromNode<schema::JsonS>(&node, schema::JsonS::compact()) + delimiter);
    };
    Pipeline pipe;
    if (!schema::toNode<schema::JsonS>(pipe.contextNode(), data)) {
        Node error;
        error.set("code", int64_t(ERR_INVALID));
        error.set("message", "invalid JSON");
        reply(error);
        return;
    }
    executeNodeRequest(std::move(pipe), state->session, std::move(reply));
}

} // namespace service
} // namespace ve
