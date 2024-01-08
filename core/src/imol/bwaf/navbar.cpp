#include "bwaf/navbar.h"
#include "ui_navbar.h"

#include <veCommon>

#include <QVBoxLayout>
#include <QTabWidget>
#include <QScrollArea>
#include <QScroller>

#define UNIT_NAME "navbar"
#define MODULE_NAME "bwaf"

using namespace bwaf;
using namespace imol;

IMOL_REGISTER_BUNIT(UNIT_NAME, NavBar)

NavBar::NavBar(QWidget *parent) : QWidget(parent), BUnit(UNIT_NAME, this),
    ui(new Ui::NavBar),
    m_top_count(0),
    m_bottom_count(0)
{
    ui->setupUi(this);

    this->setObjectName(UNIT_NAME);
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    this->setAttribute(Qt::WA_StyledBackground);
    this->setAutoFillBackground(true);

    m_upper_layout = new QVBoxLayout(ui->wgtUpper);
    m_upper_layout->setMargin(0);
    m_upper_layout->setSpacing(0);
    m_upper_layout->addStretch();
    ui->wgtUpper->setLayout(m_upper_layout);
    ui->wgtUpper->setObjectName(UNIT_NAME);

    m_lower_layout = new QVBoxLayout(ui->wgtLower);
    m_lower_layout->setMargin(0);
    m_lower_layout->setSpacing(0);
    ui->wgtLower->setLayout(m_lower_layout);

    QScroller::grabGesture(ui->scrollArea, QScroller::LeftMouseButtonGesture);

    this->setVisible(false);
}

NavBar::~NavBar()
{
    delete ui;
}

bool NavBar::addItem(QObject *context, const QString &item_name, QWidget *item_wgt, bool is_upper, int order)
{
    if (hasItem(item_name)) return false;

    if (this->itemCount() == 0) this->setVisible(true);
    if (is_upper) {
        m_upper_layout->insertWidget((order < 0) ? m_top_count : order, item_wgt);
    } else {
        m_lower_layout->insertWidget((order < 0) ? m_bottom_count + 1 : m_bottom_count + 1 - order, item_wgt);
    }
//    m_upper_wgt->resize(m_upper_wgt->sizeHint());
    if (is_upper) m_top_count++; else m_bottom_count++;

    if (!insertItemObj(context, item_name, item_wgt)) return false;
    itemMobj(item_name)->set(context, "align", is_upper ? "top" : "bottom");
    itemMobj(item_name)->set(context, "order", order);

    return true;
}

bool NavBar::removeItem(QObject *context, const QString &item_name, bool need_delete)
{
    QWidget *wgt = getItemWgt(item_name);
    if (!wgt) return false;

    QString item_align = itemMobj(item_name)->cmobj("align")->getString();
    if (item_align == "top") {
        m_top_count--;
        m_upper_layout->removeWidget(wgt);
    } else if (item_align == "bottom") {
        m_bottom_count--;
        m_lower_layout->removeWidget(wgt);
    }

    if (!removeItemObj(context, item_name, need_delete)) return false;

    if (this->itemCount() == 0) this->setVisible(false);
    return true;
}

NavBar * instantiatedNavBar(QObject *)
{
    return m(mName(MODULE_NAME, UNIT_NAME))->get().value<NavBar *>();
}

NavBar * instantiatedNavBar()
{
    return m(mName(MODULE_NAME, UNIT_NAME))->get().value<NavBar *>();
}
