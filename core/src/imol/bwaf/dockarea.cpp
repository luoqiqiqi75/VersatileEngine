#include "bwaf/dockarea.h"

#include <veCommon>

#include <QDockWidget>

#define UNIT_NAME "dockarea"
#define MODULE_NAME "bwaf"

using namespace bwaf;
using namespace imol;

IMOL_REGISTER_BUNIT(UNIT_NAME, DockArea)

DockArea::DockArea(QWidget *parent) : QMainWindow(parent), BUnit(UNIT_NAME, this)
{
    setObjectName(UNIT_NAME);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    mobj()->set(this, QVariant::fromValue<DockArea *>(this));
}

bool DockArea::addDockPage(QObject *context, const QString &item_name, QWidget *wgt, const QString &title, DockOrientation orientation, Qt::DockWidgetArea q_dock_area)
{
    if (hasItem(item_name)) return false;

    QDockWidget *dock_wgt = new QDockWidget(title, this);
    dock_wgt->setWidget(wgt);
    dock_wgt->setAttribute(Qt::WA_DeleteOnClose);
    connect(dock_wgt, &QDockWidget::destroyed, this, [=] {removeItemObj(context, item_name, false);});

    addDockWidget(q_dock_area, dock_wgt, orientation == VERTICAL ? Qt::Vertical : Qt::Horizontal);

    switch (orientation) {
    case HORIZONTAL:  break;
    case VERTICAL: break;
    case TABBED: if (itemCount() > 0) tabifyDockWidget(qobject_cast<QDockWidget *>(getItemWgt(itemNames().last())), dock_wgt); break;
    default: return false;
    }

    if (!insertItemObj(context, item_name, dock_wgt)) {
        removeDockWidget(dock_wgt);
        delete dock_wgt;
        return false;
    }

    return true;
}

bool DockArea::switchTo(QObject *context, const QString &item_name)
{
    QDockWidget *dock_wgt = qobject_cast<QDockWidget *>(getItemWgt(item_name));
    if (!dock_wgt) return false;

    dock_wgt->show();
    dock_wgt->raise();

    mobj()->set(context, "current", item_name);
    return true;
}

bool DockArea::removeItem(QObject *context, const QString &item_name, bool need_delete)
{
    if (!getItemWgt(item_name)) return false;
    return removeItemObj(context, item_name, need_delete);
}

DockArea * instantiatedDockArea()
{
    return m(mName(MODULE_NAME, UNIT_NAME))->get().value<DockArea *>();
}
