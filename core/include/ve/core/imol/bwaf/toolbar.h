#ifndef IMOL_TOOLBAR_H
#define IMOL_TOOLBAR_H

#include "bwaf_global.h"

#include <QWidget>

#include "bunit.h"

class QTabWidget;

namespace bwaf {
class BWAFSHARED_EXPORT ToolBar : public QWidget, public BUnit
{
    Q_OBJECT

public:
    explicit ToolBar(QWidget *parent = nullptr);

    bool addLeft(QObject *context, const QString &item_name, QWidget *item_wgt, int order = -1);
    bool addRight(QObject *context, const QString &item_name, QWidget *item_wgt, int order = -1);
    bool addTab(const QString &item_name, const QString &label, QWidget *item_wgt, int order = -1);
    bool removeItem(QObject *context, const QString &item_name, bool need_delete = false);

private:
    QTabWidget *m_tab_wgt;

    int m_left_count, m_right_count;
};
}

BWAFSHARED_EXPORT bwaf::ToolBar * instantiatedToolBar(QObject *);
BWAFSHARED_EXPORT bwaf::ToolBar * instantiatedToolBar();

#endif // IMOL_TOOLBAR_H
