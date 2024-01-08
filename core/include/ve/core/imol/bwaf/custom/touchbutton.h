#ifndef TOUCHBUTTON_H
#define TOUCHBUTTON_H

#include "../bwaf_global.h"

#include <QPushButton>

class BWAFSHARED_EXPORT TouchButton : public QPushButton
{
    Q_OBJECT

public:
    TouchButton(QWidget *parent = nullptr);

signals:
    void touchBegun();
    void touchEnded();

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    bool m_is_triggered;
};

#endif // TOUCHBUTTON_H
