#include "bwaf/statusbar.h"

#include <veCommon>

#include <QHBoxLayout>
#include <QStatusBar>

#define UNIT_NAME "statusbar"
#define MODULE_NAME "bwaf"

using namespace bwaf;
using namespace imol;

IMOL_REGISTER_BUNIT(UNIT_NAME, StatusBar)

StatusBar::StatusBar(QWidget *parent) : QWidget(parent), BUnit(UNIT_NAME, this),
    m_statusbar(new QStatusBar(this)),
    m_left_count(0),
    m_right_count(0)
{
    this->setObjectName(UNIT_NAME);
    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    this->setAttribute(Qt::WA_StyledBackground);
    this->setAutoFillBackground(true);

    QHBoxLayout *h_layout = new QHBoxLayout;
    h_layout->setMargin(0);
    h_layout->setSpacing(0);
    h_layout->addWidget(m_statusbar, 1);
    this->setLayout(h_layout);
}

bool StatusBar::addLeft(QObject *context, const QString &item_name, QWidget *item_wgt, int order)
{
    if (hasItem(item_name)) return false;

    QHBoxLayout *h_layout = qobject_cast<QHBoxLayout *>(layout());
    int index = (order < 0) ? m_left_count : order;
    h_layout->insertWidget(index, item_wgt);
    m_left_count++;

    if (!insertItemObj(context, item_name, item_wgt)) return false;
    itemMobj(item_name)->set(context, "align", "left");
    itemMobj(item_name)->set(context, "index", index);

    return true;
}

bool StatusBar::addRight(QObject *context, const QString &item_name, QWidget *item_wgt, int order)
{
    if (hasItem(item_name)) return false;

    QHBoxLayout *h_layout = qobject_cast<QHBoxLayout *>(layout());
    int index = (order < 0) ? m_left_count + 1 : m_left_count + m_right_count + 1 - order;
    h_layout->insertWidget(index, item_wgt);
    m_right_count++;

    if (!insertItemObj(context, item_name, item_wgt)) return false;
    itemMobj(item_name)->set(context, "align", "right");
    itemMobj(item_name)->set(context, "index", index);

    return true;
}

bool StatusBar::removeItem(QObject *context, const QString &item_name, bool need_delete)
{
    QWidget *wgt = getItemWgt(item_name);
    if (!wgt) return false;

    QString item_align = itemMobj(item_name)->cmobj("align")->getString();
    if (item_align == "left") {
        m_left_count--;
    } else if (item_align == "right") {
        m_right_count--;
    }

    QHBoxLayout *h_layout = qobject_cast<QHBoxLayout *>(layout());
    h_layout->removeWidget(wgt);

    if (!removeItemObj(context, item_name, need_delete)) return false;

    if (this->itemCount() == 0 && !m_statusbar->isVisible()) this->setVisible(false);
    return true;
}

QStatusBar * StatusBar::getStatusbar() const
{
    return m_statusbar;
}

StatusBar * instantiatedStatusBar(QObject *)
{
    return m(mName(MODULE_NAME, UNIT_NAME))->get().value<StatusBar *>();
}

StatusBar * instantiatedStatusBar()
{
    return m(mName(MODULE_NAME, UNIT_NAME))->get().value<StatusBar *>();
}
