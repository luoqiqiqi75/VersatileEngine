#include "bwaf/toolbar.h"

#include <veCommon>

#include <QHBoxLayout>
#include <QTabWidget>

#define UNIT_NAME "toolbar"
#define MODULE_NAME "bwaf"

using namespace bwaf;
using namespace imol;

IMOL_REGISTER_BUNIT(UNIT_NAME, ToolBar)

ToolBar::ToolBar(QWidget *parent) : QWidget(parent), BUnit(UNIT_NAME, this),
    m_tab_wgt(new QTabWidget(this)),
    m_left_count(0),
    m_right_count(0)
{
    setObjectName(UNIT_NAME);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_StyledBackground);
    setAutoFillBackground(true);

    QHBoxLayout *h_layout = new QHBoxLayout;
    h_layout->setMargin(0);
    h_layout->setSpacing(0);
    h_layout->addWidget(m_tab_wgt, 1);
    setLayout(h_layout);

    m_tab_wgt->setVisible(false);
    setVisible(false);
}

bool ToolBar::addLeft(QObject *context, const QString &item_name, QWidget *item_wgt, int order)
{
    if (hasItem(item_name)) return false;
    setVisible(true);

    QHBoxLayout *h_layout = qobject_cast<QHBoxLayout *>(layout());
    int index = (order < 0) ? m_left_count : order;
    h_layout->insertWidget(index, item_wgt);
    m_left_count++;

    if (!insertItemObj(context, item_name, item_wgt)) return false;
    itemMobj(item_name)->set(context, "align", "left");
    itemMobj(item_name)->set(context, "index", index);
    itemMobj(item_name)->set(context, "type", "widget");

    return true;
}

bool ToolBar::addRight(QObject *context, const QString &item_name, QWidget *item_wgt, int order)
{
    if (hasItem(item_name)) return false;
    if (itemCount() == 0) setVisible(true);

    QHBoxLayout *h_layout = qobject_cast<QHBoxLayout *>(layout());
    int index = (order < 0) ? m_left_count + 1 : m_left_count + m_right_count + 1 - order;
    h_layout->insertWidget(index, item_wgt);
    m_right_count++;

    if (!insertItemObj(context, item_name, item_wgt)) return false;
    itemMobj(item_name)->set(context, "align", "right");
    itemMobj(item_name)->set(context, "index", index);
    itemMobj(item_name)->set(context, "type", "widget");

    return true;
}

bool ToolBar::addTab(const QString &item_name, const QString &label, QWidget *item_wgt, int order)
{
    Q_UNUSED(item_name)
    Q_UNUSED(label)
    Q_UNUSED(item_wgt)
    Q_UNUSED(order)
    setVisible(true);
    m_tab_wgt->setVisible(true);

    return true;
}

bool ToolBar::removeItem(QObject *context, const QString &item_name, bool need_delete)
{
    QWidget *item_wgt = getItemWgt(item_name);
    if (!item_wgt) return false;

    imol::ModuleObject *item_mobj = itemMobj(item_name);
    QString item_type = item_mobj->cmobj("type")->getString();
    if (item_type == "tab") {
        m_tab_wgt->removeTab(m_tab_wgt->indexOf(item_wgt));
    } else if (item_type == "widget") {
        layout()->removeWidget(item_wgt);
        QString item_align = item_mobj->cmobj("align")->getString();
        if (item_align == "left") {
            m_left_count--;
        } else if (item_align == "right") {
            m_right_count--;
        }
    } else {
        return false;
    }
    bool success = removeItemObj(context, item_name, need_delete);
    if (itemCount() == 0) this->setVisible(false);
    return success;
}

ToolBar * instantiatedToolBar(QObject *)
{
    return m(mName(MODULE_NAME, UNIT_NAME))->get().value<ToolBar *>();
}

ToolBar * instantiatedToolBar()
{
    return m(mName(MODULE_NAME, UNIT_NAME))->get().value<ToolBar *>();
}
