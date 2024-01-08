#include "bwaf/custom/backgroundwidget.h"

#include <veCommon>

BackgroundWidget::BackgroundWidget(const QPixmap &pixmap, QWidget *parent) : QWidget(parent),
    m_pixmap(pixmap)
{
    this->setAutoFillBackground(true);
}

BackgroundWidget::~BackgroundWidget()
{

}

void BackgroundWidget::resizeEvent(QResizeEvent *)
{
    if (m_pixmap.isNull()) return;
    QPalette palette = this->palette();
    palette.setBrush(this->backgroundRole(),
                     QBrush(Qt::white, m_pixmap.scaled(this->size(),
                                             Qt::KeepAspectRatioByExpanding,
                                             Qt::SmoothTransformation)));
    this->setPalette(palette);
}
