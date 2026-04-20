// tcp_bin_server.cpp — ve::service::BinTcpServer
#include "ve/service/bin_service.h"
#include "ve/core/node.h"
#include "subscribe_service.h"
#include "node_protocol.h"
#include "node_task_service.h"
#include "ve/core/schema.h"
#include "server_util.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/tcp/tcp_server.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace ve {
namespace service {

static Bytes makeFrame(uint8_t flag, const Var& payload)
{
    return bin::encodeFrame(flag, payload);
}

static uint8_t flagFromReply(Node* reply)
{
    return reply->get("ok").toBool(false) ? bin::FLAG_RESPONSE : bin::FLAG_ERROR;
}

static Var toVar(const Node& node)
{
    return schema::exportAs<schema::VarS>(&node);
}

static void fillError(Node* rep, const std::string& code, const std::string& error)
{
    rep->clear();
    rep->set(Var());
    rep->set("ok", false);
    rep->set("code", code);
    rep->set("error", error);
}

struct BinTcpServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 11000;
    asio2::tcp_server server;
    std::mutex mtx;
    std::atomic<int> connCount{0};

    struct ConnState {
        Bytes recvBuf;
    };
    std::unordered_map<std::size_t, ConnState> connections;
    std::unique_ptr<SubscribeService> subscribeSvc;
    std::unique_ptr<NodeTaskService> taskSvc;

    void postToSession(uint64_t sid, uint8_t flag, Var payload)
    {
        auto frame = makeFrame(flag, payload);
        std::string data(frame.begin(), frame.end());
        server.post([this, sid, data = std::move(data)]() {
            server.foreach_session([&](auto& session_ptr) {
                if (static_cast<uint64_t>(session_ptr->hash_key()) == sid) {
                    session_ptr->async_send(data);
                }
            });
        });
    }

    template<typename SessionPtr>
    void processFrames(std::size_t connKey, SessionPtr& session_ptr)
    {
        ConnState* state = nullptr;
        {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = connections.find(connKey);
            if (it != connections.end()) {
                state = &it->second;
            }
        }
        if (!state) {
            return;
        }

        auto& buf = state->recvBuf;
        Var msg;
        uint8_t flag = 0;
        while (bin::tryPopFrame(buf, flag, msg)) {
            if ((flag & bin::FLAG_TYPE_MASK) != bin::FLAG_REQUEST) {
                continue;
            }

            Node req("req");
            if (!schema::importAs<schema::VarS>(&req, msg)) {
                Node reply("rep");
                fillError(&reply, "invalid_request", "invalid binary request");
                auto frame = makeFrame(flagFromReply(&reply), toVar(reply));
                session_ptr->async_send(std::string(frame.begin(), frame.end()));
                continue;
            }

            Node reply("rep");
            dispatchNodeProtocol(root, &req, &reply,
                                 subscribeSvc.get(), taskSvc.get(), 500,
                                 true, static_cast<uint64_t>(connKey), true,
                                 [this, sid = static_cast<uint64_t>(connKey)](const Node& event) {
                                     postToSession(sid, bin::FLAG_NOTIFY, toVar(event));
                                 });
            auto frame = makeFrame(flagFromReply(&reply), toVar(reply));
            session_ptr->async_send(std::string(frame.begin(), frame.end()));
        }
    }
};

BinTcpServer::BinTcpServer(Node* root, uint16_t port)
    : _p(std::make_unique<Private>())
{
    _p->root = root;
    _p->port = port;
}

BinTcpServer::~BinTcpServer()
{
    stop();
}

bool BinTcpServer::start()
{
    _p->subscribeSvc = std::make_unique<SubscribeService>(_p->root);
    _p->subscribeSvc->setPushCallback([this](uint64_t sessionId, const std::string& path, const Var& value) {
        Node event("event");
        event.set("event", "node.changed");
        event.set("path", path);
        event.at("value")->set(value);
        _p->postToSession(sessionId, bin::FLAG_NOTIFY, toVar(event));
    });
    _p->subscribeSvc->start();
    _p->taskSvc = std::make_unique<NodeTaskService>(_p->root);

    _p->server.bind_connect([this](auto& session_ptr) {
        auto key = session_ptr->hash_key();
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->connections[key] = {};
        }
        _p->connCount.fetch_add(1, std::memory_order_relaxed);
    });

    _p->server.bind_recv([this](auto& session_ptr, std::string_view data) {
        auto key = session_ptr->hash_key();
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            auto it = _p->connections.find(key);
            if (it != _p->connections.end()) {
                auto& buf = it->second.recvBuf;
                buf.insert(buf.end(),
                    reinterpret_cast<const uint8_t*>(data.data()),
                    reinterpret_cast<const uint8_t*>(data.data()) + data.size());
            }
        }
        _p->processFrames(key, session_ptr);
    });

    _p->server.bind_disconnect([this](auto& session_ptr) {
        auto key = session_ptr->hash_key();
        if (_p->subscribeSvc) {
            _p->subscribeSvc->removeSession(static_cast<uint64_t>(key));
        }
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->connections.erase(key);
        }
        _p->connCount.fetch_sub(1, std::memory_order_relaxed);
    });

    ve::service::disableWindowsPortReuse(_p->server);
    return _p->server.start("0.0.0.0", _p->port);
}

void BinTcpServer::stop()
{
    _p->server.stop();
    if (_p->subscribeSvc) {
        _p->subscribeSvc->stop();
        _p->subscribeSvc.reset();
    }
    _p->taskSvc.reset();
    std::lock_guard<std::mutex> lock(_p->mtx);
    _p->connections.clear();
}

bool BinTcpServer::isRunning() const
{
    return _p->server.is_started();
}

int BinTcpServer::connectionCount() const
{
    return _p->connCount.load(std::memory_order_relaxed);
}

uint16_t BinTcpServer::port() const
{
    return _p->port;
}

} // namespace service
} // namespace ve
