// ----------------------------------------------------------------------------
// jsonrpc_server.cpp — JSON-RPC 2.0 Server Implementation
// ----------------------------------------------------------------------------
#include "ve/service/jsonrpc_service.h"
#include "ve/core/node.h"
#include "ve/core/var.h"
#include "ve/core/command.h"
#include "ve/core/impl/json.h"
#include "ve/core/log.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/http/http_server.hpp>
#include <asio2/http/ws_server.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace ve {

// ============================================================================
// JSON-RPC 2.0 Error Codes
// ============================================================================

enum JsonRpcError
{
    ParseError = -32700,      // Invalid JSON
    InvalidRequest = -32600,  // Invalid JSON-RPC
    MethodNotFound = -32601,  // Method does not exist
    InvalidParams = -32602,   // Invalid method parameters
    InternalError = -32603    // Internal JSON-RPC error
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

JsonRpcServer::JsonRpcServer()
{
}

JsonRpcServer::~JsonRpcServer()
{
    stop();
}

// ============================================================================
// Service Interface
// ============================================================================

bool JsonRpcServer::start()
{
    if (isRunning()) {
        return true;
    }

    try {
        // Start HTTP server
        auto httpServer = std::make_shared<asio2::http_server>();

        httpServer->bind<http::verb::post>("/jsonrpc",
            [this](http::web_request& req, http::web_response& rep) {
                std::string requestBody(req.body());
                std::string response = handleRequest(requestBody);
                rep.fill_json(std::move(response), http::status::ok);
            });

        httpServer->bind_not_found(
            [](http::web_request& req, http::web_response& rep) {
                rep.fill_json(R"({"error":"Use POST /jsonrpc"})", http::status::not_found);
            });

        if (!httpServer->start("0.0.0.0", m_httpPort)) {
            veLogE << "[jsonrpc] Failed to start HTTP server on port " << m_httpPort;
            return false;
        }

        veLogI << "[jsonrpc] HTTP server started on port " << m_httpPort;

        // Start WebSocket server
        auto wsServer = std::make_shared<asio2::ws_server>();

        wsServer->bind_recv([this](auto& session_ptr, std::string_view data) {
            std::string requestJson(data);
            std::string response = handleRequest(requestJson);
            session_ptr->async_send(response);
        });

        if (!wsServer->start("0.0.0.0", m_wsPort)) {
            veLogE << "[jsonrpc] Failed to start WebSocket server on port " << m_wsPort;
            httpServer->stop();
            return false;
        }

        veLogI << "[jsonrpc] WebSocket server started on port " << m_wsPort;

        m_httpServer = httpServer;
        m_wsServer = wsServer;
        return true;
    }
    catch (const std::exception& e) {
        veLogE << "[jsonrpc] Failed to start: " << e.what();
        return false;
    }
}

void JsonRpcServer::stop()
{
    if (m_httpServer) {
        std::static_pointer_cast<asio2::http_server>(m_httpServer)->stop();
        m_httpServer.reset();
    }
    if (m_wsServer) {
        std::static_pointer_cast<asio2::ws_server>(m_wsServer)->stop();
        m_wsServer.reset();
    }
}

bool JsonRpcServer::isRunning() const
{
    return m_httpServer && std::static_pointer_cast<asio2::http_server>(m_httpServer)->is_started();
}

// ============================================================================
// JSON-RPC Request Handling
// ============================================================================

std::string JsonRpcServer::handleRequest(const std::string& requestJson)
{
    try {
        // Parse request
        Var request = impl::json::parse(requestJson);
        if (!request.isDict()) {
            return buildError(Var(), InvalidRequest, "Request must be an object");
        }

        auto& dict = request.toDict();

        // Validate JSON-RPC version
        if (!dict.has("jsonrpc") || dict["jsonrpc"].toString() != "2.0") {
            return buildError(Var(), InvalidRequest, "Invalid jsonrpc version");
        }

        // Extract id (optional for notifications)
        Var id = dict.has("id") ? dict["id"] : Var();

        // Extract method
        if (!dict.has("method") || !dict["method"].isString()) {
            return buildError(id, InvalidRequest, "Missing or invalid method");
        }
        std::string method = dict["method"].toString();

        // Extract params (optional)
        Var params = dict.has("params") ? dict["params"] : Var(Var::DictV{});

        // Route to handler
        Var result;
        if (method == "node.get") {
            result = handleNodeGet(params);
        } else if (method == "node.set") {
            result = handleNodeSet(params);
        } else if (method == "node.list") {
            result = handleNodeList(params);
        } else if (method == "command.run") {
            result = handleCommandRun(params);
        } else {
            return buildError(id, MethodNotFound, "Method not found: " + method);
        }

        return buildResponse(id, result);
    }
    catch (const std::exception& e) {
        return buildError(Var(), InternalError, e.what());
    }
}

// ============================================================================
// Method Handlers
// ============================================================================

Var JsonRpcServer::handleNodeGet(const Var& params)
{
    if (!params.isDict()) {
        throw std::runtime_error("params must be an object");
    }

    auto& dict = params.toDict();
    if (!dict.has("path")) {
        throw std::runtime_error("Missing required parameter: path");
    }

    std::string path = dict["path"].toString();
    Node* node = node::root()->find(path);

    if (!node) {
        Var::DictV result;
        result["found"] = false;
        return Var(result);
    }

    Var::DictV result;
    result["found"] = true;
    result["value"] = node->get();
    result["path"] = node->path();
    return Var(result);
}

Var JsonRpcServer::handleNodeSet(const Var& params)
{
    if (!params.isDict()) {
        throw std::runtime_error("params must be an object");
    }

    auto& dict = params.toDict();
    if (!dict.has("path") || !dict.has("value")) {
        throw std::runtime_error("Missing required parameters: path, value");
    }

    std::string path = dict["path"].toString();
    Var value = dict["value"];

    Node* node = node::root()->find(path);
    if (!node) {
        node = node::root()->at(path);
    }

    if (node) {
        node->set(value);
    }

    Var::DictV result;
    result["success"] = (node != nullptr);
    if (node) {
        result["path"] = node->path();
    }
    return Var(result);
}

Var JsonRpcServer::handleNodeList(const Var& params)
{
    if (!params.isDict()) {
        throw std::runtime_error("params must be an object");
    }

    auto& dict = params.toDict();
    if (!dict.has("path")) {
        throw std::runtime_error("Missing required parameter: path");
    }

    std::string path = dict["path"].toString();
    Node* node = node::root()->find(path);

    if (!node) {
        Var::DictV result;
        result["found"] = false;
        return Var(result);
    }

    Var::ListV children;
    for (auto* child : node->children()) {
        Var::DictV childInfo;
        childInfo["name"] = child->name();
        childInfo["path"] = child->path();
        childInfo["hasValue"] = !child->get().isNull();
        childInfo["childCount"] = static_cast<int64_t>(child->children().size());
        children.push_back(Var(childInfo));
    }

    Var::DictV result;
    result["found"] = true;
    result["path"] = node->path();
    result["children"] = Var(children);
    return Var(result);
}

Var JsonRpcServer::handleCommandRun(const Var& params)
{
    if (!params.isDict()) {
        throw std::runtime_error("params must be an object");
    }

    auto& dict = params.toDict();
    if (!dict.has("name")) {
        throw std::runtime_error("Missing required parameter: name");
    }

    std::string name = dict["name"].toString();
    Var args = dict.has("args") ? dict["args"] : Var(Var::DictV{});

    Var result = command::call(name, args);

    Var::DictV response;
    response["result"] = result;
    return Var(response);
}

// ============================================================================
// Response Builders
// ============================================================================

std::string JsonRpcServer::buildResponse(const Var& id, const Var& result)
{
    Var::DictV response;
    response["jsonrpc"] = "2.0";
    response["result"] = result;
    if (!id.isNull()) {
        response["id"] = id;
    }
    return impl::json::stringify(Var(response));
}

std::string JsonRpcServer::buildError(const Var& id, int code, const std::string& message)
{
    Var::DictV error;
    error["code"] = static_cast<int64_t>(code);
    error["message"] = message;

    Var::DictV response;
    response["jsonrpc"] = "2.0";
    response["error"] = Var(error);
    if (!id.isNull()) {
        response["id"] = id;
    } else {
        response["id"] = Var();  // null
    }

    return impl::json::stringify(Var(response));
}

} // namespace ve
