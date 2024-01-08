#ifndef SPIN_H
#define SPIN_H

#include "../bwaf_global.h"

#include <QSpinBox>

class BWAFSHARED_EXPORT Spin : public QSpinBox
{
    Q_OBJECT

public:
    explicit Spin(QWidget *parent = nullptr);

protected:
    void wheelEvent(QWheelEvent *) override;
};

#endif // SPIN_H
