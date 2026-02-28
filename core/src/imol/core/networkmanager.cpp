#include "core/networkmanager.h"

#include <QCoreApplication>
#include <QMetaEnum>
#include <QThread>
#include <QUdpSocket>
#include <QTcpSocket>
#include <QSslSocket>
#include <QTcpServer>
#include <QNetworkProxy>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QTimer>
#include <QtEndian>

#include "core/logmanager.h"

using namespace imol;

//! NetworkFrame
NetworkFrame::NetworkFrame(const QByteArray &content) :
    m_content(content),
    m_is_serialized(false)
{
    Q_ASSERT(sizeof(quint16) == 2);

    if (m_content.size() > maxSize()) WLOG << QObject::tr("frame size %1 exceeds %2!").arg(m_content.size()).arg(maxSize());
}

bool NetworkFrame::checkIntegrity(const QByteArray &bytes, NetworkFrameParseError &error)
{
    quint16 len = qFromBigEndian<quint16>(bytes.data());
    if (len > NETWORK_FRAME_MAX_SIZE) {
        error = NETWORK_FRAME_PARSE_SIZE_ERROR;
//        qDebug() << "[size error] len " << len << " > " << NETWORK_FRAME_MAX_SIZE;
        return true;
    }/* else if (bytes.size() < len * NETWORK_FRAME_MIN_PERCENT / 100) {
        error = NETWORK_FRAME_PARSE_BREAK_ERROR;
        qDebug() << "[break error] size " << bytes.size() << " < " << len * NETWORK_FRAME_MIN_PERCENT / 100;
        return true;
    }*/
    error = NETWORK_FRAME_PARSE_NO_ERROR;
    if (bytes.size() >= static_cast<int>(len + sizeof(quint16))) {
//        qDebug() << "---- frame correct, unhandle size:" << (bytes.size() - static_cast<int>(len + sizeof(quint16)));
        return true;
    }
//    qDebug() << "---- frame not finished, need more:" << static_cast<int>(len + sizeof(quint16)) - bytes.size();
    return false;
}

QByteArray NetworkFrame::serialize()
{
    if (m_is_serialized) return m_head + m_content;
    m_is_serialized = true;

    quint16 len = static_cast<quint16>(m_content.length());
    char len_ch[2];
    qToBigEndian(len, len_ch);
    m_head = QByteArray(len_ch, 2);

    return m_head + m_content;
}

bool NetworkFrame::deserialize(const QByteArray &bytes)
{
    quint16 len = qFromBigEndian<quint16>(bytes.data());
    if (len + 2 > bytes.size()) return false;

    m_head = bytes.left(2);
    m_content = bytes.mid(2, len);
    return true;
}

//! NetworkPackage
NetworkPackage::NetworkPackage(const QByteArray &content) :
    m_content(content),
    m_has_frame_head(true)
{
}

NetworkPackage::~NetworkPackage()
{
}

bool NetworkPackage::checkIntegrity(const QByteArray &bytes, NetworkPackageType &type, NetworkPackageParseError &error)
{
    type = NETWORK_UNDEFINED_PACKAGE;
    error = NETWORK_PACKAGE_PARSE_NO_ERROR;
    //check head size
    const int head_size = sizeof(quint32) + sizeof(quint8) + sizeof(quint8);
    if (bytes.size() < head_size) {
//        qDebug() << "---- package not finished, size " << bytes.size() << " < " << head_size;
        return false;
    }
    //check package size
    quint32 package_size = qFromBigEndian<quint32>(bytes.data());
    if (package_size > NETWORK_PACKAGE_MAX_SIZE) {
        error = NETWORK_PACKAGE_PARSE_LENGTH_ERROR;
//        qDebug() << "[parse length err] pkg size:" << package_size << " > " << NETWORK_PACKAGE_MAX_SIZE;
        return true;
    }
    if (bytes.size() < static_cast<int>(head_size + package_size)) {
//        qDebug() << "---- package not finished, size " << bytes.size() << " < " << static_cast<int>(head_size + package_size);
        return false;
    }
    //check package type
    quint8 package_type;
    memcpy(&package_type, bytes.data() + sizeof(quint32), sizeof(quint8));
    if (package_type >= NETWORK_PACKAGE_TYPE_COUNT) {
        error = NETWORK_PACKAGE_PARSE_TYPE_ERROR;
//        qDebug() << "[parse type err] pkg type:" << package_type << " >= " << NETWORK_PACKAGE_TYPE_COUNT;
        return true;
    }

    type = static_cast<NetworkPackageType>(package_type);
//    qDebug() << "---- package correct";
    return true;
}

QByteArray NetworkPackage::serialize()
{
    const int head_size = sizeof(quint32) + sizeof(quint8) + sizeof(quint8);

    quint32 package_size = m_content.size();
    quint8 package_type = static_cast<quint8>(type());
    quint8 package_reserve = reserve();

    char head_ch[head_size];
    qToBigEndian(package_size, head_ch);
    memcpy(head_ch + sizeof(quint32), &package_type, sizeof(quint8));
    memcpy(head_ch + sizeof(quint32) + sizeof(quint8), &package_reserve, sizeof(quint8));

    m_head = QByteArray(head_ch, head_size);

    return m_head + m_content;
}

bool NetworkPackage::deserialize(const QByteArray &bytes)
{
    //check head size
    const int head_size = sizeof(quint32) + sizeof(quint8) + sizeof(quint8);
    if (bytes.size() < head_size) return false;
    //check package size
    quint32 package_size = qFromBigEndian<quint32>(bytes.data());
    if (bytes.size() < static_cast<int>(head_size + package_size)) return false;

    m_head = bytes.left(head_size);
    m_content = bytes.mid(head_size, package_size);
    return true;
}

//! NetworkFilePackage
NetworkFilePackage::NetworkFilePackage(const QString &source_path, const QString &target_path, bool is_append) : NetworkPackage(),
    m_source_path(source_path),
    m_target_path(target_path),
    m_is_append(is_append)
{
    m_content.clear();
}

QByteArray NetworkFilePackage::serialize()
{
    return loadFile() ? NetworkPackage::serialize() : QByteArray();
}

bool NetworkFilePackage::deserialize(const QByteArray &bytes)
{
    return NetworkPackage::deserialize(bytes) ? saveFile() : false;
}

bool NetworkFilePackage::loadFile()
{
    QFile f(m_source_path);
    if (!f.open(QIODevice::ReadOnly)) {
        ELOG << QObject::tr("cannot open source file: ") << m_source_path;
        return false;
    }
    QByteArray file_content = f.readAll();
    f.close();
    //generate path
//    QString target_path = QFileInfo(m_target_path).canonicalFilePath();
//    QString cur_path = QFileInfo(QDir::current().absolutePath()).canonicalFilePath();
//    if (target_path.startsWith(cur_path)) target_path = "." + target_path.right(target_path.length() - cur_path.length());
//    target_path.replace(QDir::separator(), "/");

    //allocate
    m_content.resize(sizeof(quint16) * 2 + sizeof(quint8) + 16 + m_target_path.length() + file_content.size());
    //1 protocol id
    int offset = 0;
    quint16 protocol_id = 0;
    qToBigEndian(protocol_id, m_content.data() + offset);
    offset += sizeof(quint16);
    //2 write type
    quint8 write_type = m_is_append ? 1 : 0;
    memcpy(m_content.data() + offset, &write_type, sizeof(quint8));
    offset += sizeof(quint8);
    //3 md5
    QCryptographicHash f_md5(QCryptographicHash::Md5);
    f_md5.addData(file_content);
    QByteArray f_md5_bytes = f_md5.result();
    if (f_md5_bytes.size() != 16) f_md5_bytes.resize(16);
    memcpy(m_content.data() + offset, f_md5_bytes.data(), 16);
    offset += 16;
    //4 path len
    quint16 path_len = m_target_path.length();
    qToBigEndian(path_len, m_content.data() + offset);
    offset += sizeof(quint16);
    //5 path
    memcpy(m_content.data() + offset, m_target_path.toUtf8().data(), path_len);
    offset += path_len;
    //6 file
    memcpy(m_content.data() + offset, file_content.data(), file_content.size());

    return true;
}

bool NetworkFilePackage::saveFile()
{
    if (m_content.size() < sizeof(quint16) * 2 + sizeof(quint8) + 16) return false;

    //1 protocol id
    int offset = 0;
    quint16 protocol_id;
    memcpy(&protocol_id, m_content.data() + offset, sizeof(quint16));
    offset += sizeof(quint16);
    if (protocol_id != 0) {
        ELOG << QObject::tr("cannot resolve file package proto %1").arg(protocol_id);
        return false;
    }
    //2 write type
    memcpy(&m_is_append, m_content.data() + offset, sizeof(bool));
    offset += sizeof(bool);
    //3 md5
    QByteArray f_md5_bytes;
    f_md5_bytes.resize(16);
    memcpy(f_md5_bytes.data(), m_content.data() + offset, 16);
    offset += 16;
    //4 path len
    quint16 file_path_len = qFromBigEndian<quint16>(m_content.data() + offset);
    offset += sizeof(quint16);
    //5 path
    m_target_path = m_content.mid(offset, file_path_len);
    if (m_target_path.length() != file_path_len) {
        ELOG << QObject::tr("path resolves falied, len: %1 path: %2").arg(file_path_len).arg(m_target_path);
        return false;
    }
    m_source_path = m_target_path;
    offset += file_path_len;
    //6 file
    QFile f(m_target_path);
    if (!f.open(m_is_append ? QIODevice::Append : QIODevice::WriteOnly)) {
        ELOG << QObject::tr("cannot write file: ") << m_target_path;
        return false;
    }
    f.write(m_content.data() + offset, m_content.size() - offset);
    f.close();

    return true;
}

//! NetworkJsonPackage
NetworkJsonPackage::NetworkJsonPackage() : NetworkPackage(),
    m_is_array(false)
{
}

NetworkJsonPackage::NetworkJsonPackage(const QJsonObject &json) : NetworkPackage(),
    m_object(json),
    m_is_array(false)
{
}

NetworkJsonPackage::NetworkJsonPackage(const QJsonArray &array) : NetworkPackage(),
    m_array(array),
    m_is_array(true)
{
}

QByteArray NetworkJsonPackage::serialize()
{
    QJsonDocument json_doc = m_is_array ? QJsonDocument(m_array) : QJsonDocument(m_object);
    NetworkPackage::setContent(json_doc.toJson(QJsonDocument::Compact));

    return NetworkPackage::serialize();
}

bool NetworkJsonPackage::deserialize(const QByteArray &bytes)
{
    if (!NetworkPackage::deserialize(bytes)) return false;

    QJsonParseError parse_error;
    QJsonDocument json_doc = QJsonDocument::fromJson(m_content, &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        ELOG << QObject::tr("json parse error: ") << parse_error.errorString();
        return false;
    }
    m_is_array = json_doc.isArray();
    if (m_is_array) {
        m_array = json_doc.array();
    } else {
        m_object = json_doc.object();
    }

    return true;
}

//! NetworkHandle
NetworkHandle::NetworkHandle(NetworkType net_type, QAbstractSocket *socket, const NetworkChannel &channel, QObject *parent) : QObject(parent),
    m_net_type(net_type),
    m_socket(socket),
    m_is_connected(socket ? (socket->state() == QAbstractSocket::ConnectedState) : false),
    m_channel(channel),
    m_cache_timer(new QTimer(this)),
    m_enable_raw(false),
    m_enable_frame(true)
{
    if (m_socket) {
        m_socket->setParent(this);
    } else {
        //create new socket
        switch (net_type) {
        case NETWORK_UDP: {
            QUdpSocket *udp_socket = new QUdpSocket(this);
            udp_socket->setProxy(QNetworkProxy::NoProxy);
            if (channel.localPort > 0) {
                if (!channel.localIP.isBroadcast()) {
                    udp_socket->bind(channel.localIP, channel.localPort, QUdpSocket::ShareAddress);
                } else {
                    udp_socket->bind(QHostAddress::AnyIPv4, channel.localPort, QUdpSocket::ShareAddress);
                    udp_socket->joinMulticastGroup(channel.localIP);
                }
            }
            m_socket = udp_socket;
        }
            break;
        case NETWORK_TCP: {
            QTcpSocket *tcp_socket = new QTcpSocket(this);
            tcp_socket->setProxy(QNetworkProxy::NoProxy);
            if (channel.localPort > 0) tcp_socket->bind(channel.localIP, channel.localPort, QTcpSocket::ShareAddress);
            m_socket = tcp_socket;
        }
            break;
        case NETWORK_SSL: {
            QSslSocket *ssl_socket = new QSslSocket(this);
            if (channel.localPort > 0) ssl_socket->bind(channel.localIP, channel.localPort, QSslSocket::ShareAddress);
            m_socket = ssl_socket;
        }
            break;
        }
    }

    //process native signals
    connect(m_socket, &QAbstractSocket::readyRead, this, &NetworkHandle::onReceive);
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    connect(m_socket, &QAbstractSocket::errorOccurred, this,
#else
    connect(m_socket, static_cast<void (QAbstractSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error), this,
#endif
            [this] (QAbstractSocket::SocketError socket_error) {
        const QMetaObject &enum_obj = QAbstractSocket::staticMetaObject;
        QMetaEnum me_cmd_type = enum_obj.enumerator(enum_obj.indexOfEnumerator("SocketError"));
        emit socketError(QString(me_cmd_type.valueToKey(socket_error)));
    });
    connect(m_socket, &QAbstractSocket::connected, this, [this] {m_is_connected = true;});
    connect(m_socket, &QAbstractSocket::disconnected, this, [this] {m_is_connected = false;});
    //cache timer clear the cache
    connect(m_cache_timer, &QTimer::timeout, this, [this] {
        if (m_frame_cache.size() > 0) ELOG << tr("abondon frame cache, size: ") << m_frame_cache.size();
        if (m_package_cache.size() > 0) ELOG << tr("abondon package cache, size: ") << m_package_cache.size();
        m_frame_cache.clear();
        m_package_cache.clear();
        m_cache_timer->stop();
    });
}

NetworkHandle::~NetworkHandle()
{
    if (m_socket) m_socket->close();
}

QUdpSocket * NetworkHandle::udpSocket()
{
    return m_net_type == NETWORK_UDP ? qobject_cast<QUdpSocket *>(m_socket) : nullptr;
}

QTcpSocket * NetworkHandle::tcpSocket()
{
    return m_net_type == NETWORK_TCP ? qobject_cast<QTcpSocket *>(m_socket) : nullptr;
}

QSslSocket * NetworkHandle::sslSocket()
{
    return m_net_type == NETWORK_SSL ? qobject_cast<QSslSocket *>(m_socket) : nullptr;
}

NetworkHandle::NetworkPackageType NetworkHandle::packageType() const
{
    if (m_enable_raw) {
        return m_enable_frame ? NETWORK_PACKAGE_MIX : NETWORK_PACKAGE_RAW;
    } else {
        return m_enable_raw ? NETWORK_PACKAGE_MIX : NETWORK_PACKAGE_FRAME;
    }
    return NETWORK_PACKAGE_NONE;
}

void NetworkHandle::setPackageType(NetworkPackageType package_type)
{
    switch (package_type) {
    case NETWORK_PACKAGE_NONE:
        m_enable_raw = false;
        m_enable_frame = false;
        break;
    case NETWORK_PACKAGE_RAW:
        m_enable_raw = true;
        m_enable_frame = false;
        break;
    case NETWORK_PACKAGE_FRAME:
        m_enable_raw = false;
        m_enable_frame = true;
        break;
    case NETWORK_PACKAGE_MIX:
        m_enable_raw = true;
        m_enable_frame = true;
        break;
    default: break;
    }
}

void NetworkHandle::onTryConnectToHost(const QHostAddress &address, int port)
{
    socket()->abort();
    switch (m_net_type) {
//    case NETWORK_UDP: break;
    case NETWORK_SSL: sslSocket()->connectToHostEncrypted(address.toString(), port); break;
    default: socket()->connectToHost(address, port); break;
    }

    m_channel.serverIP = address;
    m_channel.serverPort = port;
}

void NetworkHandle::onTryDisconnectFromHost()
{
    socket()->disconnectFromHost();

    m_channel.serverIP = QHostAddress::LocalHost;
    m_channel.serverPort = 0;
}

void NetworkHandle::onReceive()
{
    QByteArray bytes;
    if (m_net_type == NETWORK_UDP) {
        QUdpSocket *udp_socket = qobject_cast<QUdpSocket *>(m_socket);
        while (udp_socket->hasPendingDatagrams()) {
            QByteArray pending_bytes;
            QHostAddress from_ip;
            quint16 from_port;
            pending_bytes.resize(udp_socket->pendingDatagramSize());
            udp_socket->readDatagram(pending_bytes.data(), pending_bytes.size(), &from_ip, &from_port);

            //udp filter
            if (!m_udp_remote_addr.isNull() && !from_ip.isEqual(m_udp_remote_addr, QHostAddress::ConvertV4MappedToIPv4)) return;

            bytes.append(pending_bytes);
        }
    } else {
        bytes = m_socket->readAll();

//        if (bytes.size() > 50) {
//            qDebug() << "rec" << bytes.size() << ":" << bytes.left(30).toHex(' ') << "..." << bytes.right(10).toHex(' ');
//        } else {
//            qDebug() << "rec" << bytes.size() << ":" << bytes.toHex(' ');
//        }
    }

    if (m_enable_raw) emit rawObtained(bytes);
    if (!m_enable_frame) return;

    m_frame_cache.append(bytes);

    bool frame_finished = true, package_finished = true;

    //process all frame cache
    while (m_frame_cache.size() > 0) {
        //check one net frame
        NetworkFrame::NetworkFrameParseError frame_parse_error;
        if (!(frame_finished = NetworkFrame().checkIntegrity(m_frame_cache, frame_parse_error))) {
            //frame not finished
            break;
        }
        if (frame_parse_error != NetworkFrame::NETWORK_FRAME_PARSE_NO_ERROR) {
            //frame error
            switch (frame_parse_error) {
            case NetworkFrame::NETWORK_FRAME_PARSE_SIZE_ERROR: ELOG << tr("frame size error, frame abondon"); break;
            case NetworkFrame::NETWORK_FRAME_PARSE_BREAK_ERROR: ELOG << tr("frame break error, frame abondon"); break;
            default: break;
            }
            //ignore all frame cache
            m_frame_cache.clear();
//            qDebug() << "---- ignore all frame cache";
            break;
        }

        NetworkFrame frame;
        frame.deserialize(m_frame_cache);
        m_frame_cache.remove(0, frame.size());
        m_package_cache.append(frame.content());

        //process all package cache
        while (m_package_cache.size() > 0) {
            //check one net package
            NetworkPackage::NetworkPackageType net_pkg_type;
            NetworkPackage::NetworkPackageParseError net_pkg_parse_error;
            if (!(package_finished = NetworkPackage().checkIntegrity(m_package_cache, net_pkg_type, net_pkg_parse_error))) {
                //package not finished
                break;
            }
            if (net_pkg_parse_error != NetworkPackage::NETWORK_PACKAGE_PARSE_NO_ERROR) {
                //package error
                switch (net_pkg_parse_error) {
                case NetworkPackage::NETWORK_PACKAGE_PARSE_LENGTH_ERROR: emit packetError("package length error, package abondon"); break;
                case NetworkPackage::NETWORK_PACKAGE_PARSE_TYPE_ERROR: emit packetError("package type error, package abondon"); break;
                default: break;
                }
                //ignore current frame
                m_package_cache.clear();
                break;
            }

            //handle a correct package
            switch (net_pkg_type) {
            case NetworkPackage::NETWORK_UNDEFINED_PACKAGE: {
                NetworkPackage net_pkg;
                if (net_pkg.deserialize(m_package_cache)) emit packageObtained(net_pkg.content());
                m_package_cache.remove(0, net_pkg.size());
            }
                break;
            case NetworkPackage::NETWORK_FILE_PACKAGE: {
                NetworkFilePackage net_pkg;
                if (net_pkg.deserialize(m_package_cache)) emit fileObtained(net_pkg.targetPath());
                m_package_cache.remove(0, net_pkg.size());
            }
                break;
            case NetworkPackage::NETWORK_JSON_PACKAGE: {
                NetworkJsonPackage net_pkg;
                if (net_pkg.deserialize(m_package_cache)) net_pkg.isArray() ? emit jsonArrayObtained(net_pkg.array()) : emit jsonObtained(net_pkg.object());
                m_package_cache.remove(0, net_pkg.size());
            }
                break;
            default: break;
            }

//            qDebug() << "handle package type:" << net_pkg_type << ", reset package cache size:" << m_package_cache.size();
        }
    }

    if (!frame_finished || !package_finished) m_cache_timer->start(NETWORK_HANDLE_CACHE_TIMEOUT);
}

void NetworkHandle::onSendRaw(const QByteArray &content, bool is_frame)
{
    if (is_frame) {
        //send with frame
        for (int i = 0; i < content.size(); i += NetworkFrame::maxSize()) {
            writeToSocket(NetworkFrame(content.mid(i, NetworkFrame::maxSize())).serialize());
        }
    } else {
        //send directly without frame
        for (int i = 0; i < content.size(); i += NetworkFrame::maxSize()) {
            writeToSocket(content.mid(i, NetworkFrame::maxSize()));
        }
    }

    emit packageSent();
}

void NetworkHandle::onSendFilePackage(const QString &local_path, const QString &remote_path, bool is_append)
{
    NetworkFilePackage file_pkg(local_path, remote_path, is_append);
    this->onSendRaw(file_pkg.serialize(), false);
}

void NetworkHandle::writeToSocket(const QByteArray &bytes)
{
    switch (m_net_type) {
    case NETWORK_UDP:
        if (!udpSocket()->isOpen()) {
            udpSocket()->writeDatagram(bytes, m_channel.serverIP, m_channel.serverPort);
        } else {
            m_socket->write(bytes);
        }
        break;
    default:
        m_socket->write(bytes);
        break;
    }
    m_socket->flush();
    m_socket->waitForBytesWritten();
}

//! NetworkObject
NetworkObject::NetworkObject(NetworkHandle::NetworkType net_type, QThread *handle_thread, const NetworkChannel &channel,
                             QAbstractSocket *handle_socket, QObject *parent) : QObject(parent),
    m_handle(new NetworkHandle(net_type, handle_socket, channel))
{
    QThread *target_handle_thread = (handle_thread == nullptr ? QCoreApplication::instance()->thread() : handle_thread);
    m_handle->moveToThread(target_handle_thread);

    connect(this, &NetworkObject::tryConnectToHost, m_handle, &NetworkHandle::onTryConnectToHost);
    connect(this, &NetworkObject::tryDisconnectFromHost, m_handle, &NetworkHandle::onTryDisconnectFromHost);

    connect(this, &NetworkObject::sendRaw, m_handle, &NetworkHandle::onSendRaw);
    connect(this, &NetworkObject::sendFilePackage, m_handle, &NetworkHandle::onSendFilePackage);
}

NetworkObject::~NetworkObject()
{
    m_handle->deleteLater();
}

//! NetworkManager
NetworkManager::NetworkManager(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<QHostAddress>("QHostAddress");
    qRegisterMetaType<QAbstractSocket::SocketError>("QAbstractSocket::SocketError");

    //start all threads
    for (int i = 0; i < NETWORK_MAX_THREAD_COUNT; ++i) {
        QThread *handle_thread = new QThread(this);
        handle_thread->start();
        m_thread_load_hash.insert(handle_thread, 0);
    }
}

NetworkManager::~NetworkManager()
{
    //delete all objs
    qDeleteAll(m_objects);
    //stop all threads
    foreach (QThread *handler_thread, m_thread_load_hash.keys()) {
        handler_thread->exit();
        handler_thread->wait();
//        delete handler_thread;
        handler_thread->deleteLater();
    }
}

NetworkManager & NetworkManager::instance()
{
    static NetworkManager manager;
    return manager;
}

NetworkObject * NetworkManager::regist(const QString &name, NetworkHandle::NetworkType type, const NetworkChannel &channel)
{
    if (!m_objects.contains(name)) m_objects.insert(name, new NetworkObject(type, applyThread(), channel, nullptr, this));
    return m_objects.value(name);
}

NetworkObject * NetworkManager::regist(const QString &name, NetworkHandle::NetworkType type, QAbstractSocket *socket)
{
    if (!m_objects.contains(name)) m_objects.insert(name, new NetworkObject(type, applyThread(), NetworkChannel(0), socket, this));
    return m_objects.value(name);
}

bool NetworkManager::cancel(const QString &name)
{
    NetworkObject *obj = m_objects.value(name, nullptr);
    if (!obj) return false;
    if (obj->handle()) releaseThread(obj->handle()->thread());
    m_objects.remove(name);
    delete obj;
    return true;
}

NetworkObject * NetworkManager::netObject(const QString &name)
{
    return m_objects.value(name, nullptr);
}

QString NetworkManager::findNetName(NetworkHandle *handle) const
{
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); it++) {
        if (it.value()->handle() == handle) return it.key();
    }
    return "";
}

QThread * NetworkManager::applyThread()
{
    QThread *target_handle_thread = nullptr;
    int min_load = 0x0fffffff;
    foreach (QThread *handler_thread, m_thread_load_hash.keys()) {
        int load = m_thread_load_hash.value(handler_thread);
        if (load < min_load) {
            min_load = load;
            target_handle_thread = handler_thread;
        }
    }
    m_thread_load_hash[target_handle_thread]++;

    return target_handle_thread;
}

void NetworkManager::releaseThread(QThread *handle_thread)
{
    if (m_thread_load_hash.contains(handle_thread)) m_thread_load_hash[handle_thread]--;
}
//output methods
NetworkManager & net()
{
    return NetworkManager::instance();
}

NetworkObject * net(const QString &name)
{
    return net().netObject(name);
}
