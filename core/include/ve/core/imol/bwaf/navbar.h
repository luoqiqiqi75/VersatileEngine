#ifndef IMOL_NAVBAR_H
#define IMOL_NAVBAR_H

#include "bwaf_global.h"

#include <QWidget>

#include "bunit.h"

namespace Ui {
class NavBar;
}
class QVBoxLayout;
namespace bwaf {
class BWAFSHARED_EXPORT NavBar : public QWidget, public BUnit
{
    Q_OBJECT

public:
    explicit NavBar(QWidget *parent = nullptr);
    ~NavBar();

    bool addItem(QObject* context, const QString &item_name, QWidget *item_wgt, bool is_upper = true, int order = -1);
    bool removeItem(QObject *context, const QString &item_name, bool need_delete = false);

private:
    Ui::NavBar *ui;

    int m_top_count, m_bottom_count;
    QVBoxLayout *m_upper_layout;
    QVBoxLayout *m_lower_layout;
};
}

BWAFSHARED_EXPORT bwaf::NavBar * instantiatedNavBar(QObject *);
BWAFSHARED_EXPORT bwaf::NavBar * instantiatedNavBar();

#endif // IMOL_NAVBAR_H
