#include "QResultImageView.h"
#include <QPainter>
#include <QMouseEvent>

QResultImageView::QResultImageView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
}

void QResultImageView::setImage(const QImage& image)
{
    pixmap.convertFromImage(image);
    update();
}

void QResultImageView::paintEvent(QPaintEvent* event)
{
    const int srcFullWidth = pixmap.width();
    const int srcFullHeight = pixmap.height();

    if (srcFullWidth <= 0 || srcFullHeight <= 0) {
        return;
    }

    const QRect r = rect();

    const int dstFullWidth = r.width();
    const int dstFullHeight = r.height();

    const double imageScaler = getImageScaler();

    const int srcVisibleWidth = static_cast<int>(std::round(dstFullWidth * imageScaler));
    const int srcVisibleHeight = static_cast<int>(std::round(dstFullHeight * imageScaler));

    const int zoomCenterX = pixmap.width() / 2 - static_cast<int>(round(offsetX));
    const int zoomCenterY = pixmap.height() / 2 - static_cast<int>(round(offsetY));

    int srcX1 = zoomCenterX - srcVisibleWidth / 2;
    int srcX2 = srcX1 + srcVisibleWidth;
    int srcY1 = zoomCenterY - srcVisibleHeight / 2;
    int srcY2 = srcY1 + srcVisibleHeight;

    //srcX1 += zoomLevel * srcVisibleWidth / getMaxZoomLevel();
    //srcX2 -= zoomLevel * srcVisibleWidth / getMaxZoomLevel();
    //srcY1 += zoomLevel * srcVisibleHeight / getMaxZoomLevel();
    //srcY2 -= zoomLevel * srcVisibleHeight / getMaxZoomLevel();

    int dstX1 = 0;
    int dstX2 = dstFullWidth;
    int dstY1 = 0;
    int dstY2 = dstFullHeight;

    if (srcX1 < 0) {
        dstX1 -= round(srcX1 / imageScaler);
        srcX1 -= srcX1;
    }
    if (srcX2 > srcFullWidth) {
        const int excess = srcX2 - srcFullWidth;
        dstX2 -= round(excess / imageScaler);
        srcX2 -= excess;
    }
    if (srcY1 < 0) {
        dstY1 -= round(srcY1 / imageScaler);
        srcY1 -= srcY1;
    }
    if (srcY2 > srcFullHeight) {
        const int excess = srcY2 - srcFullHeight;
        dstY2 -= round(excess / imageScaler);
        srcY2 -= excess;
    }

    if (dstX1 < 0) {
        dstX2 -= dstX1;
        dstX1 -= dstX1;
    }
    if (dstY1 < 0) {
        dstY2 -= dstY1;
        dstY1 -= dstY1;
    }
    if (dstX2 < dstX1) {
        dstX2 = dstX1;
    }
    if (dstY2 < dstY1) {
        dstY2 = dstY1;
    }

    const QRect sourceRect(srcX1, srcY1, srcX2 - srcX1, srcY2 - srcY1);
    const QRect destinationRect(dstX1, dstY1, dstX2 - dstX1, dstY2 - dstY1);

    QPainter painter(this);
    painter.drawPixmap(destinationRect, pixmap.copy(sourceRect));
}

void QResultImageView::mouseMoveEvent(QMouseEvent *event)
{
    if (hasPreviousMouseCoordinates) {
        if (event->buttons() & Qt::LeftButton) {
            const double imageScaler = getImageScaler();
            offsetX += (event->x() - previousMouseX) * imageScaler;
            offsetY += (event->y() - previousMouseY) * imageScaler;
            offsetX = std::max(-pixmap.width() / 2.0, std::min(pixmap.width() / 2.0, offsetX));
            offsetY = std::max(-pixmap.height() / 2.0, std::min(pixmap.height() / 2.0, offsetY));
            update();
        }
    }

    hasPreviousMouseCoordinates = true;
    previousMouseX = event->x();
    previousMouseY = event->y();
}

void QResultImageView::wheelEvent(QWheelEvent* event)
{
    const int newZoomLevel = std::min(std::max(zoomLevel + event->delta(), 0), getMaxZoomLevel());
    if (newZoomLevel != zoomLevel) {
        //const bool zoomingIn = newZoomLevel > zoomLevel;
        zoomLevel = newZoomLevel;
        update();
    }
}

double QResultImageView::getDefaultMagnification() const
{
    if (pixmap.size().isEmpty()) {
        return 1.0;
    }

    const QRect r = rect();

    const double magnificationX = pixmap.width() / static_cast<double>(r.width());
    const double magnificationY = pixmap.height() / static_cast<double>(r.height());
    const double magnification = std::max(magnificationX, magnificationY);

    return magnification;
}

double QResultImageView::getEffectiveZoomLevel() const
{
    const double maxZoomLevel = getMaxZoomLevel();
    const double minEffectiveZoomLevel = 200.0 / maxZoomLevel; // pretty much empirical
    const double linearPart = zoomLevel / maxZoomLevel;
    const double nonlinearPart = pow(zoomLevel / maxZoomLevel, 0.5);
    const double linearPartWeight = 0.5;
    const double adjustedZoomLevel = linearPartWeight * linearPart + (1.0 - linearPartWeight) * nonlinearPart;
    return minEffectiveZoomLevel + (1.0 - minEffectiveZoomLevel) * (1.0 - adjustedZoomLevel);
}

double QResultImageView::getImageScaler() const
{
    return getEffectiveZoomLevel() * getDefaultMagnification();
}

int QResultImageView::getMaxZoomLevel() const
{
    const int maxZoomLevelMultiplier = 4; // largely empirical
    return maxZoomLevelMultiplier * std::max(0, std::min(pixmap.width(), pixmap.height()));
}
