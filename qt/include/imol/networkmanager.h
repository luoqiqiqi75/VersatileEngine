#ifndef IMOL_NETWORKMANAGER_H
#define IMOL_NETWORKMANAGER_H

#include "core_global.h"

#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QHostAddress>

#ifndef NETWORK_FRAME_MAX_SIZE
#define NETWORK_FRAME_MAX_SIZE 10240
#endif

#ifndef NETWORK_FRAME_MIN_PERCENT
#define NETWORK_FRAME_MIN_PERCENT 2
#endif

class QAbstractSocket;
class QUdpSocket;
class QTcpSocket;
class QSslSocket;
class QTimer;

namespace imol {
//!
//! \brief The NetworkFrame class is the a single transmitt unit including simple head
//!
class NetworkFrame
{
public:
    NetworkFrame(const QByteArray &content = QByteArray());
    //! the static max size of a frame
    static int maxSize() {return NETWORK_FRAME_MAX_SIZE;}

    QByteArray head() const {return m_head;}
    QByteArray content() const {return m_content;}
    int size() const {return m_head.length() + m_content.length();}

    //! \enum NetworkFrameParseError defines the error of parsing bytes to NetworkFrame
    enum NetworkFrameParseError {
        NETWORK_FRAME_PARSE_NO_ERROR,
        NETWORK_FRAME_PARSE_SIZE_ERROR,
        NETWORK_FRAME_PARSE_BREAK_ERROR
    };
    //! return whether the bytes is complete for a network frame, parse error is returned with \p error
    virtual bool checkIntegrity(const QByteArray &bytes, NetworkFrameParseError &error);

    virtual QByteArray serialize();
    virtual bool deserialize(const QByteArray &bytes);

private:
    QByteArray m_head;
    QByteArray m_content;

    bool m_is_serialized;
};

#ifndef NETWORK_PACKAGE_MAX_SIZE
#define NETWORK_PACKAGE_MAX_SIZE 524288000
#endif

//!
//! \brief The NetworkPackage class is the basic concept of packing message
//!
//! A NetworkPackage means a complete sending message with specific package protocol, the message
//! will be further cut into NetworkFrames in order to guarantee the length of each transmitt unit.
//!
class NetworkPackage
{
public:
    NetworkPackage(const QByteArray &content = QByteArray());
    virtual ~NetworkPackage();

    //! \enum NetworkPackageType is defined with usual requirements
    enum NetworkPackageType{
        NETWORK_UNDEFINED_PACKAGE = 0,
        NETWORK_FILE_PACKAGE,
        NETWORK_JSON_PACKAGE,
        NETWORK_PACKAGE_TYPE_COUNT
    };

    //! static bytes in \a head
    virtual NetworkPackageType type() const {return NETWORK_UNDEFINED_PACKAGE;}
    virtual int reserve() const {return 0;}

    //! getter & setter
    QByteArray head() {return m_head;}
    void setHead(const QByteArray &bytes) {m_head = bytes;}
    QByteArray content() {return m_content;}
    void setContent(const QByteArray &bytes) {m_content = bytes;}
    bool has_frame_head() const {return m_has_frame_head;}
    int size() const {return m_head.length() + m_content.length();}

    enum NetworkPackageParseError {
        NETWORK_PACKAGE_PARSE_NO_ERROR,
        NETWORK_PACKAGE_PARSE_LENGTH_ERROR,
        NETWORK_PACKAGE_PARSE_TYPE_ERROR
    };
    //! return whether the bytes is complete for a network package, type is returned with \p type,
    //! parse error is returned with \p error
    virtual bool checkIntegrity(const QByteArray &bytes, NetworkPackageType &type, NetworkPackageParseError &error);

    //! process and combine \a head and \a content to generate the byte array
    virtual QByteArray serialize();
    //! save the head and content from binary data \p bytes
    virtual bool deserialize(const QByteArray &bytes);

protected:
    QByteArray m_head;
    QByteArray m_content;
    bool m_has_frame_head;
};

//!
//! \brief The NetworkFilePackage class saves data from a file into NetworkPackage
//!
//! \inherits NetworkPackage
//!
class CORESHARED_EXPORT NetworkFilePackage : public NetworkPackage
{
public:
    NetworkFilePackage(const QString &source_path = "", const QString &target_path = "", bool is_append = false);

    virtual NetworkPackageType type() const override {return NETWORK_FILE_PACKAGE;}

    QString sourcePath() {return m_source_path;}
    void setSourcePath(const QString &path) {m_source_path = path;}
    QString targetPath() {return m_target_path;}
    void setTargetPath(const QString &path) {m_target_path = path;}

    virtual QByteArray serialize() override;
    virtual bool deserialize(const QByteArray &bytes) override;

private:
    //! read the file and save into content
    bool loadFile();
    //! read the content and save into file
    bool saveFile();

private:
    QString m_source_path;
    QString m_target_path;
    bool m_is_append;
};

//!
//! \brief The NetworkJsonPackage class saves a JSON document into NetworkPackage
//!
//! \inherits NetworkPackage
//!
class CORESHARED_EXPORT NetworkJsonPackage : public NetworkPackage
{
public:
    NetworkJsonPackage();
    NetworkJsonPackage(const QJsonObject &json);
    NetworkJsonPackage(const QJsonArray &array);

    virtual NetworkPackageType type() const override {return NETWORK_JSON_PACKAGE;}

    bool isArray() {return m_is_array;}
    QJsonObject object() const {return m_object;}
    QJsonArray array() const {return m_array;}

    virtual QByteArray serialize() override;
    virtual bool deserialize(const QByteArray &bytes) override;

private:
    QJsonObject m_object;
    QJsonArray m_array;
    bool m_is_array;
};


struct CORESHARED_EXPORT NetworkChannel
{
    NetworkChannel(const QHostAddress &local_ip, int local_port, const QHostAddress &server_ip = QHostAddress::LocalHost, int server_port = 0) :
        localIP(local_ip), serverIP(server_ip),
        localPort(local_port), serverPort(server_port)
    {
    }

    NetworkChannel(int local_port, const QHostAddress &server_ip = QHostAddress::LocalHost, int server_port = 0) :
        localIP(QHostAddress::Any), serverIP(server_ip),
        localPort(local_port), serverPort(server_port)
    {
    }

public:
    QHostAddress localIP, serverIP;
    int localPort, serverPort;
};

#ifndef NETWORK_HANDLE_CACHE_TIMEOUT
#define NETWORK_HANDLE_CACHE_TIMEOUT 1000
#endif

//!
//! \brief The NetworkHandle class contains the time-consuming network operations, normally works in a unique thread
//!
class CORESHARED_EXPORT NetworkHandle : public QObject
{
    Q_OBJECT

public:
    //!
    //! \brief The NetworkType enum defines the supported socket type
    //!
    enum NetworkType {
        NETWORK_UDP,
        NETWORK_TCP,
        NETWORK_SSL
    };

    enum NetworkPackageType {
        NETWORK_PACKAGE_NONE,
        NETWORK_PACKAGE_RAW,
        NETWORK_PACKAGE_FRAME,
        NETWORK_PACKAGE_MIX
    };

    explicit NetworkHandle(NetworkType net_type, QAbstractSocket *socket, const NetworkChannel &channel, QObject *parent = nullptr);
    virtual ~NetworkHandle();

    //! internal socket can be directly visited, be careful for nullptr and multi thread operations.
    inline QAbstractSocket * socket() {return m_socket;}
    QUdpSocket * udpSocket();
    QTcpSocket * tcpSocket();
    QSslSocket * sslSocket();
    inline bool isValid()
    {
        switch (m_net_type) {
        case NETWORK_UDP: return udpSocket() != nullptr; break;
        case NETWORK_TCP: return tcpSocket() != nullptr; break;
        case NETWORK_SSL: return sslSocket() != nullptr; break;
        }
        return false;
    }
    inline bool isConnected() const {return m_is_connected;}

    NetworkChannel channel() const {return m_channel;}

    NetworkPackageType packageType() const;
    void setPackageType(NetworkPackageType package_type);

    //! restrict IP address of UDP sender
    inline void setUdpFilter(const QHostAddress &remote_address) {m_udp_remote_addr = remote_address;}
    inline QHostAddress udpFilter() const {return m_udp_remote_addr;}

public slots:
    //! To avoid cross thread operation, some socket methods must be run in socket thread
    void onTryConnectToHost(const QHostAddress &address, int port);
    void onTryDisconnectFromHost();

    //! recieve slots
    void onReceive();

    //! send slots
    void onSendRaw(const QByteArray &content, bool is_frame);
    void onSendFilePackage(const QString &local_path, const QString &remote_path, bool is_append);

signals:
    void socketError(const QString &error_str);
    void packetError(const QString &error_str);

    void packageSent();

    void rawObtained(const QByteArray &content);
    void packageObtained(const QByteArray &content);
    void fileObtained(const QString &path);
    void jsonObtained(const QJsonObject &json);
    void jsonArrayObtained(const QJsonArray &array);

private:
    void writeToSocket(const QByteArray &bytes);

private:
    NetworkType m_net_type;
    QAbstractSocket *m_socket;

    //state
    bool m_is_connected;

    //ip cache
    NetworkChannel m_channel;

    //receive cache
    QByteArray m_frame_cache;
    QByteArray m_package_cache;

    QTimer *m_cache_timer;

    //filter
    QHostAddress m_udp_remote_addr;

    //raw data
    bool m_enable_raw;
    bool m_enable_frame;
};

//!
//! \brief The NetworkObject class describes one single network connection in NetworkManager
//!
//! A NetworkObject consist of a NetworkHandle and simplified interface to reach the socket in handle,
//! the hanlde and its socket works in \a handle_thread.
//!
class CORESHARED_EXPORT NetworkObject : public QObject
{
    Q_OBJECT

public:
    explicit NetworkObject(NetworkHandle::NetworkType net_type, QThread *handle_thread, const NetworkChannel &channel,
                           QAbstractSocket *handle_socket = nullptr, QObject *parent = nullptr);
    virtual ~NetworkObject();

    NetworkHandle * handle() {return m_handle;}

    void connectToHost(const QHostAddress &address, int port) {emit tryConnectToHost(address, port);}
    void connectToHost(const QString &ip, int port) {return connectToHost(QHostAddress(ip), port);}
    void disconnectFromHost() {emit tryDisconnectFromHost();}

    void writeRaw(const QByteArray &content, bool is_frame = false) {emit sendRaw(content, is_frame);}
    void writePackage(const QByteArray &content, bool is_frame = true) {emit sendRaw(NetworkPackage(content).serialize(), is_frame);}
    void writeFilePackage(const QString &local_path, const QString &remote_path, bool is_append = false) {emit sendFilePackage(local_path, remote_path, is_append);}
    void writeJsonPackage(const QJsonObject &json, bool is_frame = true) {emit sendRaw(NetworkJsonPackage(json).serialize(), is_frame);}
    void writeJsonPackage(const QJsonArray &array, bool is_frame = true) {emit sendRaw(NetworkJsonPackage(array).serialize(), is_frame);}
signals:
    void tryConnectToHost(const QHostAddress &address, int port);
    void tryDisconnectFromHost();

    void sendRaw(const QByteArray &content, bool is_frame);
    void sendFilePackage(const QString &local_path, const QString &remote_path, bool is_append);

private:
    NetworkHandle *m_handle;
};

#ifndef NETWORK_MAX_THREAD_COUNT
#define NETWORK_MAX_THREAD_COUNT 3
#endif

//!
//! \brief The NetworkManager class controls all network access
//!
//! The main task is to control the socket working thread, and package data format, currently supports UDP, TCP, SSL
//! socket protocols, the inner package can be sent or analysed by whether package protocol or raw data.
//!
class CORESHARED_EXPORT NetworkManager : public QObject
{
    Q_OBJECT

public:
    explicit NetworkManager(QObject *parent = nullptr);
    ~NetworkManager();
    static NetworkManager & instance();

    //! the network object is constructed immediately after regist, and released by cancel.
    NetworkObject * regist(const QString &name, NetworkHandle::NetworkType type, const NetworkChannel &channel = NetworkChannel(0));
    NetworkObject * regist(const QString &name, NetworkHandle::NetworkType type, QAbstractSocket *socket);
    bool cancel(const QString &name);

    //! the main network interface is defined in NetworkObject
    //! \todo add permission control
    NetworkObject * netObject(const QString &name);

    QString findNetName(NetworkHandle *handle) const;

private:
    QThread * applyThread();
    void releaseThread(QThread *handle_thread);

private:
    QHash<QThread *, int> m_thread_load_hash;
    QHash<QString, NetworkObject *> m_objects;
};
}
//output methods
CORESHARED_EXPORT imol::NetworkManager & net();
CORESHARED_EXPORT imol::NetworkObject * net(const QString &name);

#endif // IMOL_NETWORKMANAGER_H
