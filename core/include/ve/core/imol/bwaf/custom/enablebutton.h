#ifndef ENABLEBUTTON_H
#define ENABLEBUTTON_H

#include "../bwaf_global.h"

#include <QWidget>

class QPropertyAnimation;

class BWAFSHARED_EXPORT EnableButton : public QWidget
{
    Q_OBJECT
public:
    explicit EnableButton(QWidget *parent = nullptr);
    ~EnableButton() override;

    void setStyleSheet(const QString &str);

    bool getIsEnabled();
    void setIsEnabled(bool is_enabled, bool force_animation = false);

signals:
    void enabled(bool is_enable);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void moveSlider(const QVariant &value, bool need_update = true);

private:
    QPropertyAnimation *m_change_ani;

    //slider cycle position
    int m_slider_pos;
    //last width
    int m_old_w;

    bool m_is_enable;
    //style settings
    QColor m_en_bg_color, m_dis_bg_color;
    QColor m_en_f_color, m_dis_f_color;
    int m_padding_left, m_padding_top, m_padding_right, m_padding_bottom;
};

#endif // ENABLEBUTTON_H
