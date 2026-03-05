#include "xservice_client.h"

#include "imol/networkmanager.h"
#include "imol/commandmanager.h"
#include "imol/logmanager.h"

#include <QEventLoop>
#include <QCoreApplication>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QElapsedTimer>
#include <QUuid>
#include <QThread>
#include <QWaitCondition>
#include <QMutex>

#define NAME "xservice"

#define DEFAULT_TIMEOUT 800

VE_REGISTER_MODULE(ve.xervice, XService)
VE_REGISTER_VERSION(ve.xservice, 12)

constexpr const char *XSERVICE_ID_ALL        = "@all";
constexpr const char *XSERVICE_ID_HANDSHAKE  = "@handshake";

namespace ix {

Node* createNodeAt(Node* node, const QString& path)
{
    if (path.isEmpty()) return node;
    auto n = node->r(path);
    if (n->isEmptyMobj()) {
        node->insert(node, path);
        n = node->r(path);
    }
    return n;
}

}

using namespace imol;

/**
 * @brief The Package struct
 */
struct Package
{
public:
    ix::Node n;
    ix::Node *ref;

public:
    Package(ix::Node *ref, const QString &id) : n("pkg"), ref(ref) { n.set(nullptr, "id", id); }

    ix::Node * cmdNode(const QString &cmd)
    {
        return ix::createNodeAt(&n, cmd);
    }

    ix::Node * keyNode(const QString &cmd, const QString &key)
    {
        ix::Node *tar = cmdNode(cmd);
        tar = tar->hasCmobj(key, true) ? tar->c(key, true) : tar->append(ref, key);
        return tar;
    }

    ix::Node * subKeyNode(const QString &cmd, const QString &key, const QString &sub_key)
    {
        ix::Node *tar = keyNode(cmd, key);
        tar = tar->hasCmobj(sub_key) ? tar->c(sub_key, true) : tar->append(ref, sub_key);
        return tar;
    }

    Package & append(const QString &cmd, const QString &key, const QString &sub_key = "", ix::Node *param_n = nullptr)
    {
        ix::Node *tar = sub_key.isEmpty() ? keyNode(cmd, key) : subKeyNode(cmd, key, sub_key);
        if (param_n && !param_n->isEmptyMobj()) tar->copyFrom(nullptr, param_n);
        return *this;
    }

    Package & scan(ix::Node *tar, int level/*, ix::Node *proto = nullptr*/)
    {
        if (level < 1) {
            if (tar != ref) tar->copyFrom(ref, ref);
            return *this;
        }
        QList<ix::Node *> refs = ref->cmobjs();
        for (int i = 1; i < level; i++) {
            QList<ix::Node *> tmps;
            foreach (ix::Node *tmp, refs) {
                tmps.append(tmp->cmobjs());
            }
            refs = tmps;
        }
        foreach (ix::Node *tmp, refs) {
            QString key = tmp->fullName(ref);
//            if (proto && !proto->hasRmobj(key)) continue;
            auto key_n = tar->hasCmobj(key) ? tar->c(key, true) : tar->append(ref, key);
            key_n->copyFrom(ref, tmp);
        }
        return *this;
    }

    QByteArray serialize() const
    {
        return QJsonDocument(n.exportToJson().toObject()).toJson(QJsonDocument::Compact) + '\r';
    }

    bool deserialize(const QByteArray &bytes)
    {
        QJsonParseError err;
        QJsonObject json = QJsonDocument::fromJson(bytes, &err).object();
        if (err.error != QJsonParseError::NoError) {
            ELOG << "<xservice> deserialize invalid json with error: " << err.errorString() << ", raw: " << bytes;
            return false;
        }
        n.importFromJson(nullptr, json);
        return true;
    }
};

/**
 * @brief The XServiceData struct
 */
struct XService::Private
{
    XService *ref;

public:
//    MobjHandler *handler;

    imol::NetworkObject *nobj;
    QByteArray cache;

    int timeout;
    long index;
    QHash<QString, ix::Node *> pending;
    QMutex mutex;
    QWaitCondition cond;

    ix::Node *is_connected_n;
    ix::Node *pn;
    ix::Node *pdn;
    ix::Node *pcn;

    ix::Node *debug_node;

public:
    Private(XService *ref) : ref(ref)/*, handler(new MobjHandler)*/, nobj(nullptr), timeout(DEFAULT_TIMEOUT), index(0l),
        is_connected_n(nullptr), pn(nullptr), pdn(nullptr), pcn(nullptr), debug_node(m().nullData()) {}
    ~Private() { /*delete handler;*/ }

    QString genId(const QString &cmd) { return QString("#%1%2").arg(cmd).arg(index++); }

public:
    void prune(QObject *context, ix::Node *n, int level = 0);
    ix::Node * send(QObject *context, const Package &pkg, bool force = false);
    bool recv(const Package &pkg);

private:
    void handleImol(ix::Node *n);
};

void XService::Private::prune(QObject *context, ix::Node *n, int level)
{
    if (level <= 0) {
        n->quiet(true);
        n->clear(context);
        n->quiet(false);
    } else {
        foreach (auto leaf, n->cmobjs()) {
            prune(context, leaf, level - 1);
        }
    }
}

ix::Node * XService::Private::send(QObject *context, const Package &pkg, bool force)
{
    // connection control
    if (!force && !is_connected_n->getBool()) {
//        emit ref->support(pkg.ref, &pkg.n);
        return pkg.ref;
    }

    // duplicate control
    QString id = pkg.n.c("id")->getString();
    if (pending.contains(id)) {
        WLOG << "<xservice> duplicated: " << id;
        return pkg.ref->set(context, XService::Duplicate);
    }

    // locker
    QMutexLocker locker(&mutex);

    pending.insert(id, pkg.ref);

    // send raw
    if (debug_node->getBool()) DLOG << "send(" << QThread::currentThreadId() << "): " << QJsonDocument(pkg.n.exportToJson().toObject()).toJson(QJsonDocument::Compact);
    nobj->writeRaw(pkg.serialize());

    // sync timeout control
    if (cond.wait(&mutex, timeout)) {
        if (!pending.contains(id)) return pkg.ref;
    } else {
        WLOG << "<xservice> timeout: " << id << " will force disconnect";
        if (nobj->handle()->socket()) nobj->disconnectFromHost();
        pending.remove(id);
        return pkg.ref->set(context, XService::Timeout);
    }

    // async timeout control
    static int async_max_timeout = 10 * 60 * 1000;
    QElapsedTimer exec_timer;
    exec_timer.start();
    while (!exec_timer.hasExpired(async_max_timeout) && nobj->handle()->isConnected()) {
        cond.wait(&mutex, timeout);
        if (!pending.contains(id)) return pkg.ref;
    }

    pending.remove(id);
    return pkg.ref->set(context, XService::Timeout);
}

bool XService::Private::recv(const Package &pkg)
{
    QString id = pkg.n.c("id")->getString();
    bool is_async = id.startsWith('#');
    if (!is_async && pkg.n.cmobjCount() == 1 && pkg.n.hasCmobj("id", true)) {
        cond.wakeAll();
        return false;
    }

    // lock
    mutex.lock();

    ix::Node *ref = pending.take(id);

    // data
    ix::Node *dst = ref ? ref : pdn;
    ix::Node *src = nullptr;

    foreach (const QString &dk, QStringList() << "g" << "s") {
        if (!pkg.n.hasCmobj(dk)) continue;
        src = (id == XSERVICE_ID_ALL || is_async) ? pkg.n.c(dk) : pkg.n.c(dk)->r(id);
        dst->copyFrom(ref, src);
    }
    if (pkg.n.hasCmobj("n")) {
        QString nk = id.mid(1);
        dst->r(nk)->copyFrom(ref, pkg.n.c("n")->r(nk), true, true); // auto prune
    }

    // command
    if (pkg.n.hasCmobj("c")) {
        dst = ref ? ref : pcn;
        src = (id == XSERVICE_ID_ALL || is_async) ? pkg.n.c("c") : pkg.n.c("c")->r(id);
        dst->copyFrom(ref, src);
    }

    // internal
    if (pkg.n.hasCmobj("imol")) pn->c("imol")->copyFrom(pn, pkg.n.c("imol"), true, true);

    mutex.unlock();
    cond.wakeAll();
    return true;
}

/**
 * @brief The XServiceConnectCommand class
 */
class XServiceConnectCommand : public imol::BaseCommand
{
public:
    explicit XServiceConnectCommand(QObject *parent = nullptr) : imol::BaseCommand("xservice.connect", true, parent) {}

    QString usage() const override { return tr("xservice.connect [<ip>:<port>]"); }
    QString instruction() const override { return tr("Try to connect to xservice server"); }

protected:
    void run(ix::Node *n, const QString &param) override
    {
        Q_UNUSED(n)

        if (!param.isEmpty()) {
            QString ip = param.section(":", 0, 0);
            int port = param.section(":", -1).toInt();
            if (ip.isEmpty() || port == 0) {
                emit error(tr("invalid ip or port specified"));
                return;
            }
            m("xservice.host")->set(this, "ip", ip)->set(this, "port", port);
        }
        emit output(tr("connect to %1:%2").arg(m("xservice.host.ip")->getString()).arg(m("xservice.host.port")->getInt()));
        m("xservice.trigger.connect")->trigger();
    }
};

/**
 * @brief The XServiceDisconnectCommand class
 */
class XServiceDisconnectCommand : public imol::BaseCommand
{
public:
    explicit XServiceDisconnectCommand(QObject *parent = nullptr) : imol::BaseCommand("xservice.disconnect", true, parent) {}

    QString usage() const override { return tr("xservice.disconnect"); }
    QString instruction() const override { return tr("Try to disconnect from xservice server"); }

protected:
    void run(ix::Node *n, const QString &param) override
    {
        Q_UNUSED(n)
        Q_UNUSED(param)

        if (!m("xservice.connected")->getBool()) {
            emit error(tr("not connected"));
            return;
        }
        emit output(tr("disconnect from %1:%2").arg(m("xservice.host.ip")->getString()).arg(m("xservice.host.port")->getInt()));
        m("xservice.trigger.disconnect")->trigger();
    }
};

/**
 * @brief The XServiceCommand class
 */
class XServiceCommand : public imol::BaseCommand
{
public:
    const QStringList full_cmds;
    const QStringList params;

public:
    explicit XServiceCommand(QObject *parent = nullptr) : imol::BaseCommand("xservice", true, parent),
        full_cmds(QStringList() << "get" << "set" << "command" << "watch"),
        params(QStringList() << "-async" << "-ref" << "-data =") {}

    QString usage() const override { return tr("xservice <cmd> <key> [-async] [-ref | -data = <json>]"); }
    QString instruction() const override
    {
        return tr("Run a command (get, set, command, watch) from the remote host");
    }

protected:
    void complete(ix::Node *n, const QString &text) override;
    void hint(ix::Node *n, const QString &text) override;
    void run(ix::Node *n, const QString &param) override;

private:
    bool fill(const QString &half, const QStringList &candidates);
    bool fill(const QString &half, ix::Node *candidate_node);
};

void XServiceCommand::complete(ix::Node *, const QString &text)
{
    if (text.endsWith(IMOL_MODULE_NAME_SEPARATOR)) return;

    int cnt = text.count(' ');
    if (cnt == 0) {
        fill(text, full_cmds);
    } else if (cnt == 1) {
        QString key = text.section(' ', -1);
        fill(key, text.startsWith("c") ? xservice()->commandNode() : xservice()->dataNode());
    } else if (text.length() < 128) {
        QString last = text.section(' ', -1);
        if (last.length() < 7) fill(last, params);
    }
}

void XServiceCommand::hint(ix::Node *, const QString &text)
{
    int cnt = text.count(' ');
    if (cnt != 1) return;

    QString key = text.section(' ', -1);
    ix::Node *pn = text.startsWith("c") ? xservice()->commandNode() : xservice()->dataNode();
    QString part = key.section(IMOL_MODULE_NAME_SEPARATOR, 0, -2);
    QString last = key.section(IMOL_MODULE_NAME_SEPARATOR, -1);
    QStringList candidates;
    foreach (ix::Node *item_node, (part.isEmpty() ? pn : pn->r(part))->cmobjs()) {
        if (item_node->rname().startsWith(last)) candidates << item_node->rname();
    }
    if (!candidates.isEmpty()) emit output(candidates.join("\t") + "\n");
}

void XServiceCommand::run(ix::Node *n, const QString &param)
{
    bool is_async = param.contains(" -async");
    bool use_ref = param.contains(" -ref");
    bool has_data = param.contains(" -data");

    QString cmd = param.section(' ', 0, 0);
    if (cmd != "imol" && !full_cmds.contains(cmd)) {
        emit error(tr("unknown cmd '%1', candidates are: %2").arg(cmd, full_cmds.join(",")));
        return;
    }

    if (is_async && cmd != "command") {
        emit error(tr("asynchronous only valid for command"));
        return;
    } else if (use_ref && has_data) {
        emit error(tr("conflict parameters -ref and -data"));
        return;
    }

    QString full_key = param.section(' ', 1, 1);
    QString key, sub_key;
    foreach (ix::Node *in, xservice()->mobj()->r(cmd.startsWith("c") ? "proto.imol.command" : "proto.imol.data")->cmobjs()) {
        if (full_key.startsWith(in->getString())) {
            key = in->getString();
            if (key.length() < full_key.length()) sub_key = full_key.right(full_key.length() - key.length() - 1);
            break;
        }
    }

    if (key.isEmpty()) {
        emit error(tr("invalid key: %1").arg(full_key));
        return;
    }

    ix::Node *param_node = nullptr;
    if (has_data) {
        param_node = new ix::Node("tmp");
        QString data_str = param.section("-data = ", -1);
        if (param.endsWith('}')) {
            param_node->importFromJson(this, QJsonDocument::fromJson(data_str.toUtf8()).object());
        } else if (param.endsWith(']')) {
            param_node->importFromJson(this, QJsonDocument::fromJson(data_str.toUtf8()).array());
        } else {
            QVariant var = data_str;
            if (var.canConvert<bool>()) {
                param_node->set(this, var.toBool());
            } else if (var.canConvert<int>()) {
                param_node->set(this, var.toInt());
            } else if (var.canConvert<double>()) {
                param_node->set(this, var.toDouble());
            }
        }
    }

    ix::Node *ret = nullptr;
    if (cmd == "get") {
        ret = use_ref ? xservice()->getAt(this, key, sub_key, n) : xservice()->get(this, key, sub_key);
    } else if (cmd == "set") {
        ret = use_ref ? xservice()->setAt(this, key, sub_key, n) : xservice()->set(this, key, sub_key, param_node);
    } else if (cmd == "command") {
        if (is_async) {
            xservice()->command(this, key, param_node);
        } else if (use_ref) {
            ret = xservice()->execCommandAt(this, key, n);
        } else {
            ret = xservice()->execCommand(this, key, param_node);
        }
    } else if (cmd == "watch") {
        ret = xservice()->watch(this, key, param_node ? param_node->getBool() : true);
    }

    if (ret && !use_ref) {
        QJsonValue value = ret->exportToJson();
        if (value.isObject()) {
            emit output(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
        } else if (value.isArray()) {
            emit output(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
        } else {
            emit output(ret->getString());
        }
    }

    delete param_node;
}

bool XServiceCommand::fill(const QString &half, const QStringList &candidates)
{
    if (half.isEmpty()) return true;
    if (half.endsWith(' ')) return false;
    foreach (const QString &candidate, candidates) {
        if (candidate.startsWith(half)) {
            emit input(QString("%1 ").arg(candidate.right(candidate.length() - half.length())));
            return true;
        }
    }
    return false;
}

bool XServiceCommand::fill(const QString &half, ix::Node *candidate_node)
{
    if (half.isEmpty()) return true;
    if (half.endsWith(' ')) return false;
    QString last;
    ix::Node *ref = candidate_node;
    if (half.contains(IMOL_MODULE_NAME_SEPARATOR)) {
        last = half.section(IMOL_MODULE_NAME_SEPARATOR, -1);
        ref = ref->r(half.section(IMOL_MODULE_NAME_SEPARATOR, 0, -2));
    } else {
        last = half;
    }
    foreach (ix::Node *cn, ref->cmobjs()) {
        QString rname = cn->rname();
        if (rname.startsWith(last)) {
            emit input(rname.right(rname.length() - last.length()));
            return true;
        }
    }
    return false;
}

XService* g_xservice_instance = nullptr;

XService::XService(QObject *parent) : Module(), _p(new Private(this))
{
//    bindMobjJson(NAME, ix::templatePath(NAME));

    mobj()->set(nullptr, "connected", false);
    mobj()->set(nullptr, "timeout", 1600);
    mobj()->set(nullptr, "host.ip", "127.0.0.1");
    mobj()->set(nullptr, "host.port", 5061);
    mobj()->set(nullptr, "trigger.connect", QVariant())->set(nullptr, "trigger.disconnect", QVariant());
    mobj()->set(nullptr, "proto.imol.version", QVariant())->set(nullptr, "proto.imol.data", QVariant())->set(nullptr, "proto.imol.command", QVariant())
            ->set(nullptr, "proto.data", QVariant())->set(nullptr, "proto.command", QVariant());

    mobj()->set(nullptr, "debug", true);

    _p->is_connected_n = mobj()->c("connected");
    _p->pn = mobj()->c("proto");
    _p->pdn = mobj()->r("proto.data");
    _p->pcn = mobj()->r("proto.command");
    _p->debug_node = mobj()->c("debug")->set(nullptr, m("ve.config.debug")->get());

    g_xservice_instance = this;
}

XService::~XService()
{
    net().cancel(NAME);
    delete _p;
}

ix::Node* XService::mobj() const { return imol::m("ve.xservice"); }

ix::Node * XService::dataNode(const QString &key) { return key.isEmpty() ? _p->pdn : ix::createNodeAt(_p->pdn, key); }
ix::Node * XService::commandNode(const QString &key) { return key.isEmpty() ? _p->pcn : ix::createNodeAt(_p->pcn, key); }

imol::ModuleObject * XService::get(QObject *context, const QString &key)
{
    ix::Node *dn = ix::createNodeAt(_p->pdn, key);
    _p->prune(context, dn);
    _p->send(context, Package(dn, key).append("g", key));
    return dn;
}

ix::Node * XService::get(QObject *context, const QString &key, const QString &sub_key)
{
    if (sub_key.isEmpty()) return get(context, key);

    QString id = mName(key, sub_key);
    ix::Node *sdn = ix::createNodeAt(_p->pdn, id);
    _p->prune(context, sdn);
    _p->send(context, Package(sdn, id).append("g", key, sub_key));
    return sdn;
}

ix::Node * XService::set(QObject *context, const QString &key, ix::Node *param)
{
    ix::Node *dn = ix::createNodeAt(_p->pdn, key);
    _p->prune(context, dn);
    _p->send(context, Package(dn, key).append("s", key, "", param));
    return dn;
}

ix::Node * XService::set(QObject *context, const QString &key, const QString &sub_key, ix::Node *param)
{
    if (sub_key.isEmpty()) return set(context, key, param);

    QString id = mName(key, sub_key);
    ix::Node *sdn = ix::createNodeAt(_p->pdn, id);
    _p->prune(context, sdn);
    _p->send(context, Package(sdn, id).append("s", key, sub_key, param));
    return sdn;
}

ix::Node * XService::set(QObject *context, const QString &key, const QString &sub_key, const QVariant &var)
{
    ix::Node pn("param");
    pn.importFromVariant(context, var);
    return set(context, key, sub_key, &pn);
}

ix::Node * XService::watch(QObject *context, const QString &key, bool watching)
{
    ix::Node *dn = ix::createNodeAt(_p->pdn, key);
    // no prune
    _p->send(context, Package(dn, "#" + key).append("w", key, "", ix::Node("param").set(mobj(), watching)));
    return dn;
}

ix::Node * XService::command(QObject *context, const QString &key, ix::Node *param)
{
    ix::Node *cn = ix::createNodeAt(_p->pcn, key);
    _p->prune(context, cn);
    _p->send(context, Package(cn, _p->genId("c")).append("c", key, "", param));
    return cn;
}

ix::Node * XService::command(QObject *context, const QString &key, const QVariant &var)
{
    ix::Node pn("param");
    pn.importFromVariant(context, var);
    return command(context, key, &pn);
}

ix::Node * XService::execCommand(QObject *context, const QString &key, ix::Node *param)
{
    ix::Node *cn = ix::createNodeAt(_p->pcn, key);
    _p->prune(context, cn);
    _p->send(context, Package(cn, key).append("c", key, "", param));
    return cn;
}

ix::Node * XService::execCommand(QObject *context, const QString &key, const QVariant &var)
{
    ix::Node pn("param");
    pn.importFromVariant(context, var);
    return execCommand(context, key, &pn);
}

ix::Node * XService::execImol(QObject *context, const QString &key, imol::ModuleObject *param)
{
    ix::Node *in = ix::createNodeAt(_p->pn->c("imol"), key);
    _p->send(context, Package(in, "internal").append("imol", key, "", param));
    return in;
}

ix::Node * XService::getAt(QObject *context, ix::Node *ref, int search_level)
{
    _p->prune(context, ref, search_level);
    Package pkg(ref, XSERVICE_ID_ALL);
    _p->send(context, pkg.scan(pkg.cmdNode("g"), search_level));
    return ref;
}

ix::Node * XService::getAt(QObject *context, const QString &key, ix::Node *ref, int search_level)
{
    _p->prune(context, ref, search_level);
    Package pkg(ref, key);
    _p->send(context, pkg.scan(pkg.keyNode("g", key), search_level));
    return ref;
}

ix::Node * XService::getAt(QObject *context, const QString &key, const QString &sub_key, ix::Node *ref)
{
    if (sub_key.isEmpty()) return getAt(context, key, ref, 0);
    _p->prune(context, ref);
    _p->send(context, Package(ref, mName(key, sub_key)).append("g", key, sub_key));
    return ref;
}

ix::Node * XService::setAt(QObject *context, const QString &key, ix::Node *ref, int search_level)
{
    Package pkg(ref, key);
    pkg.scan(pkg.keyNode("s", key), search_level);
    _p->prune(context, ref, search_level);
    _p->send(context, pkg);
    return ref;
}

ix::Node * XService::setAt(QObject *context, const QString &key, const QString &sub_key, ix::Node *ref)
{
    Package pkg(ref, key);
    pkg.append("s", key, sub_key, ref);
    _p->prune(context, ref);
    _p->send(context, pkg);
    return ref;
}

ix::Node * XService::execCommandAt(QObject *context, const QString &key, ix::Node *ref)
{
    Package pkg(ref, key);
    pkg.append("c", key, "", ref);
    _p->prune(context, ref);
    _p->send(context, pkg);
    return ref;
}

void XService::init()
{
    ::command().regist(new XServiceCommand(mobj()));
    ::command().regist(new XServiceConnectCommand(mobj()));
    ::command().regist(new XServiceDisconnectCommand(mobj()));

    _p->nobj = net().regist(NAME, imol::NetworkHandle::NETWORK_TCP);
    _p->nobj->handle()->setPackageType(imol::NetworkHandle::NETWORK_PACKAGE_RAW);
    QObject::connect(_p->nobj->handle()->socket(), &QAbstractSocket::connected, mobj(), [this] { onConnected(); });
    QObject::connect(_p->nobj->handle()->socket(), &QAbstractSocket::disconnected, mobj(), [this] { onDisconnected(); });
    QObject::connect(_p->nobj->handle(), &NetworkHandle::rawObtained, mobj(), [this] (const QByteArray& ba) { onReceived(ba); }, Qt::DirectConnection);

    QObject::connect(mobj()->c("timeout"), &ve::Data::changed, mobj(), [this] (const QVariant& var) { _p->timeout = var.toInt(); });
    QObject::connect(mobj()->r("trigger.connect"), &ve::Data::changed, mobj(), [this] {
        if (_p->nobj) _p->nobj->connectToHost(mobj()->r("host.ip")->getString(), mobj()->r("host.port")->getInt());
    });
    QObject::connect(mobj()->r("trigger.disconnect"), &ve::Data::changed, mobj(), [this] {
        if (_p->nobj) _p->nobj->disconnectFromHost();
    });
}

void XService::idle()
{

}

void XService::detach()
{
    _p->nobj = nullptr;
    net().cancel(NAME);
}

void XService::onConnected()
{
    ix::Node *in = _p->pn->c("imol");

    // handshake
    _p->prune(mobj(), in, 1);
    Package pkg(in, XSERVICE_ID_HANDSHAKE);
    _p->send(mobj(), pkg.scan(pkg.cmdNode("imol"), 1), true);

    // auto proto
    QStringList iks = in->cmobjNames();
    foreach (auto ik, iks) {
        if (!_p->pn->hasCmobj(ik)) continue;
        foreach (auto ikn, in->c(ik)->cmobjs()) {
            _p->pn->c(ik)->insert(mobj(), ikn->getString());
        }
    }

    ILOG << "<xservice> connected";
    _p->is_connected_n->set(mobj(), true);
}

void XService::onDisconnected()
{
    if (!_p->is_connected_n->getBool()) return;

    ILOG << "<xservice> disconnected";
    _p->is_connected_n->set(mobj(), false);
}

void XService::onReceived(const QByteArray &bytes)
{
    // net thread!
    QList<QByteArray> fragments = (_p->cache + bytes).split('\r');
    if (!bytes.endsWith('\r')) {
        _p->cache = fragments.takeLast();
    } else {
        _p->cache.clear();
    }

    foreach (const QByteArray &piece, fragments) {
        if (piece.isEmpty()) continue;
        if (_p->debug_node->getBool()) DLOG << "recv(" << QThread::currentThreadId() << "): " << piece;
        Package pkg(nullptr, IMOL_EMPTY_MODULE_NAME);
        if (pkg.deserialize(piece)) _p->recv(pkg);
    }
}

XService * xservice() { return g_xservice_instance; }
