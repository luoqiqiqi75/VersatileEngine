#include "bwaf/custom/enablebutton.h"

#include <QPropertyAnimation>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOption>
#include <QDebug>

#define MAX_VALUE 25
#define DURATION 200

EnableButton::EnableButton(QWidget *parent) : QWidget(parent),
    m_change_ani(new QPropertyAnimation(this, "")),
    m_slider_pos(0),
    m_old_w(0),
    m_is_enable(false),
    m_en_bg_color(QColor(141, 185, 244)),
    m_dis_bg_color(QColor(189, 193, 198)),
    m_en_f_color(QColor(26, 115, 232)),
    m_dis_f_color(QColor(221, 221, 221)),
    m_padding_left(0),
    m_padding_top(0),
    m_padding_right(0),
    m_padding_bottom(0)
{
    m_old_w = width();

    m_change_ani->setStartValue(0);
    m_change_ani->setEndValue(MAX_VALUE);
    m_change_ani->setDuration(DURATION);
    m_change_ani->setCurrentTime(DURATION);
    connect(m_change_ani, SIGNAL(valueChanged(QVariant)), this, SLOT(moveSlider(QVariant)));
}

EnableButton::~EnableButton()
{
    delete m_change_ani;
}

QColor strToColor(const QString &str, const QColor &defaultColor)
{
    if (str.length() < 7) return defaultColor;
    int r = str.mid(1, 2).toInt(nullptr, 16);
    int g = str.mid(3, 2).toInt(nullptr, 16);
    int b = str.mid(5, 2).toInt(nullptr, 16);
    int a = str.size() >= 9 ? str.mid(7, 2).toInt(nullptr, 16) : 255;
    return QColor(r, g, b, a);
}

void EnableButton::setStyleSheet(const QString &str)
{
    QStringList style_strs = str.split(";");
    foreach (QString style_str, style_strs) {
        QStringList keyValue = style_str.split(":");
        if (keyValue.size() != 2) continue;
        QString key = keyValue.at(0);
        QString value = keyValue.at(1);
        if (value.endsWith("px")) value = value.left(value.size() - 2);
        if (key == "padding-left") {
            m_padding_left = qRound(value.toDouble());
        } else if (key == "padding-top") {
            m_padding_top = qRound(value.toDouble());
        } else if (key == "padding-right") {
            m_padding_right = qRound(value.toDouble());
        } else if (key == "padding-bottom") {
            m_padding_bottom = qRound(value.toDouble());
        } else if (key == "disable-background-color") {
            m_dis_bg_color = strToColor(value, m_dis_bg_color);
        } else if (key == "disable-front-color") {
            m_dis_f_color = strToColor(value, m_dis_f_color);
        } else if (key == "enable-background-color") {
            m_en_bg_color = strToColor(value, m_en_bg_color);
        } else if (key == "enable-front-color") {
            m_en_f_color = strToColor(value, m_en_f_color);
        } else if (key == "width") {
            setFixedWidth(qRound(value.toDouble()));
        } else if (key == "height") {
            setFixedHeight(qRound(value.toDouble()));
        }
    }
//    QWidget::setStyleSheet(str);
}

bool EnableButton::getIsEnabled()
{
    return m_is_enable;
}

void EnableButton::setIsEnabled(bool is_enabled, bool force_animation)
{
    if (m_is_enable == is_enabled && !force_animation) return;

    m_is_enable = is_enabled;
    m_change_ani->start();
}

void EnableButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    int new_w = width() - m_padding_left - m_padding_right;
    int new_h = height() - m_padding_top - m_padding_bottom;
    if (new_w < new_h) return;

    //resize
    if (width() != m_old_w) {
        moveSlider(m_change_ani->currentValue(), false);
        m_old_w = width();
    }

    int h_bg = static_cast<int>(new_h * 0.6);
    int d_bg = (new_h - h_bg) / 2;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QBrush(m_is_enable ? m_en_bg_color : m_dis_bg_color));

    painter.drawEllipse(m_padding_left + new_h / 2,
                        m_padding_top + d_bg,
                        h_bg, h_bg);
    painter.drawRect(m_padding_left + new_h / 2 + h_bg / 2,
                     m_padding_top + d_bg,
                     new_w - new_h - h_bg,
                     h_bg);
    painter.drawEllipse(width() - m_padding_right - (new_h / 2) - h_bg,
                        m_padding_top + d_bg,
                        h_bg, h_bg);

    painter.setBrush(QBrush(m_is_enable ? m_en_f_color : m_dis_f_color));
    painter.drawEllipse(m_slider_pos + m_padding_left, m_padding_top, new_h, new_h);
}

void EnableButton::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    m_slider_pos = m_is_enable ? width() - height() : 0;
    m_is_enable = !m_is_enable;
    m_change_ani->start();

    emit enabled(m_is_enable);
}

void EnableButton::moveSlider(const QVariant &value, bool need_update)
{
    int pure_width = width()  - m_padding_left - m_padding_right - height() + m_padding_top + m_padding_bottom;
    if (m_is_enable) {
        m_slider_pos = static_cast<int>(pure_width * 1.0 * value.toInt() / MAX_VALUE);
    } else {
        m_slider_pos = static_cast<int>(pure_width * 1.0 * (MAX_VALUE - value.toInt()) / MAX_VALUE);
    }
    if (need_update) update();
}
