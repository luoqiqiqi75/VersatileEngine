#include "command_service.h"

#include "imol/logmanager.h"
#include "ve/qt/service/command_server.h"

#include <QThread>

//VE_REGISTER_MODULE(ve::service::command_server, ve::service::CommandServerModule)

namespace ve::service {

CommandServer::CommandServer(Data *d) : _d(d)
{
    _server.bind_recv(VE_MEMBER_2(onSessionRecv))
    .bind_connect(VE_MEMBER_1(onClientConnected))
    .bind_disconnect(VE_MEMBER_1(onClientDisconnected));
}

CommandServer::~CommandServer()
{
    _server.destroy();
}

void CommandServer::start()
{
    auto ip = _d->r("address.ip")->getString("0.0.0.0").toStdString();
    auto port = _d->r("address.port")->getInt(5061);
    auto sep = _d->r("eof")->getString("").toStdString();
    bool ok = sep.empty() ? _server.start(ip, port, "\r\n") : _server.start(ip, port, sep);
    if (ok) {
        ILOG << "<ve.server.command> start at " << ip.c_str() << ":" << port << " successfully";
    } else {
        auto err = asio2::get_last_error();
        ELOG << "<ve.server.command> start at " << ip.c_str() << ":" << port << " failed(" << err.value() << "): " << err.message().c_str();
    }
}

void CommandServer::stop()
{
    _server.stop();
    ILOG << "Real stop";
}

void CommandServer::onSessionRecv(SessionPtr& session_ptr, std::string_view s)
{
    auto key = std::to_string(session_ptr->hash_key());
    auto client_d = _d->c("client")->c(QString::fromStdString(key));
    auto context_d = _context_map.value(session_ptr->hash_key(), ve::data::manager().rootMobj());


    ILOG << key.c_str() << " - recv [" << QThread::currentThreadId() << "]: " << s.size() << ", data: [" << QString::fromStdString(s.data()) << "]";

    session_ptr->async_send(std::string("1234") + s.data(), [] (std::size_t bytes_sent) {
        ILOG << "sent [" << QThread::currentThreadId() << "]: " << bytes_sent << ", error: "
             << asio2::get_last_error().value();
    });
}

void CommandServer::onClientConnected(ve::service::CommandServer::SessionPtr &session_ptr)
{
    auto key = std::to_string(session_ptr->hash_key());
    ILOG << "<ve.server.command> client [" << key.c_str() << "] " << session_ptr->remote_address().c_str() << ":" << session_ptr->remote_port()
        << " online at " << session_ptr->local_address().c_str() << ":" << session_ptr->local_port();
    QString client_path = QString::fromStdString("client." + key);
    _d->insert(nullptr, client_path);
    auto client_d = _d->r(client_path);
    client_d->set(_d, "address.ip", session_ptr->remote_address().c_str());
    client_d->set(_d, "address.port", session_ptr->remote_port());
    client_d->set(_d, "path", "");
    _context_map[session_ptr->hash_key()] = ve::data::manager().rootMobj();
}

void CommandServer::onClientDisconnected(ve::service::CommandServer::SessionPtr &session_ptr)
{
    ILOG << "<ve.server.command> client " << session_ptr->remote_address().c_str() << ":" << session_ptr->remote_port() << " offline";
    auto key = session_ptr->hash_key();
    _d->c("client")->remove(nullptr, QString::number(key));
    _context_map.erase(session_ptr->hash_key());
}

CommandServerModule::CommandServerModule()
{
    ILOG << "CREATE CMMOAND SERVERMODULE";
}

CommandServerModule::~CommandServerModule()
{

}

void CommandServerModule::init()
{

}

void CommandServerModule::ready()
{
    // maybe auto start
}

void CommandServerModule::deinit()
{
    if (auto s = server()) {
        s->stop();
    }
}

}

namespace ve::server::command {

void start(ve::Data *d)
{
    static auto module = ve::module::instance<service::CommandServerModule>("ve::service::command_server");
    if (!module) return; // todo error
    ve::service::CommandServer* s = module->server();
    if (s) {
        //todo
    } else {
        if (!d || d->isEmptyMobj()) d = ve::d("ve.server.command");
        s = new ve::service::CommandServer(d);
    }
    s->start();
}

void stop()
{
    static auto module = ve::module::instance<service::CommandServerModule>("ve::service::command_server");
    ve::service::CommandServer* s = module->server();
    if (s) {
        //todo
    } else {
        return;
    }
    s->stop();
}

}