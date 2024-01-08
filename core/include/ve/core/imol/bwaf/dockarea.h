#ifndef IMOL_DOCKAREA_H
#define IMOL_DOCKAREA_H

#include "bwaf_global.h"

#include <QMainWindow>
#include <QWidget>

#include "bunit.h"

namespace bwaf {
class BWAFSHARED_EXPORT DockArea : public QMainWindow, public BUnit
{
    Q_OBJECT

public:
    explicit DockArea(QWidget *parent = nullptr);

    enum DockOrientation {
        HORIZONTAL,
        VERTICAL,
        TABBED
    };

    bool addDockPage(QObject *context, const QString &item_name, QWidget *wgt, const QString &title,
                     DockOrientation orientation = TABBED, Qt::DockWidgetArea q_dock_area = Qt::TopDockWidgetArea);
    bool switchTo(QObject *context, const QString &item_name);
    bool removeItem(QObject *context, const QString &item_name, bool need_delete = false);
};
}

BWAFSHARED_EXPORT bwaf::DockArea * instantiatedDockArea();

#endif // IMOL_DOCKAREA_H
