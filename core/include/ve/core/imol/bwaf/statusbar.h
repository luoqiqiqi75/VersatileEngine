#ifndef IMOL_STATUSBAR_H
#define IMOL_STATUSBAR_H

#include "bwaf_global.h"

#include <QWidget>

#include "bunit.h"

class QStatusBar;

namespace bwaf {
class BWAFSHARED_EXPORT StatusBar : public QWidget, public BUnit
{
    Q_OBJECT
public:
    explicit StatusBar(QWidget *parent = nullptr);

    bool addLeft(QObject *context, const QString &item_name, QWidget *item_wgt, int order = -1);
    bool addRight(QObject *context, const QString &item_name, QWidget *item_wgt, int order = -1);
    bool removeItem(QObject *context, const QString &item_name, bool need_delete = false);

    QStatusBar * getStatusbar() const;

private:
    QStatusBar *m_statusbar;

    int m_left_count, m_right_count;
};
}

BWAFSHARED_EXPORT bwaf::StatusBar * instantiatedStatusBar(QObject *);
BWAFSHARED_EXPORT bwaf::StatusBar * instantiatedStatusBar();

#endif // IMOL_STATUSBAR_H
