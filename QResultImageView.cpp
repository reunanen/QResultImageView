#include "QResultImageView.h"
#include <QPainter>

QResultImageView::QResultImageView(QWidget *parent)
    : QWidget(parent)
{}

void QResultImageView::setImage(const QImage& image)
{
    pixmap.convertFromImage(image);
    update();
}

void QResultImageView::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    QRect r = rect();
    painter.drawPixmap(r, pixmap);
}
