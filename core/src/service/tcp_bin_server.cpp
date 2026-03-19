// tcp_bin_server.cpp — TcpBinServer: binary TCP IPC transport
//
// Frame:  [flag:1][len:4 LE][payload]
// Flag:   bits[7:6] = 00 request, 01 response, 10 notify, 11 error
// Payload is bin-encoded Var::Dict.
//
// Uses command::call() for stateless ops and SubscribeService for push.

#include "ve/service/tcp_bin_server.h"
#include "ve/service/subscribe_service.h"
#include "ve/core/node.h"
#include "ve/core/var.h"
#include "ve/core/command.h"
#include "ve/core/impl/bin.h"
#include "ve/core/log.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/tcp/tcp_server.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <mutex>
#include <unordered_map>
#include <cstring>

namespace ve {

// ============================================================================
// Frame constants
// ============================================================================

static constexpr uint8_t FLAG_REQUEST  = 0x00;
static constexpr uint8_t FLAG_RESPONSE = 0x40;
static constexpr uint8_t FLAG_NOTIFY   = 0x80;
static constexpr uint8_t FLAG_ERROR    = 0xC0;
static constexpr uint8_t FLAG_TYPE_MASK= 0xC0;

static constexpr size_t FRAME_HEADER_SIZE = 5;

// ============================================================================
// Frame encoding helpers
// ============================================================================

static Bytes encodeFrame(uint8_t flag, const Var& payload)
{
    Bytes body;
    bin::writeVar(payload, body);

    Bytes frame;
    frame.reserve(FRAME_HEADER_SIZE + body.size());
    frame.push_back(flag);

    uint32_t len = static_cast<uint32_t>(body.size());
    frame.push_back(static_cast<uint8_t>(len));
    frame.push_back(static_cast<uint8_t>(len >> 8));
    frame.push_back(static_cast<uint8_t>(len >> 16));
    frame.push_back(static_cast<uint8_t>(len >> 24));

    frame.insert(frame.end(), body.begin(), body.end());
    return frame;
}

static Bytes makeResponse(int64_t id, int code, const Var& data)
{
    Var::DictV dict;
    dict["id"] = Var(id);
    dict["code"] = Var(static_cast<int64_t>(code));
    if (!data.isNull())
        dict["data"] = data;
    uint8_t flag = (code < 0) ? FLAG_ERROR : FLAG_RESPONSE;
    return encodeFrame(flag, Var(std::move(dict)));
}

static Bytes makeNotify(const std::string& path, const Var& value)
{
    Var::DictV dict;
    dict["path"] = Var(path);
    dict["value"] = value;
    return encodeFrame(FLAG_NOTIFY, Var(std::move(dict)));
}

// ============================================================================
// Private
// ============================================================================

struct TcpBinServer::Private
{
    Node*    root = nullptr;
    uint16_t port = 5065;

    asio2::tcp_server server;
    std::mutex mtx;
    std::atomic<int> connCount{0};

    struct ConnState {
        Bytes recvBuf;
    };
    std::unordered_map<std::size_t, ConnState> connections;

    std::unique_ptr<SubscribeService> subscribeSvc;

    template<typename SessionPtr>
    void processFrames(std::size_t connKey, SessionPtr& session_ptr) {
        ConnState* cs = nullptr;
        {
            std::lock_guard<std::mutex> lock(mtx);
            auto it = connections.find(connKey);
            if (it != connections.end()) cs = &it->second;
        }
        if (!cs) return;

        auto& buf = cs->recvBuf;
        while (buf.size() >= FRAME_HEADER_SIZE) {
            uint8_t flag = buf[0];
            uint32_t len = static_cast<uint32_t>(buf[1])
                         | (static_cast<uint32_t>(buf[2]) << 8)
                         | (static_cast<uint32_t>(buf[3]) << 16)
                         | (static_cast<uint32_t>(buf[4]) << 24);

            if (buf.size() < FRAME_HEADER_SIZE + len)
                break;

            const uint8_t* payload = buf.data() + FRAME_HEADER_SIZE;
            const uint8_t* payEnd  = payload + len;

            Var msg = bin::readVar(payload, payEnd);

            buf.erase(buf.begin(), buf.begin() + FRAME_HEADER_SIZE + len);

            uint8_t msgType = flag & FLAG_TYPE_MASK;
            if (msgType != FLAG_REQUEST)
                continue;

            handleRequest(connKey, session_ptr, msg);
        }
    }

    template<typename SessionPtr>
    void handleRequest(std::size_t connKey, SessionPtr& session_ptr, const Var& msg) {
        if (!msg.isDict()) return;
        auto& dict = msg.toDict();

        std::string op;
        std::string path;
        Var args;
        int64_t id = 0;

        if (dict.has("op"))   op   = dict["op"].toString();
        if (dict.has("path")) path = dict["path"].toString();
        if (dict.has("args")) args = dict["args"];
        if (dict.has("id"))   id   = dict["id"].toInt64();

        if (op == "subscribe") {
            auto sessionId = static_cast<uint64_t>(connKey);
            subscribeSvc->subscribe(sessionId, path);
            auto frame = makeResponse(id, 0, Var(true));
            session_ptr->async_send(std::string(frame.begin(), frame.end()));
            return;
        }
        if (op == "unsubscribe") {
            auto sessionId = static_cast<uint64_t>(connKey);
            subscribeSvc->unsubscribe(sessionId, path);
            auto frame = makeResponse(id, 0, Var(true));
            session_ptr->async_send(std::string(frame.begin(), frame.end()));
            return;
        }

        // Build command input: LIST{path, ...args}
        Var::ListV inputList;
        inputList.push_back(Var(path));
        if (args.type() == Var::LIST) {
            for (auto& a : args.toList())
                inputList.push_back(a);
        }

        auto result = command::call(op, Var(std::move(inputList)));

        auto frame = makeResponse(id, result.code(), result.content());
        session_ptr->async_send(std::string(frame.begin(), frame.end()));
    }
};

// ============================================================================
// TcpBinServer
// ============================================================================

TcpBinServer::TcpBinServer(Node* root, uint16_t port)
    : _p(std::make_unique<Private>())
{
    _p->root = root;
    _p->port = port;
}

TcpBinServer::~TcpBinServer()
{
    stop();
}

bool TcpBinServer::start()
{
    _p->subscribeSvc = std::make_unique<SubscribeService>(_p->root);
    _p->subscribeSvc->setPushCallback(
        [this](uint64_t sessionId, const std::string& path, const Var& value) {
            auto frame = makeNotify(path, value);
            std::string data(frame.begin(), frame.end());

            _p->server.post([this, sessionId, data = std::move(data)]() {
                _p->server.foreach_session(
                    [&](auto& session_ptr) {
                        if (static_cast<uint64_t>(session_ptr->hash_key()) == sessionId)
                            session_ptr->async_send(data);
                    });
            });
        });
    _p->subscribeSvc->start();

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
        _p->subscribeSvc->removeSession(static_cast<uint64_t>(key));
        {
            std::lock_guard<std::mutex> lock(_p->mtx);
            _p->connections.erase(key);
        }
        _p->connCount.fetch_sub(1, std::memory_order_relaxed);
    });

    bool ok = _p->server.start("0.0.0.0", _p->port);
    if (ok)
        veLogIs("TcpBinServer started on port", _p->port);
    else
        veLogEs("TcpBinServer failed to start on port", _p->port);
    return ok;
}

void TcpBinServer::stop()
{
    if (_p->subscribeSvc) {
        _p->subscribeSvc->stop();
        _p->subscribeSvc.reset();
    }
    _p->server.stop();
    std::lock_guard<std::mutex> lock(_p->mtx);
    _p->connections.clear();
}

bool TcpBinServer::isRunning() const
{
    return _p->server.is_started();
}

int TcpBinServer::connectionCount() const
{
    return _p->connCount.load(std::memory_order_relaxed);
}

uint16_t TcpBinServer::port() const
{
    return _p->port;
}

} // namespace ve
