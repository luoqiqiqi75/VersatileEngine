#include "bwaf/sidebar.h"

#include <veCommon>

#include <QVBoxLayout>

#define UNIT_NAME "sidebar"
#define MODULE_NAME "bwaf"

using namespace bwaf;
using namespace imol;

IMOL_AUTO_RUN(bUnitCreator().regist("leftsidebar", [] (QWidget *parent) -> QWidget * { return new SideBar(true, parent); }));
IMOL_AUTO_RUN(bUnitCreator().regist("rightsidebar", [] (QWidget *parent) -> QWidget * { return new SideBar(false, parent); }));

SideBar::SideBar(bool is_left, QWidget *parent) : QWidget(parent), BUnit(QString("%1%2").arg(is_left ? "left" : "right", UNIT_NAME), this),
    m_is_left(is_left),
    m_top_count(0),
    m_bottom_count(0)
{
    this->setObjectName(mobj()->name());
    this->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    this->setAttribute(Qt::WA_StyledBackground);
    this->setAutoFillBackground(true);

    QVBoxLayout *v_layout = new QVBoxLayout;
    v_layout->setMargin(0);
    v_layout->setSpacing(0);
    this->setLayout(v_layout);

    this->setVisible(false);
}

bool SideBar::addItem(QObject *context, const QString &item_name, QWidget *item_wgt, bool is_upper, int order)
{
    if (hasItem(item_name)) return false;

    if (this->itemCount() == 0) this->setVisible(true);
    QVBoxLayout *v_layout = qobject_cast<QVBoxLayout *>(layout());
    int index = (order < 0) ? m_top_count : order;
    if (!is_upper) index = (order < 0) ? m_top_count : m_top_count + m_bottom_count - order;
    v_layout->insertWidget(index, item_wgt);
    if (is_upper) m_top_count++; else m_bottom_count++;

    if (!insertItemObj(context, item_name, item_wgt)) return false;
    itemMobj(item_name)->set(context, "align", is_upper ? "top" : "bottom");
    itemMobj(item_name)->set(context, "order", order);

    return true;
}

bool SideBar::removeItem(QObject *context, const QString &item_name, bool need_delete)
{
    QWidget *wgt = getItemWgt(item_name);
    if (!wgt) return false;

    QString item_align = itemMobj(item_name)->cmobj("align")->getString();
    if (item_align == "top") {
        m_top_count--;
    } else if (item_align == "bottom") {
        m_bottom_count--;
    }

    QVBoxLayout *v_layout = qobject_cast<QVBoxLayout *>(layout());
    v_layout->removeWidget(wgt);

    if (!removeItemObj(context, item_name, need_delete)) return false;

    if (this->itemCount() == 0) this->setVisible(false);
    return true;
}

SideBar * instantiatedSideBar(QObject *, bool is_left)
{
    return m(mName(MODULE_NAME, QString("%1%2").arg(is_left ? "left" : "right", UNIT_NAME)))->get().value<SideBar *>();
}

SideBar * instantiatedSideBar(bool is_left)
{
    return m(mName(MODULE_NAME, QString("%1%2").arg(is_left ? "left" : "right", UNIT_NAME)))->get().value<SideBar *>();
}
