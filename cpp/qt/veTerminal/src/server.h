#ifndef IMOL_SERVER_H
#define IMOL_SERVER_H

#include <QObject>
#include <QHash>
#include <QStringList>

class QTcpServer;
class Server : public QObject
{
    Q_OBJECT

public:
    explicit Server(QObject *parent = nullptr);

    void start(int port);

    QStringList clientNames() const;

private slots:
    void registNewClient();

    void handleCommand(const QByteArray &bytes);

private:
    QTcpServer *m_server;

    int m_client_count;
    QHash<QObject *, QString> m_net_name_hash;
};

#endif // IMOL_SERVER_H
