#pragma once

#include <ve/rtt/loop_object.h>
#include <ve/rtt/loop_manager.h>

namespace imol {

enum NetType { NET_RAW, NET_CACHE, NET_BACKSLASH_R };

using NetHandler = std::function<int(const std::string& addr, const std::string& msg)>;
using NetErrorHandler = std::function<void(const std::string& addr, int error)>;

class MsgHandler {
public:
    virtual ~MsgHandler() = default;
    virtual int handle(const NetHandler& handler, const std::string& addr, const char* data, size_t len) = 0;
};

class RawMsgHandler : public MsgHandler {
public:
    int handle(const NetHandler& handler, const std::string& addr, const char* data, size_t len) override;
};

class CacheMsgHandler : public MsgHandler {
public:
    int handle(const NetHandler& handler, const std::string& addr, const char* data, size_t len) override;
private:
    std::string m_bytes;
};

class BackslashRMsgHandler : public MsgHandler {
public:
    int handle(const NetHandler& handler, const std::string& addr, const char* data, size_t len) override;
private:
    std::string m_bytes;
};

MsgHandler* createMsgHandler(NetType type);

class NetObject : public Object {
public:
    explicit NetObject(const std::string& name, NetType type = NET_RAW);
    virtual ~NetObject();

    void setHandler(const NetHandler& h) { m_handler = h; }
    void setErrorHandler(const NetErrorHandler& h) { m_err_handler = h; }

    const NetHandler& handler() const { return m_handler; }
    const NetErrorHandler& errorHandler() const { return m_err_handler; }

    LoopObject* loop() const { return m_loop; }

protected:
    void setLoop(LoopObject* l) { m_loop = l; }
    int dispatchMessage(const std::string& addr, const char* data, size_t len);

private:
    LoopObject* m_loop = nullptr;
    NetHandler m_handler;
    NetErrorHandler m_err_handler;
    MsgHandler* m_msg_handler = nullptr;
};

} // namespace imol
