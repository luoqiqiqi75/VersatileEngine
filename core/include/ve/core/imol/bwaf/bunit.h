#ifndef IMOL_BUNIT_H
#define IMOL_BUNIT_H

#include "core/creatormanager.h"

#include <QStringList>
#include <QHash>

#include "bwaf_global.h"

class QWidget;
class QObject;

namespace imol { class ModuleObject; }
namespace bwaf {
class BWAFSHARED_EXPORT BUnit
{
public:
    BUnit(const QString &unit_name, QWidget *widget);
    virtual ~BUnit();

    imol::ModuleObject * mobj() const;

    static QString itemRname(const QString &item_name);

    imol::ModuleObject * itemMobj(const QString &item_name) const;
    bool hasItem(const QString &item_name) const;
    int itemCount() const;
    QStringList itemNames() const;

    QWidget * getItemWgt(const QString &item_name);

protected:
    QObject * getItemObj(const QString &item_name);

    bool insertItemObj(QObject *context, const QString &item_name, QObject *item_obj);
    bool removeItemObj(QObject *context, const QString &item_name, bool auto_delete = true);

private:
    QObject *m_context;
    imol::ModuleObject *m_mobj;
};

typedef IMOL_CREATOR_TYPE(QWidget *, (QWidget *)) BUnitCreator;
BWAFSHARED_EXPORT BUnitCreator & bUnitCreator();
}

#define IMOL_REGISTER_BUNIT(KEY, CLASS_NAME) IMOL_AUTO_RUN(bwaf::bUnitCreator().regist(KEY, [] (QWidget *parent) -> QWidget * { return new CLASS_NAME(parent); }))

#endif // IMOL_BUNIT_H
