#include "bwaf/topmenu.h"

#include <veCommon>

#include <QHBoxLayout>
#include <QMenuBar>

#include <QPushButton>

#define UNIT_NAME "topmenu"
#define MODULE_NAME "bwaf"

using namespace bwaf;
using namespace imol;

IMOL_REGISTER_BUNIT(UNIT_NAME, TopMenu)

TopMenu::TopMenu(QWidget *parent) : QWidget(parent), BUnit(UNIT_NAME, this),
    m_menubar(new QMenuBar(this)),
    m_left_count(0),
    m_right_count(0)
{
    this->setObjectName(UNIT_NAME);
    this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    this->setAttribute(Qt::WA_StyledBackground);
    this->setAutoFillBackground(true);

    QHBoxLayout *h_layout = new QHBoxLayout;
    h_layout->setMargin(0);
    h_layout->setSpacing(0);
    h_layout->addWidget(m_menubar);
    this->setLayout(h_layout);

    m_menubar->setNativeMenuBar(false);
    m_menubar->setVisible(false);
    m_menubar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    this->setVisible(false);
}

bool TopMenu::addLeft(QObject *context, const QString &item_name, QWidget *item_wgt, int order)
{
    if (hasItem(item_name)) return false;
    if (itemCount() == 0) this->setVisible(true);

    QHBoxLayout *h_layout = qobject_cast<QHBoxLayout *>(layout());
    int index = (order < 0) ? m_left_count : order;
    h_layout->insertWidget(index, item_wgt);
    m_left_count++;

    if (!insertItemObj(context, item_name, item_wgt)) return false;
    itemMobj(item_name)->set(context, "align", "left");
    itemMobj(item_name)->set(context, "index", index);

    return true;
}

bool TopMenu::addLeft(QObject *context, QWidget *item_wgt, int order)
{
    return addLeft(context, item_wgt->objectName(), item_wgt, order);
}

bool TopMenu::addRight(QObject *context, const QString &item_name, QWidget *item_wgt, int order)
{
    if (hasItem(item_name)) return false;
    if (itemCount() == 0) this->setVisible(true);

    QHBoxLayout *h_layout = qobject_cast<QHBoxLayout *>(layout());
    int index = (order < 0) ? m_left_count + 1 : m_left_count + m_right_count + 1 - order;
    h_layout->insertWidget(index, item_wgt);
    m_right_count++;

    if (!insertItemObj(context, item_name, item_wgt)) return false;
    itemMobj(item_name)->set(context, "align", "right");
    itemMobj(item_name)->set(context, "index", index);

    return true;
}

bool TopMenu::addRight(QObject *context, QWidget *item_wgt, int order)
{
    return addRight(context, item_wgt->objectName(), item_wgt, order);
}

bool TopMenu::addMenu(QObject *context, const QString &item_name, const QString &menu_label, QAction *action)
{
    if (hasItem(item_name)) return false;
    if (itemCount() == 0) this->setVisible(true);
    if (m_menus.size() == 0) m_menubar->setVisible(true);

    QString menu_name = "menu_" + menu_label;
    QMenu *menu = m_menus.value(menu_name, nullptr);
    if (!menu) {
        menu = m_menubar->addMenu(menu_label);
        m_menus.insert(menu_name, menu);
    }
    menu->addAction(action);

    if (!insertItemObj(context, item_name, action)) return false;
    itemMobj(item_name)->set(context, "align", "menu");
    itemMobj(item_name)->set(context, "menu_name", menu_name);

    return true;
}

bool TopMenu::addSeparator(QObject *context, const QString &item_name, const QString &menu_label)
{
    if (hasItem(item_name)) return false;
    if (itemCount() == 0) this->setVisible(true);
    if (m_menus.size() == 0) m_menubar->setVisible(true);

    QString menu_name = "menu_" + menu_label;
    QMenu *menu = m_menus.value(menu_name, nullptr);
    if (!menu) {
        menu = m_menubar->addMenu(menu_label);
        m_menus.insert(menu_name, menu);
    }

    if (!insertItemObj(context, item_name, menu->addSeparator())) return false;
    itemMobj(item_name)->set(context, "align", "menu");
    itemMobj(item_name)->set(context, "menu_name", menu_name);

    return true;
}

bool TopMenu::removeItem(QObject *context, const QString &item_name, bool need_delete)
{
    if (!hasItem(item_name)) return false;

    imol::ModuleObject *item_mobj = itemMobj(item_name);
    QString align = item_mobj->cmobj("align")->getString();
    if (align == "menu") {
        QString menu_name = item_mobj->cmobj("menu_name")->getString();
        QMenu *menu = m_menus.value(menu_name, nullptr);
        if (!menu) return false;
        QAction *item_act = qobject_cast<QAction *>(getItemObj(item_name));
        if (!item_act || !removeItemObj(context, item_name, need_delete)) return false;
        if (menu->isEmpty()) {
            menu->setVisible(false);
            menu->deleteLater();
            m_menus.remove(menu_name);
            if (m_menus.isEmpty()) m_menubar->setVisible(false);
        }
    } else {
        QWidget *item_wgt = getItemWgt(item_name);
        if (item_wgt) layout()->removeWidget(item_wgt);
        if (!item_wgt || !removeItemObj(context, item_name, need_delete)) return false;

        if (align == "left") {
            m_left_count--;
        } else if (align == "right") {
            m_right_count--;
        }
    }
    if (itemCount() == 0) this->setVisible(false);
    return true;
}

TopMenu * instantiatedTopMenu(QObject *)
{
    return m(mName(MODULE_NAME, UNIT_NAME))->get().value<TopMenu *>();
}

TopMenu * instantiatedTopMenu()
{
    return m(mName(MODULE_NAME, UNIT_NAME))->get().value<TopMenu *>();
}
