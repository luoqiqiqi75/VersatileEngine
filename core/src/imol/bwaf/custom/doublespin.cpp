#include "bwaf/custom/doublespin.h"

DoubleSpin::DoubleSpin(QWidget *parent) :
    QDoubleSpinBox(parent)
{
    this->setButtonSymbols(NoButtons);
}

QString DoubleSpin::textFromValue(double val) const
{
    QString str_all = QString::number(val, 'f', decimals());
    if (decimals() == 0) return str_all;
    int str_len = str_all.length();
    for (int i = str_all.length() - 1; i > 0; i--) {
        if (str_all.at(i) == '.') {
            str_len--;
            break;
        } else if (str_all.at(i) != '0') {
            break;
        }
        str_len--;
    }
    return str_all.left(str_len);
}

void DoubleSpin::wheelEvent(QWheelEvent *)
{
    return;
}
