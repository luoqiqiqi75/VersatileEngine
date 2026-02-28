#include "cbs.h"

#include "ve/core/imol/core/logmanager.h"
#include "ve/service/compact_binary_service.h"

#include <QElapsedTimer>
#include <QDataStream>
#include <QThread>

#include "ve/core/imol/core/commandmanager.h"

#define DEFAULT_SERVER_PATH "ve.service.cbs.server"
#define DEFAULT_CLIENT_PATH "ve.service.cbs.client"
#define SERVER_TAG "<" << DEFAULT_SERVER_PATH << "> "
#define CLIENT_TAG "<" << DEFAULT_CLIENT_PATH << "> "

VE_REGISTER_MODULE(ve.service.cbs, ve::service::CBSModule)

namespace ve::service {

namespace internal {

bool pkg_len_ctrl(QDataStream& ds, FlagT f_all, std::size_t s, Data* pd)
{
    FlagT f_len = f_all & 0xc0;
    auto need_more_f = [&] (auto& len) {
        ds >> len;
        pd->set(nullptr, s * 100.0 / (len + 1 + sizeof(len))); // no flag byte, no len byte
        return s < len + 1 + sizeof(len);
    };
    if (f_len == l_short) {
        pd->set(nullptr, 100.0);
        return false;
    } else if (f_len == l_medium) {
        std::uint16_t len = 0;
        return need_more_f(len);
    } else if (f_len == l_long) {
        std::uint32_t len = 0;
        return need_more_f(len);
    }
    return false;
}

template<typename T> bool transaction_need_more_ctrl(bool& need_more, QDataStream& ds, T& o)
{
    ds.startTransaction();
    ds >> o;
    if (ds.commitTransaction()) return true;
    need_more = true;
    ELOG << "this transaction need more!!";
    return false;
}

bool cache_contains_more_ctrl(bool need_more, QDataStream& ds, QByteArray& cache)
{
    if (!need_more) {
        if (ds.atEnd()) {
            cache.clear();
        } else {
            cache = ds.device()->readAll();
            return true;
        }
    }
    return false;
}

void fix_pkg_offset(FlagT f, QByteArray& pkg, QDataStream& ds, int& offset)
{
    std::uint32_t len32 = pkg.size() - 5;  // no flag byte, no len byte
    if (pkg.size() > 65000) {
        f |= l_long;
        ds.device()->seek(offset);
        ds << f << len32;
    } else if (pkg.size() > 1500) {
        offset = 2;
        std::uint16_t len16 = len32;
        f |= l_medium;
        ds.device()->seek(offset);
        ds << f << len16;
    } else {
        offset = 4;
        f |= l_short;
        ds.device()->seek(offset);
        ds << f;
    }
}

template<typename S> void c_send_pkg(S& session, FlagT flag, const std::string& path)
{
    QByteArray pkg;
    QDataStream ds(&pkg, QIODevice::WriteOnly);
    ds << flag << QString::fromStdString(path);
//    ILOG << "@@ client send pkg len: " << pkg.size() << ", data: " << pkg.toHex(' ');
    session.async_send(pkg.data(), pkg.size());
}
template<typename S, typename T> void c_send_pkg_content(S& session, FlagT flag, const std::string& path, const T& content)
{
    QByteArray pkg;
    QDataStream ds(&pkg, QIODevice::WriteOnly);
    std::uint32_t len32 = 0;
    ds << flag << len32 << QString::fromStdString(path) << content;
    int offset = 0;
    fix_pkg_offset(flag, pkg, ds, offset);
    // ILOG << "@@ client send pkg len: " << pkg.size() << ", offs: " << offset << ", data: " << pkg.left(10).toHex(' ') << " ... " << pkg.right(10).toHex(' ');
    session.async_send(pkg.data() + offset, pkg.size() - offset); // should async?
}

template<bool HasID> void s_send_pkg(SessionPtr& session_ptr, FlagT f, IdT id)
{
    QByteArray pkg;
    QDataStream ds(&pkg, QIODevice::WriteOnly);
    ds << f;
    if constexpr (HasID) ds << id;
//    ILOG << "** server send pkg len: " << pkg.size() << ", data: " << pkg.toHex(' ');
    session_ptr->send(pkg.data(), pkg.size());
}
template<bool HasID, typename T> void s_send_pkg_content(asio2::tcp_session* session, FlagT f, IdT id, const T& content)
{
    QByteArray pkg;
    QDataStream ds(&pkg, QIODevice::WriteOnly);
    std::uint32_t len32 = 0;
    int offset = 0;
    ds << f << len32;
    if constexpr (HasID) ds << id;
    ds << content;
    fix_pkg_offset(f, pkg, ds, offset);
//    ILOG << "** server send pkg len: " << pkg.size() << ", offs: " << offset << ", data: " << pkg.left(10).toHex(' ') << " ... " << pkg.right(10).toHex(' ');
    session->socket().send(asio::buffer(pkg.data() + offset, pkg.size() - offset)); // use original
//    session->set_sndbuf_size(65536); // why this
}

}

CBSServer::CBSServer(ve::Data *d) : _d(d),
    _iopool(4), // todo
    _server(_iopool)
{
    _iopool.start();

    _server.bind_recv(VE_MEMBER_2(onSessionRecv))
    .bind_connect(VE_MEMBER_1(onClientConnected))
    .bind_disconnect(VE_MEMBER_1(onClientDisconnected));

    _d->set(nullptr, QVariant::fromValue(static_cast<void*>(this)));
}

CBSServer::~CBSServer()
{
    stop();
    _d->remove(nullptr, "client"); // todo: what else
    _d->set(nullptr, QVariant());
    _server.destroy();
}

void CBSServer::start()
{
    auto ip = _d->r("address.ip")->getString("0.0.0.0").toStdString();
    auto port = _d->r("address.port")->getInt(5065);
    if (_server.start(ip, port)) {
        ILOG << SERVER_TAG << "start at " << ip.c_str() << ":" << port << " successfully";
    } else {
        auto err = asio2::get_last_error();
        ELOG << SERVER_TAG << "start at " << ip.c_str() << ":" << port << " failed(" << err.value() << "): " << err.message().c_str();
    }
}

void CBSServer::stop()
{
    _server.stop();
    ILOG << SERVER_TAG << "stop";
}

void CBSServer::onSessionRecv(SessionPtr& session_ptr, std::string_view s)
{
    auto key = std::to_string(session_ptr->hash_key());
    auto client_d = _d->c("client")->c(QString::fromStdString(key));

    auto& cache = _cache_map[session_ptr->hash_key()];
    if (cache.isEmpty()) {
        cache = QByteArray(s.data(), static_cast<int>(s.size()));
    } else {
        cache.append(s.data(), static_cast<int>(s.size()));
    }
server_recv:
    QDataStream ds(cache);

    FlagT f_all = 0;
    ds >> f_all;

    bool need_more_data = internal::pkg_len_ctrl(ds, f_all, cache.size(), d(client_d, "progress"));
    if (need_more_data) return;

    // SLOG << SERVER_TAG << "** server recv len:" << cache.size() << ", data: " << cache.left(10).toHex(' ') << " ... " << cache.right(10).toHex(' ');

    QString path;
    ds >> path;

    auto target_d = data::at(path);
    if (!target_d || target_d->isEmptyMobj()) {
        ELOG << SERVER_TAG << "session [" << key.c_str() << "] invalid target path: \"" << path << "\"";
        internal::s_send_pkg<false>(session_ptr, m_error, 0);
        cache.clear(); // abandon
        return;
    }

    if (flags::get(f_all, g_advanced)) {
        // todo
    } else {
        bool is_recursive = flags::get(f_all, o_recursive);
        const char* recursive_tag = is_recursive ? " recursively" : "";

        FlagT f_low = f_all & 0xf;
        FlagT f_command = f_all & 0x6;
        if (f_command == c_echo) {
            ILOG << SERVER_TAG << "session [" << key.c_str() << "] echo " << path << recursive_tag;
            if (is_recursive) {
                internal::s_send_pkg_content<false>(session_ptr.get(), m_response | f_low, 0, qCompress(target_d->exportToBin(), 1));
            } else {
                internal::s_send_pkg_content<false>(session_ptr.get(), m_response | f_low, 0, target_d->get());
            }
        } else if (f_command == c_pub) {
            if (is_recursive) {
                QByteArray bytes;
                if (internal::transaction_need_more_ctrl(need_more_data, ds, bytes)) target_d->importFromBin(client_d, qUncompress(bytes), true, true, true);
            } else {
                QVariant var;
                if (internal::transaction_need_more_ctrl(need_more_data, ds, var)) target_d->set(client_d, var);
            }
            if (!need_more_data) {
                // ILOG << SERVER_TAG << "session [" << key.c_str() << "] publish " << path << recursive_tag;
                internal::s_send_pkg<false>(session_ptr, m_response | f_low, 0);
            }
        } else if (f_command == c_sub) {
            ILOG << SERVER_TAG << "session [" << key.c_str() << "] subscribe " << path << recursive_tag;
            auto async_connect_f = [=] (auto f1, auto f2) {
                QObject::connect(target_d, f1, client_d, [=] { session_ptr->post(f2); }, Qt::DirectConnection);
            };
            auto id = client_d->c("id")->get().value<IdT>();

            if (is_recursive) {
                async_connect_f(&ve::Data::changed, [=, session = session_ptr.get()] {
                    // ILOG << "** server async post " << target_d->fullName() << " id " << id << " <recursive>";
                    internal::s_send_pkg_content<true>(session, m_notify | f_low, id, qCompress(target_d->exportToBin(), 1));
                });
            } else {
                async_connect_f(&ve::Data::changed, [=, session = session_ptr.get()] {
                    // ILOG << "** server async post " << target_d->fullName() << " id " << id << " <single>";
                    internal::s_send_pkg_content<true>(session, m_notify | f_low, id, target_d->get());
                });
            }
            client_d->set(target_d, "id", static_cast<IdT>(id + 1));

            internal::s_send_pkg<true>(session_ptr, m_response | f_low, id);
        } else if (f_command == c_cancel) {
            ILOG << SERVER_TAG << "session [" << key.c_str() << "] unsubscribe " << path;
            target_d->disconnect(client_d); // cannot disconnect single signal
            internal::s_send_pkg<false>(session_ptr, m_response | c_cancel, 0);
        }
    }

    if (internal::cache_contains_more_ctrl(need_more_data, ds, cache)) goto server_recv;
}

void CBSServer::onClientConnected(SessionPtr &session_ptr)
{
    auto key = std::to_string(session_ptr->hash_key());
    ILOG << SERVER_TAG << "session [" << key.c_str() << "] " << session_ptr->remote_address().c_str() << ":" << session_ptr->remote_port()
         << " online at " << session_ptr->local_address().c_str() << ":" << session_ptr->local_port();
    QString client_path = QString::fromStdString("client." + key);
    _d->insert(nullptr, client_path);
    auto client_d = _d->r(client_path);
    client_d->set(_d, "address.ip", session_ptr->remote_address().c_str());
    client_d->set(_d, "address.port", session_ptr->remote_port());
    client_d->set(_d, "path", "");
}

void CBSServer::onClientDisconnected(SessionPtr &session_ptr)
{
    ILOG << SERVER_TAG << "client " << session_ptr->remote_address().c_str() << ":" << session_ptr->remote_port() << " offline";
    auto key = session_ptr->hash_key();
    _d->c("client")->remove(nullptr, QString::number(key));
}

CBSClient::CBSClient(ve::Data *d) : _d(d)
{
    _client.auto_reconnect(_d->r("reconnect.enable")->getBool(false),
                           std::chrono::milliseconds(_d->r("reconnect.delay")->getInt(300)));
    auto connect_timeout = std::chrono::milliseconds(_d->r("timeout")->getInt(200));
    _client.set_connect_timeout(connect_timeout);
    _client.set_disconnect_timeout(connect_timeout);
//    _client.buffer().pre_size(65536); // useless

    _client.bind_recv(VE_MEMBER_1(onRecv))
    .bind_connect(VE_MEMBER_0(onConnected))
    .bind_disconnect(VE_MEMBER_0(onDisconnected));

    _d->set(nullptr, QVariant::fromValue(static_cast<void*>(this)));
}

CBSClient::~CBSClient()
{
    _d->remove(nullptr, "data"); // todo: what else
    _d->set(nullptr, QVariant());
}

void CBSClient::connectToHost()
{
    auto host_ip = _d->r("host.ip")->getString("0.0.0.0").toStdString();
    auto host_port = _d->r("host.port")->getString("5065").toStdString();
    ILOG << CLIENT_TAG << "try to connect to " << host_ip.c_str() << ":" << host_port.c_str();
    _client.start(host_ip, host_port);
}

void CBSClient::disconnectFromHost()
{
    ILOG << CLIENT_TAG << "try to disconnect from " << _client.remote_address().c_str() << ":" << _client.remote_port();
    _client.stop();
}

template<HeadFlag C, bool R> void CBSClient::execRequest(const std::string &remote_path, Data *local_data)
{
    _mutex.lock();
    std::lock_guard<std::mutex> lc(_cm);
    _pending.push_back({ remote_path, local_data });
    if constexpr (R) {
        internal::c_send_pkg(_client, m_request | C | o_recursive, remote_path);
    } else {
        internal::c_send_pkg(_client, m_request | C, remote_path);
    }
    if (_cva.wait_for(_cm, std::chrono::milliseconds(1000)) == std::cv_status::timeout) {
        ELOG << CLIENT_TAG << "request timeout";
    }
    _mutex.unlock();
}

template<bool R> void CBSClient::execPublish(const std::string &remote_path, Data *local_data)
{
    _mutex.lock();
    std::lock_guard<std::mutex> lc(_cm);
    // static unsigned long long cnt = 0;
    // SLOG << cnt << " +++ !!!! thread:" << QThread::currentThread();
    if constexpr (R) {
        internal::c_send_pkg_content(_client, m_request | c_pub | o_recursive, remote_path, qCompress(local_data->exportToBin(), 1));
    } else {
        internal::c_send_pkg_content(_client, m_request | c_pub, remote_path, local_data->get());
    }
    // auto t0 = std::chrono::high_resolution_clock::now();
    if (_cva.wait_for(_cm, std::chrono::milliseconds(1000)) == std::cv_status::timeout) {
        ELOG << CLIENT_TAG << "publish timeout";
    }
    // auto t1 = std::chrono::high_resolution_clock::now();
    // SLOG << cnt << " --- !!!! " << ((t1 - t0).count() / 1000) << "us";
    // cnt++;
    _mutex.unlock();
}

void CBSClient::execUnsubscribe(const std::string &remote_path)
{
    _mutex.lock();
    std::lock_guard<std::mutex> lc(_cm);
    internal::c_send_pkg(_client, m_request | c_cancel, remote_path);
    for (auto it = _map.begin(); it != _map.end();) {
        if (remote_path == it->second.first) {
            it = _map.erase(it);
        } else {
            ++it;
        }
    }
    if (_cva.wait_for(_cm, std::chrono::milliseconds(1000)) == std::cv_status::timeout) {
        ELOG << CLIENT_TAG << "unsubscribe timeout";
    }
    _mutex.unlock();
}

void CBSClient::onRecv(std::string_view s)
{
    if (_cache.isEmpty()) {
        _cache = QByteArray(s.data(), static_cast<int>(s.size()));
    } else {
        _cache.append(s.data(), static_cast<int>(s.size()));
    }
client_recv:
    QDataStream ds(_cache);

    FlagT f_all = 0;
    ds >> f_all;

    static auto pd = ve::d(_d, "progress");
    bool need_more_data = internal::pkg_len_ctrl(ds, f_all, _cache.size(), pd);

//    ILOG << "@@ client recv len " << s.size() << ", cache len: " << _cache.size();

    if (need_more_data) return;

//    ILOG << "@@ client NOW HANDLE: " << _cache.left(10).toHex(' ') << " ... " << _cache.right(10).toHex(' ');

    // message
    FlagT f_msg = f_all & 0x30;
//    FlagT f_low = f_all & 0xf;
    FlagT f_command = f_all & 0x6;
    bool is_recursive = flags::get(f_all, o_recursive);
//    const char* recursive_tag = is_recursive ? " recursively" : "";

    if (f_msg == m_error) {
        // todo
//        ILOG << "@@ client msg [error]";
    } else if (f_msg == m_response) {
        // todo: advanced
        if (f_command == c_sub) {
            IdT id = 0;
            ds >> id;
//            ILOG << "@@ client msg [response.subscribe] id " << id;
            if (_pending.empty()) {
                ELOG << CLIENT_TAG << "unexpected subscribe response with id: " << id;
            } else { // not able to distinguish watch type
                _map[id] = _pending.front();
                _pending.pop_front();
                ILOG << CLIENT_TAG << "subscribe [" << id << "]: " << _map[id].first.c_str() << " -> " << _map[id].second->fullName();
            }
        } else if (f_command == c_echo) {
            if (_pending.empty()) {
                ELOG << CLIENT_TAG << "unexpected echo response";
            } else {
                auto [remote_path, local_data] = _pending.front();
//                ILOG << "@@ client msg [response.echo]" << " path: " << remote_path.c_str() << " local: " << local_data->fullName() << (is_recursive ? " <recursive>" : " <single>");
                _pending.pop_front();
                if (is_recursive) {
                    QByteArray bytes;
                    if (internal::transaction_need_more_ctrl(need_more_data, ds, bytes)) local_data->importFromBin(_d, qUncompress(bytes), true, true, true);
                } else {
                    QVariant var;
                    if (internal::transaction_need_more_ctrl(need_more_data, ds, var)) local_data->set(_d, var);
                }
            }
        } else if (f_command == c_pub) {
            // todo: finish
//            ILOG << "@@ client msg [response.publish]";
        } else if (f_command == c_cancel) {
            // todo: finish
//            ILOG << "@@ client msg [response.cancel]";
        }
    } else if (f_msg == m_notify) {
        IdT id = 0;
        ds >> id;
        if (_map.has(id)) {
            auto [remote_path, local_data] = _map[id];
//            ILOG << "@@ client msg [notify] id " << id << " path: " << remote_path.c_str() << " local: " << local_data->fullName() << (is_recursive ? " <recursive>" : " <single>");
            if (is_recursive) {
                QByteArray bytes;
                if (internal::transaction_need_more_ctrl(need_more_data, ds, bytes)) local_data->importFromBin(_d, qUncompress(bytes), true, true, true);
            } else {
                QVariant var;
                if (internal::transaction_need_more_ctrl(need_more_data, ds, var)) local_data->set(_d, var);
            }
        } else {
            ELOG << CLIENT_TAG << "notify invalid id: " << id;
            ds.device()->readAll(); // abandon frame
        }
    } else { // not possible with error reply
        ELOG << CLIENT_TAG << "abandon frame";
        ds.device()->readAll(); // abandon frame
        return;
    }

    if (f_msg != m_notify) {
        std::lock_guard<std::mutex> lc(_cm);
        _cva.notify_one(); // sync frame finished
        // SLOG << CLIENT_TAG << "!!!!!!notify " << f_msg << ", thread: " << QThread::currentThread();
    }

    if (internal::cache_contains_more_ctrl(need_more_data, ds, _cache)) goto client_recv;
}

void CBSClient::onConnected()
{
    if (!_client.is_started()) {
        auto ec = asio2::get_last_error();
        ELOG << CLIENT_TAG << "connected FAILED from " << _client.local_address().c_str() << ":" << _client.local_port()
                           << " to " << _client.remote_address().c_str() << ":" << _client.remote_port()
                           << " reason (" << ec.value() << "): " << QString::fromLocal8Bit(ec.message().c_str());
        _d->set(nullptr, "connected", false);
        return;
    }
    ILOG  << CLIENT_TAG << "connected from " << _client.local_address().c_str() << ":" << _client.local_port()
        << " to " << _client.remote_address().c_str() << ":" << _client.remote_port();
    _d->set(nullptr, "connected", true);
}

void CBSClient::onDisconnected()
{
    ILOG  << CLIENT_TAG << "disconnect from " << _client.local_address().c_str() << ":" << _client.local_port()
         << " to " << _client.remote_address().c_str() << ":" << _client.remote_port();
    _d->set(nullptr, "connected", false);
}

class CBSServerCommand : public imol::BaseCommand
{
public:
    explicit CBSServerCommand(QObject *parent = nullptr) : BaseCommand("cbs.server", false, parent) {}
    QString usage() const override { return tr("cbs.server start|stop"); }
//    QString instruction() const override { return tr("Convert 1st type of data to 2nd type"); }

protected:
    void complete(Data*, const QString& text) override
    {
        QString half = text.section(' ', -1);
        static QStringList maybe = { "start", "stop" };
        foreach (auto str, maybe) {
            if (str.startsWith(half)) {
                emit input(str.right(str.length() - half.length()));
                break;
            }
        }
    }
    void run(Data*, const QString &param) override
    {
        if (param.contains("start")) {
            server::cbs::start();
        } else if (param.contains("stop")) {
            server::cbs::stop();
        } else {
            error("unknown param: " + param);
        }
    }
};

class CBSClientCommand : public imol::BaseCommand
{
    static QStringList const cmds;
    static QStringList const options;

public:
    explicit CBSClientCommand(QObject *parent = nullptr) : BaseCommand("cbs.client", false, parent) {}
    QString usage() const override { return tr("cbs.client <cmd> <path> [option]"); }
//    QString instruction() const override { return tr("Convert 1st type of data to 2nd type"); }

protected:
    void complete(Data *d, const QString &text) override;
    void hint(Data *d, const QString &text) override;
    void run(Data*, const QString &param) override;
};

QStringList const CBSClientCommand::cmds = { "connect", "disconnect", "echo", "pub", "sub", "unsub" };
QStringList const CBSClientCommand::options = { "--recursive", "--default" };

void CBSClientCommand::complete(ve::Data *d, const QString &text)
{
    int cnt = text.count(' ');
    if (cnt > 1) {
        QString option_half = text.section(' ', -1);
        foreach (auto str, options) {
            if (str.startsWith(option_half)) {
                emit input(str.right(str.length() - option_half.length()));
                break;
            }
        }
    } else if (cnt == 0) {
        foreach (auto cmd, cmds) {
            if (!text.isEmpty() && cmd.startsWith(text)) {
                emit input(cmd.right(cmd.length() - text.length()) + " ");
                break;
            }
        }
    }
}

void CBSClientCommand::hint(ve::Data *d, const QString &text)
{
    int cnt = text.count(' ');
    if (cnt == 0) {
        emit output(cmds.join('\t') + "\n");
    }
}

void CBSClientCommand::run(Data* d, const QString &param)
{
    if (param.startsWith("connect")) {
        QString ip = param.section(' ', -1);
        if (ip.count('.') == 3) {
            client::cbs::connectTo(ip.toStdString(), 5065);
        } else {
            client::cbs::connectTo();
        }
        output(client::cbs::defaultContextD()->c("connected")->getBool() ? "<cbs.client> connected" : "<cbs.client> connect failed" );
    } else if (param.startsWith("disconnect")) {
        client::cbs::disconnectFrom();
    } else {
        if (!client::cbs::defaultContextD()->c("connected")->getBool()) {
            error("client not yet connected");
            return;
        }
        auto path = param.section(' ', 1, 1).toStdString();
        if (path.empty()) {
            error("invalid empty remote path");
            return;
        }
        bool is_default = param.contains("--default");
        bool is_recursive = param.contains("--recursive");
        using namespace client::cbs;
        Data* res_d = is_default ? defaultLocalD(defaultContextD(), path) : d;
        if (param.startsWith("unsub")) {
            unsubscribe(path);
            return;
        }
        if (is_recursive) {
            if (param.startsWith("echo")) {
                echo<recursive>(path, res_d);
            } else if (param.startsWith("pub")) {
                publish<recursive>(path, res_d);
            } else if (param.startsWith("sub")) {
                subscribe<recursive>(path, res_d);
            }
        } else {
            if (param.startsWith("echo")) {
                echo(path, res_d);
            } else if (param.startsWith("pub")) {
                publish(path, res_d);
            } else if (param.startsWith("sub")) {
                subscribe(path, res_d);
            }
        }
    }
}

CBSModule::CBSModule()
{
}

CBSModule::~CBSModule()
{
}

void CBSModule::init()
{
    command().regist(new CBSServerCommand);
    command().regist(new CBSClientCommand);
}

void CBSModule::ready()
{
//    server::start();
}

void CBSModule::deinit()
{
//    server::stop();
}

namespace internal {


template<typename T> inline T get_as(Data* d)
{
    return static_cast<T>(d->get().value<void*>());
}

inline CBSClient* get_connected_client(Data* d)
{
    auto c = get_as<CBSClient*>(d);
    if (!c || !c->data()->c("connected")->getBool()) {
        // todo
        return nullptr;
    }
    return c;
}

}

}

namespace ve::server::cbs {

using namespace ve::service;

Data* defaultContextD()
{
    static Data* cd = d(DEFAULT_SERVER_PATH);
    return cd;
}

Data* start(Data* context)
{
    auto ps = internal::get_as<CBSServer*>(context);
    if (!ps) ps = new CBSServer(context);
    ps->start();
    return context;
}

Data* stop(Data* context)
{
    auto ps = internal::get_as<CBSServer*>(context);
    delete ps; // stop
    return context;
}

}

namespace ve::client::cbs {

using namespace ve::service;

Data* defaultContextD()
{
    static Data* cd = d(DEFAULT_CLIENT_PATH);
    return cd;
}

Data* connectTo(Data* context)
{
    auto c = internal::get_as<CBSClient*>(context);
    if (!c) c = new CBSClient(context);
    if (c->data()->c("connected")->getBool()) {
        // todo
    }
    c->connectToHost();
    return context;
}
Data* connectTo(Data* context, const std::string& ip, int port)
{
    context->set(nullptr, "host.ip", QString::fromStdString(ip));
    context->set(nullptr, "host.port", port);
    return connectTo(context);
}

Data* disconnectFrom(Data* context)
{
    auto c = internal::get_as<CBSClient*>(context);
    if (c) c->disconnectFromHost();
    return context;
}

Data* defaultLocalD(Data* context, const std::string& remote_path)
{
    return d(context, "data." + remote_path);
}

#define CBS_C_IMPL(...) \
    if (auto c = internal::get_connected_client(context)) c->__VA_ARGS__(remote_path, local_data); return local_data

template<> VE_API Data* echo<single>(Data* context, const std::string& remote_path, Data* local_data) { CBS_C_IMPL(execRequest<c_echo, false>); }
template<> VE_API Data* echo<recursive>(Data* context, const std::string& remote_path, Data* local_data) { CBS_C_IMPL(execRequest<c_echo, true>); }
template<> VE_API Data* publish<single>(Data* context, const std::string& remote_path, Data* local_data) { CBS_C_IMPL(execPublish<false>); }
template<> VE_API Data* publish<recursive>(Data* context, const std::string& remote_path, Data* local_data) { CBS_C_IMPL(execPublish<true>); }
template<> VE_API Data* subscribe<single>(Data* context, const std::string& remote_path, Data* local_data) { CBS_C_IMPL(execRequest<c_sub, false>); }
template<> VE_API Data* subscribe<recursive>(Data* context, const std::string& remote_path, Data* local_data) { CBS_C_IMPL(execRequest<c_sub, true>); }

Data* unsubscribe(Data* context, const std::string& remote_path)
{
    if (auto sc = internal::get_connected_client(context)) {
        sc->execUnsubscribe(remote_path);
        context->remove(nullptr, "data." + QString::fromStdString(remote_path));
    }
    return context;
}

}
