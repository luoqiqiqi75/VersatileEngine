#include "server.h"

#include "core/networkmanager.h"

#include <QTcpServer>
#include <QTcpSocket>

#include "terminal.h"

Server::Server(QObject *parent) : QObject(parent),
    m_server(new QTcpServer(this)),
    m_client_count(0)
{
}

void Server::start(int port)
{
    if (m_server->listen(QHostAddress::Any, port)) {
        connect(m_server, &QTcpServer::newConnection, this, &Server::registNewClient);
    } else {
        ELOG << "<imol.server> server starts failed at: " << port;
    }
}

QStringList Server::clientNames() const
{
    return m_net_name_hash.values();
}

void Server::registNewClient()
{
    QTcpSocket *socket = m_server->nextPendingConnection();
    QString socket_name = QString("socket%1").arg(m_client_count++);

    ISLOG << "<imol.server> client" << socket->localAddress() << socket->localPort() << "connected as:" << socket_name;

    net().regist(socket_name, imol::NetworkHandle::NETWORK_TCP, socket);
    net(socket_name)->handle()->setPackageType(imol::NetworkHandle::NETWORK_PACKAGE_RAW);
    connect(net(socket_name)->handle(), &imol::NetworkHandle::rawObtained, this, &Server::handleCommand);

    m_net_name_hash.insert(net(socket_name)->handle(), socket_name);

    Terminal::instance().appendMsgHandle(socket_name);
}

void Server::handleCommand(const QByteArray &bytes)
{
    QString socket_name = m_net_name_hash.value(sender(), "");

    if (socket_name.isEmpty()) {
        ELOG << "<imol.server> no socket for: " << sender();
        return;
    }

    Terminal::instance().setOutputHandle(socket_name);
    QStringList commands = QString::fromUtf8(bytes).split("\r");
    foreach (QString command, commands) {
        Terminal::instance().analyseCommand(command);
    }
    Terminal::instance().setOutputHandle();

    net(socket_name)->writeRaw(QString("######\r").toUtf8());
}
