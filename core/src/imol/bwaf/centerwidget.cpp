#include "bwaf/centerwidget.h"

#include <veCommon>

#include <QTabWidget>
#include <QResizeEvent>

#define UNIT_NAME "centerwidget"
#define MODULE_NAME "bwaf"

using namespace bwaf;
using namespace imol;

IMOL_REGISTER_BUNIT(UNIT_NAME, CenterWidget)

CenterWidget::CenterWidget(QWidget *parent) : QStackedWidget(parent), BUnit(UNIT_NAME, this),
    m_tab_wgt(new QTabWidget)
{
    this->setObjectName(UNIT_NAME);
    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    this->setAttribute(Qt::WA_StyledBackground);
    this->setAutoFillBackground(true);

    mobj()->set(this, QVariant::fromValue<CenterWidget *>(this));

    this->addPage(this, "main_tab", m_tab_wgt);
    mobj()->set(this, "current", "main_tab");

    mobj()->set(this, "size.width", width());
    mobj()->set(this, "size.height", height());

    connect(m_tab_wgt, &QTabWidget::tabCloseRequested, this, &CenterWidget::closeTab);
}

bool CenterWidget::addPage(QObject *context, const QString &item_name, QWidget *item_wgt)
{
    if (hasItem(item_name)) return false;
    if (!insertItemObj(context, item_name, item_wgt)) return false;
    itemMobj(item_name)->set(context, "type", "page");

    this->addWidget(item_wgt);

    return true;
}

bool CenterWidget::addTab(QObject *context, const QString &item_name, QWidget *item_wgt, const QString &label, int order)
{
    if (hasItem(item_name)) return false;
    if (!insertItemObj(context, item_name, item_wgt)) return false;
    itemMobj(item_name)->set(context, "type", "tab");
    itemMobj(item_name)->set(context, "order", order);

    getTabWidget()->insertTab(order > 0 ? order : m_tab_wgt->count(), item_wgt, label);

    return true;
}

bool CenterWidget::switchTo(QObject *context, const QString &item_name)
{
    QWidget *wgt = getItemWgt(item_name);
    if (!wgt) return false;

    QString item_type = itemMobj(item_name)->cmobj("type")->getString();
    if (item_type == "tab") {
        setCurrentWidget(m_tab_wgt);
        m_tab_wgt->setCurrentWidget(wgt);
    } else if (item_type == "page") {
        setCurrentWidget(wgt);
    } else {
        return false;
    }
    mobj()->set(context, "current", item_name);
    return true;
}

bool CenterWidget::removeItem(QObject *context, const QString &item_name, bool need_delete)
{
    QWidget *wgt = getItemWgt(item_name);
    if (!wgt) return false;

    QString item_type = itemMobj(item_name)->cmobj("type")->getString();
    if (item_type == "tab") {
        m_tab_wgt->removeTab(m_tab_wgt->indexOf(wgt));
    } else if (item_type == "page") {
        removeWidget(wgt);
    } else {
        return false;
    }
    return removeItemObj(context, item_name, need_delete);
}

QTabWidget * CenterWidget::getTabWidget() const
{
    return m_tab_wgt;
}

void CenterWidget::resizeEvent(QResizeEvent *event)
{
    mobj()->set(this, "size.width", width());
    mobj()->set(this, "size.height", height());
    mobj()->set(this, "size", QVariant());

    return QWidget::resizeEvent(event);
}

void CenterWidget::closeTab(int index)
{
    QWidget *tab_wgt = getTabWidget()->widget(index);
    if (!tab_wgt) return;
    foreach (imol::ModuleObject *item_mobj, mobj()->cmobj("item")->cmobjs()) {
        if (item_mobj->get().value<QWidget *>() == tab_wgt) {
            removeItem(this, item_mobj->name(), true);
            return;
        }
    }
}

CenterWidget * instantiatedCenterWidget(QObject *)
{
    return m(mName(MODULE_NAME, UNIT_NAME))->get().value<CenterWidget *>();
}

CenterWidget * instantiatedCenterWidget()
{
    return m(mName(MODULE_NAME, UNIT_NAME))->get().value<CenterWidget *>();
}

