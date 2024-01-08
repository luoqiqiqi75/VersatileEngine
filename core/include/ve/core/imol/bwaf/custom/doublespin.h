#ifndef DOUBLE_SPIN_H
#define DOUBLE_SPIN_H

#include "../bwaf_global.h"

#include <QDoubleSpinBox>

class BWAFSHARED_EXPORT DoubleSpin : public QDoubleSpinBox
{
    Q_OBJECT

public:
    explicit DoubleSpin(QWidget *parent = nullptr);
    
    QString textFromValue(double val) const;

protected:
    void wheelEvent(QWheelEvent *) override;
};

#endif // DOUBLE_SPIN_H
