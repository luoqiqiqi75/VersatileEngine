#include "ve/service/tcp_bin_client.h"
#include "ve/service/tcp_bin_frame.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <asio2/tcp/tcp_client.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <condition_variable>
#include <string>

namespace ve {

struct TcpBinClient::Private
{
    asio2::tcp_client client;

    Bytes recvBuf;

    std::mutex              waitMtx;
    std::condition_variable waitCv;
    bool                    waitActive = false;
    bool                    waitDone   = false;
    int64_t                 waitId     = 0;
    Var                     response;

    std::mutex       callMtx;
    std::atomic<int64_t> nextId{1};
    std::atomic<bool>    connected{false};

    void onRecv(std::string_view data)
    {
        recvBuf.insert(recvBuf.end(),
                       reinterpret_cast<const uint8_t*>(data.data()),
                       reinterpret_cast<const uint8_t*>(data.data()) + data.size());

        Var msg;
        uint8_t flag = 0;
        while (tcp_bin::tryPopFrame(recvBuf, flag, msg)) {
            uint8_t ty = static_cast<uint8_t>(flag & tcp_bin::FLAG_TYPE_MASK);
            if (ty == tcp_bin::FLAG_NOTIFY) {
                continue;
            }
            if (ty != tcp_bin::FLAG_RESPONSE && ty != tcp_bin::FLAG_ERROR) {
                continue;
            }
            if (!msg.isDict()) {
                continue;
            }
            auto& dict = msg.toDict();
            if (!dict.has("id")) {
                continue;
            }
            int64_t rid = dict["id"].toInt64();
            std::lock_guard<std::mutex> lk(waitMtx);
            if (waitActive && rid == waitId) {
                response  = std::move(msg);
                waitDone  = true;
                waitActive = false;
                waitCv.notify_all();
            }
        }
    }

    void onDisconnect()
    {
        connected.store(false, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lk(waitMtx);
        if (waitActive) {
            waitDone   = true;
            waitActive = false;
            waitCv.notify_all();
        }
    }
};

TcpBinClient::TcpBinClient()
    : _p(std::make_unique<Private>())
{}

TcpBinClient::~TcpBinClient()
{
    disconnect();
}

bool TcpBinClient::connect(const std::string& host, uint16_t port)
{
    disconnect();

    _p->recvBuf.clear();
    _p->client.bind_recv([this](std::string_view data) { _p->onRecv(data); });
    _p->client.bind_disconnect([this]() { _p->onDisconnect(); });

    bool ok = _p->client.start(host, std::to_string(static_cast<int>(port)));
    _p->connected.store(ok, std::memory_order_relaxed);
    if (ok) {
        std::lock_guard<std::mutex> lk(_p->waitMtx);
        _p->waitDone   = false;
        _p->waitActive = false;
    }
    return ok;
}

void TcpBinClient::disconnect()
{
    if (_p->client.is_started()) {
        _p->client.stop();
    }
    _p->connected.store(false, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(_p->waitMtx);
    if (_p->waitActive) {
        _p->waitDone = true;
    }
    _p->waitActive = false;
    _p->recvBuf.clear();
    _p->waitCv.notify_all();
}

bool TcpBinClient::isConnected() const
{
    return _p->connected.load(std::memory_order_relaxed) && _p->client.is_started();
}

bool TcpBinClient::call(Var request, Var& outResponse, int timeoutMs)
{
    std::lock_guard<std::mutex> serial(_p->callMtx);
    if (!isConnected()) {
        return false;
    }
    if (!request.isDict()) {
        return false;
    }

    Var req = std::move(request);
    auto& dict = req.toDict();
    int64_t id = 0;
    if (dict.has("id")) {
        id = dict["id"].toInt64();
    } else {
        id = _p->nextId.fetch_add(1, std::memory_order_relaxed);
        dict["id"] = Var(id);
    }

    Bytes frame = tcp_bin::encodeFrame(tcp_bin::FLAG_REQUEST, req);
    std::string packet(reinterpret_cast<const char*>(frame.data()), frame.size());

    {
        std::unique_lock<std::mutex> lk(_p->waitMtx);
        _p->waitId     = id;
        _p->waitDone   = false;
        _p->waitActive = true;
        _p->response   = Var();
    }

    _p->client.send(packet);

    std::unique_lock<std::mutex> lk(_p->waitMtx);
    bool ok = _p->waitCv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [this] {
        return _p->waitDone;
    });
    _p->waitActive = false;
    if (!ok || !_p->waitDone) {
        return false;
    }
    if (!_p->response.isDict()) {
        return false;
    }
    outResponse = std::move(_p->response);
    return true;
}

} // namespace ve
