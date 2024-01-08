#ifndef IMOL_CENTERWIDGET_H
#define IMOL_CENTERWIDGET_H

#include "bwaf_global.h"

#include <QStackedWidget>

#include "bunit.h"

class QTabWidget;

namespace bwaf {
class BWAFSHARED_EXPORT CenterWidget : public QStackedWidget, public BUnit
{
    Q_OBJECT

public:
    explicit CenterWidget(QWidget *parent = nullptr);

    bool addPage(QObject *context, const QString &item_name, QWidget *item_wgt);
    bool addTab(QObject *context, const QString &item_name, QWidget *item_wgt, const QString &label, int order = -1);
    bool switchTo(QObject *context, const QString &item_name);
    bool removeItem(QObject *context, const QString &item_name, bool need_delete = false);

    QTabWidget * getTabWidget() const;

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void closeTab(int index);

private:
    QTabWidget *m_tab_wgt;
};
}

BWAFSHARED_EXPORT bwaf::CenterWidget * instantiatedCenterWidget(QObject *);
BWAFSHARED_EXPORT bwaf::CenterWidget * instantiatedCenterWidget();

#endif // IMOL_CENTERWIDGET_H
