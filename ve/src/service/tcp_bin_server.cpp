// tcp_bin_server.cpp — ve::service::BinTcpServer (multi-session, binary frames)
#include "ve/service/bin_service.h"
#include "ve/core/node.h"
#include "ve/core/schema.h"
#include "node_session.h"
#include "node_commands.h"
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

#include "ve/core/factory.h"

namespace ve {
namespace service {

static Var toVar(const Node& node)
{
    return schema::exportAs<schema::VarS>(&node);
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
        std::unique_ptr<Session> session;
    };
    std::unordered_map<std::size_t, ConnState> connections;

    void sendFrame(uint64_t sid, uint8_t flag, const Var& payload)
    {
        auto frame = bin::encodeFrame(flag, payload);
        std::string data(frame.begin(), frame.end());
        server.post([this, sid, data = std::move(data)]() {
            server.foreach_session([&](auto& session_ptr) {
                if (static_cast<uint64_t>(session_ptr->hash_key()) == sid)
                    session_ptr->async_send(data);
            });
        });
    }

    std::unique_ptr<Session> makeSession(uint64_t sid)
    {
        return std::make_unique<Session>(root, [this, sid](std::string msg) {
            Node event;
            schema::importAs<schema::JsonS>(&event, msg);
            sendFrame(sid, bin::FLAG_NOTIFY, toVar(event));
        });
    }

    template<typename SessionPtr>
    void processFrames(std::size_t connKey, SessionPtr& session_ptr)
    {
        ConnState* state = nullptr;
        {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = connections.find(connKey);
            if (it != connections.end()) state = &it->second;
        }
        if (!state) return;

        auto& buf = state->recvBuf;
        Var msg;
        uint8_t flag = 0;
        while (bin::tryPopFrame(buf, flag, msg)) {
            if ((flag & bin::FLAG_TYPE_MASK) != bin::FLAG_REQUEST) continue;

            Node ctx;
            if (!schema::importAs<schema::VarS>(&ctx, msg)) {
                Node err;
                err.set("code", int64_t(-2));
                err.set("message", std::string("invalid binary request"));
                auto frame = bin::encodeFrame(bin::FLAG_ERROR, toVar(err));
                session_ptr->async_send(std::string(frame.begin(), frame.end()));
                continue;
            }

            if (dispatch(state->session.get(), &ctx)) {
                int code = ctx.get("code").toInt(0);
                uint8_t repFlag = code < 0 ? bin::FLAG_ERROR : bin::FLAG_RESPONSE;
                auto frame = bin::encodeFrame(repFlag, toVar(ctx));
                session_ptr->async_send(std::string(frame.begin(), frame.end()));
            }
        }
    }
};

BinTcpServer::BinTcpServer(const Node* config_n)
    : _p(std::make_unique<Private>())
{
    _p->root = ve::n(config_n->get("root").toString("/"));
    _p->port = config_n->get("port").toInt(0);
}

BinTcpServer::~BinTcpServer()
{
    stop();
}

bool BinTcpServer::start()
{
    registerNodeCommands();

    _p->server.bind_connect([this](auto& session_ptr) {
        auto key = session_ptr->hash_key();
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->connections[key].session = _p->makeSession(static_cast<uint64_t>(key));
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
