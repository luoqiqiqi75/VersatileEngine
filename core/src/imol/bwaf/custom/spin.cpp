#include "bwaf/custom/spin.h"

Spin::Spin(QWidget *parent) : QSpinBox(parent)
{

}

void Spin::wheelEvent(QWheelEvent *)
{
    return;
}
