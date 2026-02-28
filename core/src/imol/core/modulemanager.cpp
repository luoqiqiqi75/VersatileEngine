#include "core/modulemanager.h"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDir>
#include <QMutex>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QUuid>
#include <QDataStream>
#include <QCoreApplication>
#include <QSizeF>
#include <QPointF>

#include "core/logmanager.h"
#include "core/commandmanager.h"

namespace imol {

//!
//! ModuleObject implementation
//!
ModuleObject::ModuleObject(const QString &name, ModuleObject *pmobj) : QObject(nullptr),
    m_pmobj(pmobj),
    m_name(name),
    m_is_quiet(false),
    m_is_watching(false),
    m_mutex(new QMutex)
{
    m_prev_bmobj = m_next_bmobj = nullptr;
    m_first_cmobj = m_last_cmobj = nullptr;
}

ModuleObject::~ModuleObject()
{
    qDeleteAll(m_cmobjs);
    delete m_mutex;
}

bool ModuleObject::isEmptyMobj() const { return false; }

QString ModuleObject::name() const { return m_name; }

bool ModuleObject::isQuiet() const { return m_is_quiet; }
void ModuleObject::quiet(bool is_quiet, bool is_recursively)
{
    m_is_quiet = is_quiet;
    if (is_recursively) foreach (auto mobj, m_cmobjs) mobj->quiet(is_quiet, true);
}

bool ModuleObject::isWatching() const { return m_is_watching; }
void ModuleObject::watch(bool is_watching, bool is_recursively)
{
    m_is_watching = is_watching;
    if (is_recursively) foreach (auto mobj, m_cmobjs) mobj->watch(is_watching, true);
}

bool isStrictName(const QString &name) { return name.isEmpty() || name[0] != IMOL_MODULE_ID_START[0]; }

int ModuleObject::indexOfRname(const QString &rname)
{
    if (isStrictName(rname)) return -1;

    bool is_int = false;
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    int index = rname.mid(1).toInt(&is_int);
#else
    int index = rname.midRef(1).toInt(&is_int);
#endif
    return is_int ? index : -1;
}

QString ModuleObject::generateId(ModuleObject *mobj)
{
    QString id = IMOL_MODULE_ID_START + QUuid::createUuid().toString();
    if (!mobj->hasCmobj(id, true)) return id;

    int n = 0;
    while (mobj->hasCmobj(QString("%1_%2").arg(id).arg(++n))) {}

    return QString("%1_%2").arg(id).arg(n);
}

ModuleObject * ModuleObject::pmobj() const
{ auto mobj = m_pmobj; return mobj ? mobj : m().nullData(); }
ModuleObject * ModuleObject::pmobj(int level) const
{ return m_pmobj ? (level > 0 ? m_pmobj->pmobj(level - 1) : m_pmobj) : m().nullData(); }
ModuleObject * ModuleObject::p(int level) const { return pmobj(level); }

ModuleObject * ModuleObject::prevBmobj() const
{ auto mobj = m_prev_bmobj; return mobj ? mobj : m().nullData(); }
ModuleObject * ModuleObject::nextBmobj() const
{ auto mobj = m_next_bmobj; return mobj ? mobj : m().nullData(); }
ModuleObject * ModuleObject::bmobj(int next) const
{
    ModuleObject *mobj = const_cast<ModuleObject*>(this);
    if (next > 0) {
        for (int i = 0; i < next && !mobj->isEmptyMobj(); i++) mobj = mobj->nextBmobj();
    } else if (next < 0) {
        for (int i = 0; i < - next && !mobj->isEmptyMobj(); i++) mobj = mobj->prevBmobj();
    }
    return mobj;
}
ModuleObject * ModuleObject::b(int next) const { return bmobj(next); }
ModuleObject * ModuleObject::b(const QString &rname, bool is_strict) const { return pmobj()->cmobj(rname, is_strict); }

int ModuleObject::indexInPmobj() const
{
    if (pmobj()->isEmptyMobj()) return -1;
    ModuleObject *bmobj = const_cast<ModuleObject*>(this);
    int index = 0;
    while (!(bmobj = bmobj->prevBmobj())->isEmptyMobj()) index++;
    return index;
}

bool ModuleObject::isRelative() const { return !isStrictName(m_name); }

QString ModuleObject::rname() const { return isStrictName(m_name) ? m_name : mIndex(indexInPmobj(), m_name); }

ModuleObject * ModuleObject::cmobj(const QString &rname, bool is_strict) const
{
    if (is_strict) return m_cmobjs.value(rname, m().nullData());
    int index = indexOfRname(rname);
    return index >= 0 ? cmobj(index) : m_cmobjs.value(rname, m().nullData());
}
ModuleObject * ModuleObject::c(const QString &rname, bool is_strict) const { return cmobj(rname, is_strict); }
ModuleObject * ModuleObject::cmobj(int index) const
{
    int s = m_cmobjs.size();
    if (index >= s || index < 0) return m().nullData();
    return (index > s / 2) ? last()->bmobj(index + 1 - s) : first()->bmobj(index);
}
ModuleObject * ModuleObject::c(int index) const { return cmobj(index); }

bool ModuleObject::hasCmobj(const QString &rname, bool is_strict) const
{
    if (rname.isEmpty()) return !m_cmobjs.isEmpty();
    if (is_strict) return m_cmobjs.contains(rname);
    int index = indexOfRname(rname);
    return index < 0 ? m_cmobjs.contains(rname) : index < m_cmobjs.size();
}
bool ModuleObject::hasCmobj(ModuleObject *mobj) const
{ return m_cmobjs.contains(mobj->name()); }

ModuleObject * ModuleObject::first() const
{ auto mobj = m_first_cmobj; return mobj ? mobj : m().nullData(); }
ModuleObject * ModuleObject::last() const
{ auto mobj = m_last_cmobj; return mobj ? mobj : m().nullData(); }

int ModuleObject::cmobjCount() const { return m_cmobjs.size(); }
int ModuleObject::size() const { return m_cmobjs.size(); }

QStringList ModuleObject::cmobjNames(bool is_strict) const
{
    int cnt = 0;
    QStringList names;
    m_mutex->lock();
    names.reserve(size());
    for (ModuleObject *mobj = first(); !mobj->isEmptyMobj(); mobj = mobj->nextBmobj()) {
        names.append(!is_strict && mobj->isRelative() ? mIndex(cnt) : mobj->name());
        cnt++;
    }
    m_mutex->unlock();
    return names;
}

QList<ModuleObject *> ModuleObject::cmobjs(bool) const
{
//    if (!is_ordered) return m_cmobjs.values();

    QList<ModuleObject *> mobjs;
    m_mutex->lock();
    mobjs.reserve(size());
    for (ModuleObject *mobj = first(); !mobj->isEmptyMobj(); mobj = mobj->nextBmobj()) {
        mobjs.append(mobj);
    }
    m_mutex->unlock();
    return mobjs;
}

ModuleObject * ModuleObject::rmobj(const QString &rpath) const
{
    ModuleObject *tar_mobj = const_cast<ModuleObject *>(this);
    foreach (const QString &name, rpath.split(IMOL_MODULE_NAME_SEPARATOR)) {
        if ((tar_mobj = tar_mobj->cmobj(name))->isEmptyMobj()) return m().nullData();
    }
    return tar_mobj;
}
ModuleObject * ModuleObject::r(const QString &rpath) const { return rmobj(rpath); }

bool ModuleObject::hasRmobj(const QString &rpath) const
{ return !rmobj(rpath)->isEmptyMobj(); }
bool ModuleObject::isRmobjOf(ModuleObject *mobj) const
{ return pmobj()->isEmptyMobj() ? false : (pmobj() == mobj ? true : pmobj()->isRmobjOf(mobj)); }

QString ModuleObject::fullName(ModuleObject *ancestral_mobj) const
{
    ModuleObject *mobj = pmobj();
    ModuleObject *rmobj = ancestral_mobj ? ancestral_mobj : m().rootMobj();
    return (mobj->isEmptyMobj() || mobj == rmobj) ? rname() : mName(mobj->fullName(ancestral_mobj), rname());
}

void ModuleObject::activate(QObject *context, ModuleObject *changed_mobj, ActivateType type)
{
    if (m_is_quiet) return;

    emit activated(changed_mobj, type, context);

    ModuleObject *mobj = pmobj();
    if (!mobj->isEmptyMobj() && mobj->isWatching()) mobj->activate(context, changed_mobj, type);
}

const QVariant& ModuleObject::get(const QVariant &default_var) const
{
    return m_var.isNull() ? default_var : m_var;
}

bool ModuleObject::getBool(bool default_bool) const
{
    QVariant var = get();
    return var.isNull() || !var.canConvert(QVariant::Bool) ? default_bool : var.toBool();
}

int ModuleObject::getInt(int default_int) const
{
    QVariant var = get();
    return var.isNull() || !var.canConvert(QVariant::Int) ? default_int : var.toInt();
}

double ModuleObject::getDouble(double default_double) const
{
    QVariant var = get();
    return var.isNull() || !var.canConvert(QVariant::Double) ? default_double : var.toDouble();
}

QString ModuleObject::getString(const QString &default_str) const
{
    QVariant var = get();
    return var.isNull() || !var.canConvert(QVariant::String) ? default_str : var.toString();
}

QVariantList ModuleObject::getList() const
{
    QVariantList list;
    foreach (ModuleObject *mobj, cmobjs(true)) {
        list.append(mobj->get());
    }
    return list;
}

void ModuleObject::setList(QObject *context, const QVariantList &vars, bool intelligent)
{
    for (int i = 0; i < vars.size(); ++i) {
        set(context, mIndex(i), vars.at(i), intelligent);
    }
}

void ModuleObject::setList(QObject *context, const QString &rpath, const QVariantList &vars, bool intelligent)
{
    for (int i = 0; i < vars.size(); ++i) {
        set(context, mName(rpath, mIndex(i)), vars.at(i), intelligent);
    }
}

ModuleObject * ModuleObject::set(QObject *context, const QVariant &var)
{
    QVariant tmp_var = var;

    m_var.swap(tmp_var);

    //emit signal
    if (!isQuiet()) emit changed(m_var, tmp_var, context);
    if (isWatching()) activate(context, this, MOBJ_CHANGE);

    return this;
}

ModuleObject * ModuleObject::set(QObject *context, const QString &rpath, const QVariant &var, bool intelligent)
{
    ModuleObject *tar_mobj = rmobj(rpath);

    //need insert
    if (tar_mobj->isEmptyMobj() && intelligent) {
        insert(context, rpath);
        tar_mobj = rmobj(rpath);
    }

    //normal set
    if (tar_mobj->isEmptyMobj()) WLOG << "<m><" << context << "> set unreachable module: \"" << rpath << "\" with: " << var;

    tar_mobj->set(context, var);

    return this;
}

bool ModuleObject::insert(QObject *context, ModuleObject *mobj, bool auto_replace, int index)
{
    if (!mobj || mobj->isEmptyMobj()) {
        ESLOG << "<mobj><" << context << "> attempt to insert a null module obj";
        return false;
    }

    if (mobj->name().isEmpty()) {
        mobj->setName(generateId(this));
    } else if (hasCmobj(mobj->name(), true)) {
        if (!auto_replace) {
            ESLOG << "<mobj><" << context << "> attempt to insert an existed module obj: " << mobj->name();
            return false;
        }
        if (!remove(context, mobj->name())) return false;
    }

    if (!isQuiet()) emit gonnaInsert(mobj, this, context);

    m_mutex->lock();

    mobj->setPmobj(this);
    mobj->watch(isWatching());
    mobj->setPrevBmobj(nullptr);
    mobj->setNextBmobj(nullptr);

    if (index < 0 || index > size()) index = size();
    if (!m_first_cmobj) { // size == 0, index == 0
        m_first_cmobj = mobj;
        m_last_cmobj = mobj;
    } else if (index == size()) { // size > 0, index == size
        m_last_cmobj->setNextBmobj(mobj);
        mobj->setPrevBmobj(m_last_cmobj);
        m_last_cmobj = mobj;
    } else if (index == 0) { // size > 0, index == 0
        m_first_cmobj->setPrevBmobj(mobj);
        mobj->setNextBmobj(m_first_cmobj);
        m_first_cmobj = mobj;
    } else { // size > 0, 0 < index < size
        ModuleObject *prev = cmobj(index - 1); // no empty
        ModuleObject *next = prev->nextBmobj();
        prev->setNextBmobj(mobj);
        next->setPrevBmobj(mobj);
        mobj->setNextBmobj(next);
        mobj->setPrevBmobj(prev);
    }

    m_cmobjs.insert(mobj->name(), mobj);

    m_mutex->unlock();

    if (!isQuiet()) emit added(mobj->isRelative() ? mIndex(index) : mobj->name(), context);
    if (isWatching()) activate(context, mobj, MOBJ_INSERT);

    return true;
}

bool ModuleObject::insert(QObject *context, const QString &rpath, bool auto_replace)
{
    ModuleObject *tar_mobj = this;
    foreach (const QString &rname, rpath.split(IMOL_MODULE_NAME_SEPARATOR)) {
        if (tar_mobj->isEmptyMobj()) return false;

        if (!tar_mobj->hasCmobj(rname)) {
            int index = indexOfRname(rname);
            if (index < 0) {
                ModuleObject *mobj = m().create(rname, tar_mobj);
                if (!tar_mobj->insert(context, mobj, auto_replace)) {
                    delete mobj;
                    return false;
                }
            } else {
                for (int i = tar_mobj->cmobjCount(); i <= index; i++) {
                    ModuleObject *mobj = m().create(generateId(this), tar_mobj);
                    if (!tar_mobj->insert(context, mobj, auto_replace)) {
                        delete mobj;
                        return false;
                    }
                }
            }
        }

        tar_mobj = tar_mobj->cmobj(rname);
    }

    return true;
}

ModuleObject * ModuleObject::append(QObject *context, const QString &name)
{
    ModuleObject *mobj = m().create(name.isEmpty() ? generateId(this) : name, this);
    if (!insert(context, mobj)) {
        delete mobj;
        return m().nullData();
    }
    return mobj;
}

bool ModuleObject::remove(QObject *context, ModuleObject *mobj, bool auto_delete)
{
    if (!mobj || mobj->isEmptyMobj()) {
        ESLOG << "<mobj><" << context << "> attempt to remove a null module obj";
        return false;
    }

    if (!hasCmobj(mobj->name(), true)) {
        ESLOG << "<mobj><" << context << "> attempt to remove an unexisted module obj: " << mobj->name();
        return false;
    }

    if (!isQuiet()) emit gonnaRemove(mobj, this, context);

    m_mutex->lock();

    QString rname = mobj->isRelative() ? mIndex(mobj->indexInPmobj()) : mobj->name();

    ModuleObject *prev = mobj->prevBmobj()->isEmptyMobj() ? nullptr : mobj->prevBmobj();
    ModuleObject *next = mobj->nextBmobj()->isEmptyMobj() ? nullptr : mobj->nextBmobj();

    if (m_first_cmobj == mobj) m_first_cmobj = next;
    if (m_last_cmobj == mobj) m_last_cmobj = prev;

    if (prev) prev->setNextBmobj(next);
    if (next) next->setPrevBmobj(prev);

    mobj->setPrevBmobj(nullptr);
    mobj->setNextBmobj(nullptr);

    m_cmobjs.remove(mobj->name());

    m_mutex->unlock();

    if (!isQuiet()) emit removed(rname, context);
    if (isWatching()) activate(context, mobj, MOBJ_REMOVE);

    if (auto_delete) delete mobj;

    return true;
}

bool ModuleObject::remove(QObject *context, const QString &rpath, bool auto_delete)
{
    ModuleObject *tar_mobj = rmobj(rpath);
    return tar_mobj->pmobj()->remove(context, tar_mobj, auto_delete);
}

void ModuleObject::copyFrom(QObject *context, ModuleObject *other, bool auto_insert, bool auto_remove)
{
    if (!other) return;

    //auto remove
    if (auto_remove) {
        foreach (ModuleObject *mobj, m_cmobjs) {
            if (!other->hasCmobj(mobj->rname())) remove(context, mobj);
        }
    }

    //auto insert
    foreach (const QString &rname, other->cmobjNames()) {
        if (!hasCmobj(rname) && auto_insert && !insert(context, rname)) continue;
        cmobj(rname)->copyFrom(context, other->cmobj(rname), auto_insert, auto_remove);
    }

    set(context, other->get());
}

bool ModuleObject::reorder(QObject *context, int from_index, int to_index)
{
    if (from_index == to_index || size() <= 1) return false;

    ModuleObject *mobj = cmobj(from_index);
    if (mobj->isEmptyMobj() || !remove(context, mobj, false)) return false;
    if (!insert(context, mobj, false, to_index)) return false;
    return true;
}

bool ModuleObject::reorder(QObject *context, const QStringList &rnames)
{
    if (rnames.isEmpty() || size() <= 1 || rnames.size() > size()) return false;

    QList<ModuleObject *> ordered_mobjs;
    ordered_mobjs.reserve(size());
    foreach (const QString &rname, rnames) {
        ModuleObject *mobj = cmobj(rname);
        if (mobj->isEmptyMobj() || ordered_mobjs.contains(mobj)) continue;
        ordered_mobjs.append(mobj);
    }
    if (ordered_mobjs.size() < size()) {
        QList<ModuleObject *> cur_mobjs = cmobjs();
        foreach (ModuleObject *mobj, ordered_mobjs) {
            cur_mobjs.removeOne(mobj);
        }
        ordered_mobjs.append(cur_mobjs);
    }

    m_mutex->lock();
    m_first_cmobj = ordered_mobjs.first();
    m_last_cmobj = ordered_mobjs.last();
    for (int i = 0; i < ordered_mobjs.size(); i++) {
        ModuleObject *mobj = ordered_mobjs.at(i);
        mobj->setPrevBmobj(mobj == m_first_cmobj ? nullptr : ordered_mobjs.at(i - 1));
        mobj->setNextBmobj(mobj == m_last_cmobj ? nullptr : ordered_mobjs.at(i + 1));
    }
    m_mutex->unlock();

    //emit signal
    if (isWatching()) activate(context, this, MOBJ_REORDER);

    return true;
}

void ModuleObject::clear(QObject *context, bool auto_delete, bool only_var)
{
    if (only_var) {
        //only clear var
        foreach (ModuleObject *mobj, cmobjs()) {
            mobj->clear(context, auto_delete, only_var);
        }
        set(context, QVariant());
    } else {
        //clear all cmobjs
        foreach (ModuleObject *mobj, cmobjs()) {
            remove(context, mobj, auto_delete);
        }
        set(context, QVariant());
    }
}

void ModuleObject::dump(ModuleObject *rmobj) const
{
    SLOG << "<mobj>" << fullName(rmobj) << ": " << get() << ", children: " << cmobjNames();
    foreach (ModuleObject *mobj, cmobjs(true)) {
        if (mobj) mobj->dump();
    }
}

void ModuleObject::importFromVariant(QObject *context, const QVariant &var, bool auto_insert, bool auto_replace, bool auto_remove)
{
    if (var.type() == QVariant::List) {
        QVariantList list = var.toList();

        // remove control
        if (auto_remove) {
            while (cmobjCount() > list.size()) {
                remove(context, last());
            }
        }

        // insert control
        if (auto_insert) {
            while (cmobjCount() < list.size()) {
                append(context);
            }
        }

        // cmobj
        QList<ModuleObject *> mobjs = cmobjs();
        for (int i = 0; i < qMin(list.size(), mobjs.size()); i++) {
            mobjs.at(i)->importFromVariant(context, list.at(i), auto_insert, auto_replace, auto_remove);
        }
    } else if (var.type() == QVariant::Map) {
        QVariantMap dict = var.toMap();

        // remove control
        if (auto_remove) {
            foreach (const QString& key, cmobjNames(true)) {
                if (!dict.contains(key)) remove(context, key);
            }
        }

        // insert control
        if (auto_insert) {
            foreach (const QString& key, dict.keys()) {
                if (!hasRmobj(key)) insert(context, key, auto_replace);
            }
        }

        // rmobj
        foreach (const QString& key, dict.keys()) {
            rmobj(key)->importFromVariant(context, dict.value(key), auto_insert, auto_replace, auto_remove);
        }
    } else if (var.type() == QVariant::Hash) {
        QVariantHash dict = var.toHash();

        // remove control
        if (auto_remove) {
            foreach (const QString& key, cmobjNames(true)) {
                if (!dict.contains(key)) remove(context, key);
            }
        }

        // insert control
        if (auto_insert) {
            foreach (const QString& key, dict.keys()) {
                if (!hasRmobj(key)) insert(context, key, auto_replace);
            }
        }

        // rmobj
        foreach (const QString& key, dict.keys()) {
            rmobj(key)->importFromVariant(context, dict.value(key), auto_insert, auto_replace, auto_remove);
        }
    } else {
        // replace control
        if (!auto_replace) return;

        // remove control
        if (auto_remove) {
            foreach (ModuleObject *mobj, cmobjs()) {
                remove(context, mobj);
            }
        }

        // data
        set(context, var);
        return;
    }

    //emit signal
    if (!isQuiet()) emit changed(m_var, m_var, context);
    if (isWatching()) activate(context, this, MOBJ_CHANGE);
}

QVariant ModuleObject::exportToVariant(bool auto_ignore) const
{
    // set var
    if (!hasCmobj()) return get();

    if (first()->isRelative()) {
        // build as list
        QVariantList list;
        foreach (ModuleObject *mobj, cmobjs(true)) {
            list.append(mobj->exportToVariant(auto_ignore));
        }
        return list;
    } else {
        // build as dict
        QVariantMap dict;
        foreach (const QString &name, cmobjNames(true)) {
            if (auto_ignore && name.startsWith(IMOL_MODULE_SPECIAL_START)) continue;
            dict.insert(name, cmobj(name, true)->exportToVariant(auto_ignore));
        }
        return dict;
    }
}

void ModuleObject::importFromJson(QObject *context, const QJsonValue &json_value, bool auto_insert, bool auto_replace, bool auto_remove)
{
    if (json_value.isArray()) {
        QJsonArray list = json_value.toArray();

        // remove control
        if (auto_remove) {
            while (cmobjCount() > list.size()) {
                remove(context, last());
            }
        }

        // insert control
        if (auto_insert) {
            while (cmobjCount() < list.size()) {
                append(context);
            }
        }

        // cmobj
        QList<ModuleObject *> mobjs = cmobjs();
        for (int i = 0; i < qMin(list.size(), mobjs.size()); i++) {
            mobjs.at(i)->importFromJson(context, list.at(i), auto_insert, auto_replace, auto_remove);
        }
    } else if (json_value.isObject()) {
        QJsonObject dict = json_value.toObject();

        // remove control
        if (auto_remove) {
            foreach (const QString& key, cmobjNames(true)) {
                if (!dict.contains(key)) remove(context, key);
            }
        }

        // insert control
        if (auto_insert) {
            foreach (const QString& key, dict.keys()) {
                if (!hasRmobj(key)) insert(context, key, auto_replace);
            }
        }

        // rmobj
        foreach (const QString& key, dict.keys()) {
            rmobj(key)->importFromJson(context, dict.value(key), auto_insert, auto_replace, auto_remove);
        }
    } else {
        // replace control
        if (!auto_replace) return;

        // remove control
        if (auto_remove) {
            foreach (ModuleObject *mobj, cmobjs()) {
                remove(context, mobj);
            }
        }

        // data
        set(context, json_value.toVariant());
        return;
    }

    //emit signal
    if (!isQuiet()) emit changed(m_var, m_var, context);
    if (isWatching()) activate(context, this, MOBJ_CHANGE);
}

QJsonValue ModuleObject::exportToJson(bool auto_ignore) const
{
    //set json from var
    if (!hasCmobj()) return QJsonValue::fromVariant(get());

    if (first()->isRelative()) {
        //build json array
        QJsonArray list;
        foreach (ModuleObject *mobj, cmobjs(true)) {
            list.append(mobj->exportToJson(auto_ignore));
        }
        return list;
    } else {
        //build json object
        QJsonObject dict;
        foreach (const QString &name, cmobjNames(true)) {
            if (auto_ignore && name.startsWith(IMOL_MODULE_SPECIAL_START)) continue;
            dict.insert(name, cmobj(name, true)->exportToJson(auto_ignore));
        }
        return dict;
    }
}

bool qvariant2String(const QVariant &var, QString &str)
{
    if (var.canConvert(QVariant::String)) {
        str = var.toString();
        return true;
    }

    if (QString(var.typeName()).isEmpty()) return false;

    bool success = true;
    switch (var.type()) {
    case QVariant::Size: {
        QSize s = var.toSize();
        str = QString("%1,%2").arg(s.width()).arg(s.height());
    }
        break;
    case QVariant::SizeF: {
        QSizeF s = var.toSizeF();
        str = QString("%1,%2").arg(s.width()).arg(s.height());
    }
        break;
    case QVariant::Point: {
        QPoint p = var.toPoint();
        str = QString("%1,%2").arg(p.x()).arg(p.y());
    }
        break;
    case QVariant::PointF: {
        QPointF p = var.toPointF();
        str = QString("%1,%2").arg(p.x()).arg(p.y());
    }
        break;
    default:
        success = false;
        break;
    }

    return success;
}

bool string2QVariant(int type, const QString &str, QVariant &var)
{
    switch (type) {
    case QVariant::Size: {
        QStringList xy = str.split(",");
        var = QSize(xy.value(0).toInt(), xy.value(1).toInt());
    }
        break;
    case QVariant::SizeF: {
        QStringList xy = str.split(",");
        var = QSizeF(xy.value(0).toDouble(), xy.value(1).toDouble());
    }
        break;
    case QVariant::Point: {
        QStringList xy = str.split(",");
        var = QPoint(xy.value(0).toInt(), xy.value(1).toInt());
    }
        break;
    case QVariant::PointF: {
        QStringList xy = str.split(",");
        var = QPointF(xy.value(0).toDouble(), xy.value(1).toDouble());
    }
        break;
    default:
        var = QVariant(str);
        return var.convert(type);
    }

    return true;
}

void ModuleObject::importFromXml(QObject *context, QXmlStreamReader &xml_reader, bool auto_insert, bool auto_replace, bool only_standard)
{
    QHash<ModuleObject *, int> index_hash; //save index
    QHash<ModuleObject *, QVariant> value_hash; //save value
    for (ModuleObject *tar_mobj = this; !xml_reader.atEnd() && tar_mobj != pmobj(); xml_reader.readNext()) {
        if (xml_reader.isStartElement()) {
            QXmlStreamAttributes attrs = xml_reader.attributes();

            //value for current node
            QVariant value_var;
            if (attrs.hasAttribute("value")) {
                if (attrs.hasAttribute("type")) {
                    if (!string2QVariant(attrs.value("type").toInt(), attrs.value("value").toString(), value_var)) {
                        WLOG << "<m><" << context << "> cannot convert in xml: ("<< xml_reader.lineNumber() << ", " << xml_reader.columnNumber() << ")";
                    }
                } else {
                    value_var = attrs.value("value").toString();
                }
            }

            //set current value quietly
            if (xml_reader.name() == QString("m")) {
                if (auto_replace) {
                    quiet(true);
                    set(context, value_var);
                    quiet(false);
                }
                continue;
            }

            //set cmobj name
            QString cmobj_name;

            if (xml_reader.name() == QString("c")) {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
                QStringView name_ref = attrs.value("name");
#else
                QStringRef name_ref = attrs.value("name");
#endif
                if (name_ref.isNull() || name_ref.isEmpty()) {
                    WLOG << "<m><" << context << "> no name specified in xml: ("<< xml_reader.lineNumber() << ", " << xml_reader.columnNumber() << ")";
                    xml_reader.skipCurrentElement();
                    continue;
                }
                cmobj_name = QString().append(name_ref);
            } else if (xml_reader.name() == QString("l")) {
                cmobj_name = mIndex(index_hash.value(tar_mobj, 0));
            } else {
                cmobj_name = QString().append(xml_reader.name());

                if (only_standard) {
                    WLOG << "<m><" << context << "> non-standard xml element: \"" << cmobj_name << "\"";
                    xml_reader.skipCurrentElement();
                    continue;
                }
            }

            //create cmobj
            if (!tar_mobj->hasCmobj(cmobj_name)) {
                if (!auto_insert) {
                    WLOG << "<m><" << context << "> importing xml element not exist: \"" << cmobj_name << "\"";
                    xml_reader.skipCurrentElement();
                    continue;
                } else if (!tar_mobj->insert(context, cmobj_name, auto_replace)) {
                    xml_reader.skipCurrentElement();
                    continue;
                }
            }

            //record index
            index_hash[tar_mobj] = index_hash.value(tar_mobj, 0) + 1;

            //insert child and set value
            tar_mobj = tar_mobj->cmobj(cmobj_name);
            if (auto_replace) value_hash[tar_mobj] = value_var;
        } else if (xml_reader.isEndElement()) {
            //finish cur item
            if (auto_replace) tar_mobj->set(context, value_hash.value(tar_mobj, QVariant()));

            index_hash.remove(tar_mobj);
            value_hash.remove(tar_mobj);

            tar_mobj = tar_mobj->pmobj();
        }
    }

    //emit signal
    if (!isQuiet()) emit changed(get(), get(), context);
    if (isWatching()) activate(context, this, MOBJ_CHANGE);
}

void ModuleObject::exportToXml(QXmlStreamWriter &xml_writer, bool is_root, bool auto_ignore) const
{
    xml_writer.writeStartElement(is_root ? "m" : (name().startsWith(IMOL_MODULE_ID_START) ? "l" : "c"));
    if (!is_root && !name().startsWith(IMOL_MODULE_ID_START)) xml_writer.writeAttribute("name", name());

    //write value
    QVariant var = get();
    if (!var.isNull()) {
        xml_writer.writeAttribute("type", QString::number(var.type()));
        QString value_str;
        if (qvariant2String(var, value_str)) xml_writer.writeAttribute("value", value_str);
    }

    //write cmobj
    foreach (ModuleObject *cmobj, cmobjs(true)) {
        if (auto_ignore && cmobj->name().startsWith(IMOL_MODULE_SPECIAL_START)) continue;
        cmobj->exportToXml(xml_writer, false, auto_ignore);
    }

    xml_writer.writeEndElement();
}

void ModuleObject::importFromXmlStr(QObject *context, const QString &xml_str, bool auto_insert, bool auto_replace, bool only_standard)
{
    QXmlStreamReader xml_reader(xml_str);
    importFromXml(context, xml_reader, auto_insert, auto_replace, only_standard);
}

QString ModuleObject::exportToXmlStr(int indent, bool auto_ignore) const
{
    QString xml_str;
    QXmlStreamWriter xml_writer(&xml_str);
    if (indent > 0) {
        xml_writer.setAutoFormatting(true);
        xml_writer.setAutoFormattingIndent(indent);
    }

    xml_writer.writeStartDocument();
    exportToXml(xml_writer, true, auto_ignore);
    xml_writer.writeEndDocument();

    return xml_str;
}

enum BinaryVariantCondition
{
    BIN_NULL,
    BIN_QVARIANT,
    BIN_CUSTOM
};

void ModuleObject::importFromBin(QObject *context, const QByteArray &bytes, bool auto_insert, bool auto_replace, bool auto_remove)
{
    QDataStream in_stream(bytes);

    //variant
    QVariant var;
    uchar cond;
    in_stream >> cond;
    if (cond == BIN_QVARIANT) {
        in_stream >> var;
    } else if (cond == BIN_CUSTOM) {
        QString type_name;
        int type_id;
        QByteArray var_data;
        in_stream >> type_name >> type_id >> var_data;

        if (type_id != QMetaType::type(type_name.toStdString().c_str())) {
            WLOG << "<m> import invalid variant: " << fullName() << " with type \"" << type_name << "\" and id = " << type_id;
        } else {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
            var = QVariant(static_cast<QMetaType>(type_id), QMetaType::create(type_id, var_data.data()));
#else
            var = QVariant(type_id, QMetaType::create(type_id, var_data.data()));
#endif
        }

    }

    //cmobjs
    QStringList cmobj_names;
    in_stream >> cmobj_names;

    //start to remove
    if (auto_remove) {
        foreach (ModuleObject *cmobj, cmobjs()) {
            if (!cmobj_names.contains(cmobj->rname())) remove(context, cmobj);
        }
    }

    //start to insert
    foreach (QString cmobj_name, cmobj_names) {
        QByteArray cmobj_bytes;
        in_stream >> cmobj_bytes;

        if (!hasCmobj(cmobj_name)) {
            if (!auto_insert) continue;
            if (!insert(context, cmobj_name)) continue;
        }

        cmobj(cmobj_name)->importFromBin(context, cmobj_bytes, auto_insert, auto_replace, auto_remove);
    }

    //start to replace
    if (auto_replace) {
        set(context, var);
    } else {
        //emit signal
        if (!isQuiet()) emit changed(get(), get(), context);
        if (isWatching()) activate(context, this, MOBJ_CHANGE);
    }
}

Q_GLOBAL_STATIC_WITH_ARGS(QList<int>, unexportable_type_ids,
                          ({QMetaType::UnknownType, QMetaType::QObjectStar, QMetaType::Void, QMetaType::Nullptr, QMetaType::VoidStar}));

QByteArray ModuleObject::exportToBin(bool auto_ignore) const
{
    QByteArray bytes;
    QDataStream out_stream(&bytes, QIODevice::WriteOnly);

    //variant
    uchar cond = BIN_NULL;
    QVariant var = get();
    if (var.isNull() || unexportable_type_ids()->contains(var.type())) {
        out_stream << cond;
    } else if (var.type() >= QVariant::UserType) {
        cond = BIN_CUSTOM;
        QString type_name = var.typeName();
        int type_id = QMetaType::type(var.typeName());
        out_stream << cond << type_name << type_id << QByteArray((char *)var.data(), QMetaType::sizeOf(type_id));
    } else {
        cond = BIN_QVARIANT;
        out_stream << cond << var;
    }

    //cmobjs
    QStringList cmobj_names;
    foreach (ModuleObject *cmobj, cmobjs(true)) {
        if (auto_ignore && cmobj->name().startsWith(IMOL_MODULE_SPECIAL_START)) continue;
        cmobj_names << cmobj->rname();
    }
    out_stream << cmobj_names;

    foreach (ModuleObject *cmobj, cmobjs(true)) {
        if (auto_ignore && cmobj->name().startsWith(IMOL_MODULE_SPECIAL_START)) continue;
        out_stream << cmobj->exportToBin(auto_ignore);
    }
    return bytes;
}

void ModuleObject::trigger() { set(sender(), get()); }

void ModuleObject::setName(const QString &name) { m_name = name; }
void ModuleObject::setPmobj(ModuleObject *mobj) { qSwap(m_pmobj, mobj); }
void ModuleObject::setPrevBmobj(ModuleObject *mobj) { qSwap(m_prev_bmobj, mobj); }
void ModuleObject::setNextBmobj(ModuleObject *mobj) { qSwap(m_next_bmobj, mobj); }

//!
//! EmptyModuleObject implementation
//!
EmptyModuleObject::EmptyModuleObject(const QString &name, ModuleObject *parent_mobj) : ModuleObject(name, parent_mobj) {}

bool EmptyModuleObject::isEmptyMobj() const { return true; }

const QVariant& EmptyModuleObject::get(const QVariant &) const { static QVariant v; return v; }

ModuleObject * EmptyModuleObject::set(QObject *, const QVariant &) { return this; }
ModuleObject * EmptyModuleObject::set(QObject *, const QString &, const QVariant &, bool) { return this; }

void EmptyModuleObject::copyFrom(QObject *, ModuleObject *, bool, bool) {}

bool EmptyModuleObject::insert(QObject *, ModuleObject *, bool, int) { return false; }
bool EmptyModuleObject::insert(QObject *, const QString &, bool) { return false; }
ModuleObject * EmptyModuleObject::append(QObject *, const QString &) { return this; }

bool EmptyModuleObject::remove(QObject *, ModuleObject *, bool) { return false; }
bool EmptyModuleObject::remove(QObject *, const QString &, bool) { return false; }

void EmptyModuleObject::importFromJson(QObject *, const QJsonValue &, bool, bool, bool) {}
QJsonValue EmptyModuleObject::exportToJson(bool) const { return QJsonValue(); }

void EmptyModuleObject::importFromXml(QObject *, QXmlStreamReader &, bool, bool, bool) {}
void EmptyModuleObject::exportToXml(QXmlStreamWriter &, bool, bool) const {}

void EmptyModuleObject::importFromBin(QObject *, const QByteArray &, bool, bool, bool) {}
QByteArray EmptyModuleObject::exportToBin(bool) const { return QByteArray(); }

//!
//! \brief The ConvertCommand class
//!
class ConvertCommand : public BaseCommand
{
public:
    explicit ConvertCommand(QObject *parent = nullptr) : BaseCommand("convert", false, parent) {}
    QString usage() const override {return tr("convert JSON|XML|BIN JSON|XML|BIN -data=<data>|-path=<path>");}
    QString instruction() const override {return tr("Convert 1st type of data to 2nd type");}

protected:
    void run(ModuleObject *mobj, const QString &param) override
    {
        Q_UNUSED(mobj)

        QString type0 = param.section(" ", 0, 0).trimmed();
        QString type1 = param.section(" ", 1, 1).trimmed();
        if (type0 == type1) {
            emit error(tr("origin type is same with target type"));
            return;
        }

        ModuleObject temp("");

        //read
        if (param.contains("-path")) {
            QString path = param.section("-path=", -1);
            if (type0.startsWith("JSON", Qt::CaseInsensitive)) {
                temp.importFromJson(this, m().readFromJson(path));
            } else if (type0.startsWith("XML", Qt::CaseInsensitive)) {
                m().readFromXmlFile(this, &temp, path);
            } else if (type0.startsWith("BIN", Qt::CaseInsensitive)) {
                temp.importFromBin(this, qUncompress(m().readFromBin(path)));
            } else {
                emit error(tr("unknown origin type: ") + type0);
                return;
            }
        } else if (param.contains("-data")) {
            QString data = param.section("-data=", -1);
            if (type0.startsWith("JSON", Qt::CaseInsensitive)) {
                QJsonDocument json_doc = QJsonDocument::fromJson(data.toUtf8());
                if (json_doc.object().isEmpty()) {
                    temp.importFromJson(this, json_doc.array());
                } else {
                    temp.importFromJson(this, json_doc.object());
                }
            } else if (type0.startsWith("XML", Qt::CaseInsensitive)) {
                temp.importFromXmlStr(this, data);
            } else if (type0.startsWith("BIN", Qt::CaseInsensitive)) {
                data.replace(" ", "");
                temp.importFromBin(this, qUncompress(QByteArray::fromHex(data.toUtf8())));
            }
        }

        //write
        if (type1.startsWith("JSON", Qt::CaseInsensitive)) {
            bool is_compact = false;
            QJsonValue json_value = temp.exportToJson(false);
            if (json_value.isArray()) {
                QJsonDocument json_doc(json_value.toArray());
                emit output(QString::fromUtf8(json_doc.toJson(is_compact ? QJsonDocument::Compact : QJsonDocument::Indented)));
            } else if (json_value.isObject()) {
                QJsonDocument json_doc(json_value.toObject());
                emit output(QString::fromUtf8(json_doc.toJson(is_compact ? QJsonDocument::Compact : QJsonDocument::Indented)));
            } else {
                emit output(json_value.toString());
            }
        } else if (type1.startsWith("XML", Qt::CaseInsensitive)) {
            emit output(temp.exportToXmlStr(4, false));
        } else if (type1.startsWith("BIN", Qt::CaseInsensitive)) {
            emit output(QString::fromUtf8(qCompress(temp.exportToBin(false)).toHex(' ')));
        }
    }
};

//!
//! ModuleManager implementation
//!
ModuleManager::ModuleManager(QObject *parent) : QObject(parent),
    m_root_mobj(new ModuleObject(IMOL_ROOT_MODULE_NAME)),
    m_empty_mobj(new EmptyModuleObject(IMOL_EMPTY_MODULE_NAME))
{
    command().regist(new ConvertCommand(this));
}

ModuleManager::~ModuleManager()
{
    delete m_root_mobj; // keep empty
}

ModuleManager & ModuleManager::instance()
{
    static ModuleManager manager;
    return manager;
}
QString ModuleManager::separator() { return IMOL_MODULE_NAME_SEPARATOR; }

ModuleObject * ModuleManager::rootMobj() const { return m_root_mobj; }
ModuleObject * ModuleManager::emptyMobj() const { return m_empty_mobj; }

void ModuleManager::reserve(int max_size)
{
    m_backup_mobjs.reserve(max_size);
}

ModuleObject * ModuleManager::create(const QString &name, ModuleObject *pmobj)
{
    // todo
    return new ModuleObject(name, pmobj);
}

ModuleObject * ModuleManager::regist(QObject *context, const QString &full_name)
{ return m_root_mobj->insert(context, full_name) ? m_root_mobj->rmobj(full_name) : m_empty_mobj; }

bool ModuleManager::cancel(QObject *context, const QString &full_name)
{ return m_root_mobj->remove(context, full_name); }

bool ModuleManager::importFromJson(QObject *context, const QString &json_path, const QString &prefix, bool is_watched, bool auto_insert)
{
    ModuleObject *tar_mobj = m(prefix);
    if (tar_mobj->isEmptyMobj()) tar_mobj = regist(context, prefix);

    QJsonValue json_value = readFromJson(json_path);
    if (json_value.isNull()) WLOG << "<m><" << context << "> import empty json from: \"" << json_path << "\"";
    tar_mobj->watch(is_watched, true);
    tar_mobj->importFromJson(context, json_value, auto_insert);
    return true;
}

bool ModuleManager::exportToJson(QObject *context, const QString &full_name, const QString &json_path)
{
    ModuleObject *tar_mobj = m(full_name);
    if (tar_mobj->isEmptyMobj()) {
        ELOG << "<m><" << context << "> export non-exist module: \"" << full_name << "\"";
        return false;
    }
    return writeToJson(json_path, tar_mobj->exportToJson());
}

QString ModuleManager::convertToUtf8(const QByteArray &content)
{
    // QTextCodec::ConverterState state;
    // QTextCodec *codec = QTextCodec::codecForName("UTF-8");
    // QString text = codec->toUnicode(content.constData(), content.size(), &state);
    // if (state.invalidChars > 0) {
    //     text = QTextCodec::codecForName("GBK")->toUnicode(content);
    // } else {
    //     text = content;
    // }
    //
    // return text;
    return content;
}

QVariant ModuleManager::readFromJsonAsVariant(const QString &json_path)
{
    QFile json_file(json_path);
    if (!json_file.open(QIODevice::ReadOnly)) {
        ELOG << "<m><static> cannot read json file: \"" << json_path << "\"";
        return QVariant();
    }
    QByteArray json_raw = json_file.readAll();
    json_file.close();

    QJsonParseError json_err;
    QJsonDocument json_doc = QJsonDocument::fromJson(json_raw, &json_err);
    //try to fix codec problem
    if (json_err.error == QJsonParseError::IllegalUTF8String) {
        json_doc = QJsonDocument::fromJson(convertToUtf8(json_raw).toUtf8(), &json_err);
    }
    if (json_err.error != QJsonParseError::NoError) {
        ELOG << "<m><static> read invalid json from: \"" << json_path << "\", parse error: \"" << json_err.errorString() << "\"";
    }
    return json_doc.toVariant();
}

QJsonValue ModuleManager::readFromJson(const QString &json_path)
{
    QFile json_file(json_path);
    if (!json_file.open(QIODevice::ReadOnly)) {
        ELOG << "<m><static> cannot read json file: \"" << json_path << "\"";
        return QJsonValue();
    }
    QByteArray json_raw = json_file.readAll();
    json_file.close();

    QJsonParseError json_err;
    QJsonDocument json_doc = QJsonDocument::fromJson(json_raw, &json_err);
    //try to fix codec problem
    if (json_err.error == QJsonParseError::IllegalUTF8String) {
        json_doc = QJsonDocument::fromJson(convertToUtf8(json_raw).toUtf8(), &json_err);
    }
    if (json_err.error != QJsonParseError::NoError) {
        ELOG << "<m><static> read invalid json from: \"" << json_path << "\", parse error: \"" << json_err.errorString() << "\"";
    }
    return json_doc.isObject() ? QJsonValue(json_doc.object()) : QJsonValue(json_doc.array());
}

bool ModuleManager::writeToJson(const QString &json_path, const QJsonValue &json_value, bool auto_create_path)
{
    QJsonDocument json_doc;
    if (json_value.isObject()) {
        json_doc.setObject(json_value.toObject());
    } else if (json_value.isArray()) {
        json_doc.setArray(json_value.toArray());
    } else {
        ELOG << "<m><static> writing json is neither array nor object: \"" << json_path << "\"";
        return false;
    }

    if (auto_create_path && !QFileInfo(json_path).dir().exists() && !QDir::current().mkpath(QFileInfo(json_path).dir().absolutePath())) {
        ELOG << "<m><static> cannot create foler \"" << json_path << "\" to write json";
        return false;
    }

    QFile json_file(json_path);
    if (!json_file.open(QIODevice::WriteOnly)) {
        ELOG << "<m><static> cannot write json file: \"" << json_path << "\"";
        return false;
    }
    json_file.write(json_doc.toJson());
    json_file.close();

    return true;
}

bool ModuleManager::readFromXmlFile(QObject *context, ModuleObject *mobj, const QString &xml_path)
{
    if (!mobj || mobj->isEmptyMobj()) {
        ELOG << "<m><static> try to read xml for an empty mobj";
        return false;
    }

    QFile xml_file(xml_path);
    if (!xml_file.open(QIODevice::ReadOnly)) {
        ELOG << "<m><static> cannot read xml file: \"" << xml_path << "\"";
        return false;
    }

    QXmlStreamReader xml_reader(&xml_file);
    mobj->importFromXml(context, xml_reader);

    xml_file.close();
    return true;
}

bool ModuleManager::writeToXmlFile(const QString &xml_path, ModuleObject *mobj, bool auto_create_path, bool auto_ignore)
{
    if (!mobj || mobj->isEmptyMobj()) {
        ELOG << "<m><static> try to write xml from an empty mobj";
        return false;
    }

    if (auto_create_path && !QFileInfo(xml_path).dir().exists() && !QDir::current().mkpath(QFileInfo(xml_path).dir().absolutePath())) {
        ELOG << "<m><static> cannot create path \"" << xml_path << "\" to write xml";
        return false;
    }

    QFile xml_file(xml_path);
    if (!xml_file.open(QIODevice::WriteOnly)) {
        ELOG << "<m><static> cannot write xml file: \"" << xml_path << "\"";
        return false;
    }

    QXmlStreamWriter xml_writer(&xml_file);
    xml_writer.setAutoFormatting(true);
    xml_writer.setAutoFormattingIndent(4);

    xml_writer.writeStartDocument();
    mobj->exportToXml(xml_writer, true, auto_ignore);
    xml_writer.writeEndDocument();

    xml_file.close();
    return true;
}

QByteArray ModuleManager::readFromBin(const QString &bin_path)
{
    QFile bin_file(bin_path);
    if (!bin_file.open(QIODevice::ReadOnly)) {
        ELOG << "<m><static> cannot read binary file: \"" << bin_path << "\"";
        return QByteArray();
    }
    QByteArray bin_bytes = bin_file.readAll();
    bin_file.close();
    return qUncompress(bin_bytes);
}

bool ModuleManager::writeToBin(const QString &bin_path, const QByteArray &bin_bytes, int compress_level, bool auto_create_path)
{
    if (auto_create_path && !QFileInfo(bin_path).dir().exists() && !QDir::current().mkpath(QFileInfo(bin_path).dir().absolutePath())) {
        ELOG << "<m><static> cannot create path \"" << bin_path << "\" to write binary file";
        return false;
    }

    QFile bin_file(bin_path);
    if (!bin_file.open(QIODevice::WriteOnly)) {
        ELOG << "<m><static> cannot write binary file: \"" << bin_path << "\"";
        return false;
    }

    bin_file.write(qCompress(bin_bytes, compress_level));
    bin_file.close();
    return true;
}

//output function
ModuleManager & m() { return ModuleManager::instance(); }
ModuleObject * m(const QString &rpath, ModuleObject *relative_mobj)
{ return relative_mobj ? relative_mobj->rmobj(rpath) : m().rootMobj()->rmobj(rpath); }

QString mIndex(int index, const QString &default_name)
{ return index < 0 ? default_name : QString("%1%2").arg(IMOL_MODULE_ID_START).arg(index); }
QString mName(const QString &n0, const QString &n1)
{ return n0 + IMOL_MODULE_NAME_SEPARATOR + n1; }
QString mName(const QString &n0, const QString &n1, const QString &n2)
{ return mName(n0, n1) + IMOL_MODULE_NAME_SEPARATOR + n2; }
QString mName(const QString &n0, const QString &n1, const QString &n2, const QString &n3)
{ return mName(n0, n1, n2) + IMOL_MODULE_NAME_SEPARATOR + n3; }
QString mName(const QString &n0, const QString &n1, const QString &n2, const QString &n3, const QString &n4)
{ return mName(n0, n1, n2, n3) + IMOL_MODULE_NAME_SEPARATOR + n4; }
QString mName(const QString &n0, const QString &n1, const QString &n2, const QString &n3, const QString &n4, const QString &n5)
{ return mName(n0, n1, n2, n3, n4) + IMOL_MODULE_NAME_SEPARATOR + n5; }
QString mName(const QString &n0, const QString &n1, const QString &n2, const QString &n3, const QString &n4, const QString &n5, const QString &n6)
{ return mName(n0, n1, n2, n3, n4, n5) + IMOL_MODULE_NAME_SEPARATOR + n6; }

}