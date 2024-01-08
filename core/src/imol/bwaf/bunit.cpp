#include "bwaf/bunit.h"

#include <veCommon>

#include <QObject>
#include <QWidget>

#define MODULE_NAME "bwaf"

using namespace imol;

namespace bwaf {

BUnit::BUnit(const QString &unit_name, QWidget *widget):
    m_context(widget)
{
    QString full_name = mName(MODULE_NAME, unit_name);
    m().regist(widget, full_name);

    m_mobj = m(full_name);
    m_mobj->set(widget, QVariant::fromValue<QWidget *>(widget));
    m_mobj->insert(widget, "item");
    m_mobj->watch(true);
}

BUnit::~BUnit()
{
    m().cancel(m_context, m_mobj->fullName());
    m_mobj = m().emptyMobj();
}

imol::ModuleObject * BUnit::mobj() const { return m_mobj; }

QString BUnit::itemRname(const QString &item_name)
{
    return mName("item", item_name);
}

imol::ModuleObject * BUnit::itemMobj(const QString &item_name) const
{
    return mobj()->rmobj(itemRname(item_name));
}

bool BUnit::hasItem(const QString &item_name) const
{
    return mobj()->hasRmobj(itemRname(item_name));
}

int BUnit::itemCount() const
{
    return mobj()->cmobj("item")->cmobjCount();
}

QStringList BUnit::itemNames() const
{
    return mobj()->cmobj("item")->cmobjNames();
}

QWidget * BUnit::getItemWgt(const QString &item_name)
{
    return qobject_cast<QWidget *>(getItemObj(item_name));
}

QObject * BUnit::getItemObj(const QString &item_name)
{
    return itemMobj(item_name)->get().value<QObject *>();
}

bool BUnit::insertItemObj(QObject *context, const QString &item_name, QObject *item_obj)
{
    if (hasItem(item_name)) return false;
    mobj()->set(context, itemRname(item_name), QVariant::fromValue<QObject *>(item_obj));
    return true;
}

bool BUnit::removeItemObj(QObject *context, const QString &item_name, bool auto_delete)
{
    if (!hasItem(item_name)) return false;
    if (auto_delete) delete getItemObj(item_name);
    return mobj()->remove(context, itemRname(item_name));
}

BUnitCreator & bUnitCreator()
{
    static BUnitCreator mgr;
    return mgr;
}

}
