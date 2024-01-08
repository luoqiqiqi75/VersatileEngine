#ifndef BACKGROUNDWIDGET_H
#define BACKGROUNDWIDGET_H

#include "../bwaf_global.h"

#include <QWidget>

class BWAFSHARED_EXPORT BackgroundWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BackgroundWidget(const QPixmap &pixmap, QWidget *parent = nullptr);
    ~BackgroundWidget() override;

protected:
    void resizeEvent(QResizeEvent *) override;

signals:

public slots:

private:
    QPixmap m_pixmap;
};

#endif // BACKGROUNDWIDGET_H
