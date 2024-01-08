#ifndef IMOL_TOPMENU_H
#define IMOL_TOPMENU_H

#include "bwaf_global.h"

#include <QWidget>

#include "bunit.h"

class QMenuBar;
class QMenu;

namespace bwaf {
class BWAFSHARED_EXPORT TopMenu : public QWidget, public BUnit
{
    Q_OBJECT

public:
    explicit TopMenu(QWidget *parent = nullptr);

    bool addLeft(QObject *context, const QString &item_name, QWidget *item_wgt, int order = -1);
    bool addLeft(QObject *context, QWidget *item_wgt, int order = -1);
    bool addRight(QObject *context, const QString &item_name, QWidget *item_wgt, int order = -1);
    bool addRight(QObject *context, QWidget *item_wgt, int order = -1);

    bool addMenu(QObject *context, const QString &item_name, const QString &menu_label, QAction *action);
    bool addSeparator(QObject *context, const QString &item_name, const QString &menu_label);

    bool removeItem(QObject *context, const QString &item_name, bool need_delete = false);

private:
    QHash<QString, QMenu *> m_menus;

    QMenuBar *m_menubar;

    int m_left_count, m_right_count;
};
}

BWAFSHARED_EXPORT bwaf::TopMenu * instantiatedTopMenu(QObject *);
BWAFSHARED_EXPORT bwaf::TopMenu * instantiatedTopMenu();

#endif // IMOL_TOPMENU_H
