#ifndef IMOL_SIDEBAR_H
#define IMOL_SIDEBAR_H

#include "bwaf_global.h"

#include <QWidget>

#include "bunit.h"

namespace bwaf {
class BWAFSHARED_EXPORT SideBar : public QWidget, public BUnit
{
    Q_OBJECT

public:
    explicit SideBar(bool is_left, QWidget *parent = nullptr);

    bool addItem(QObject *context, const QString &item_name, QWidget *item_wgt, bool is_upper = true, int order = -1);
    bool removeItem(QObject *context, const QString &item_name, bool need_delete = false);

private:
    bool m_is_left;

    int m_top_count, m_bottom_count;
};
}

BWAFSHARED_EXPORT bwaf::SideBar * instantiatedSideBar(QObject *, bool is_left);
BWAFSHARED_EXPORT bwaf::SideBar * instantiatedSideBar(bool is_left);

#endif // IMOL_SIDEBAR_H
