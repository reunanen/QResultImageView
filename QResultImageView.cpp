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

    const double srcVisibleWidth = dstFullWidth * imageScaler;
    const double srcVisibleHeight = dstFullHeight * imageScaler;

    const double zoomCenterX = pixmap.width() / 2 - offsetX;
    const double zoomCenterY = pixmap.height() / 2 - offsetY;

    const double srcLeft = std::max(0.0, zoomCenterX - srcVisibleWidth / 2);
    const double srcRight = std::min(static_cast<double>(pixmap.width()), srcLeft + srcVisibleWidth);
    const double srcTop = std::max(0.0, zoomCenterY - srcVisibleHeight / 2);
    const double srcBottom = std::min(static_cast<double>(pixmap.height()), srcTop + srcVisibleHeight);

    const QPointF dstTopLeft = sourceToScreen(QPointF(srcLeft, srcTop));
    const QPointF dstBottomRight = sourceToScreen(QPointF(srcRight, srcBottom));

    const QPointF srcTopLeft = screenToSource(dstTopLeft);
    const QPointF srcBottomRight = screenToSource(dstBottomRight);

    Q_ASSERT(fabs(srcTopLeft.x() - srcLeft) < 1e-6);
    Q_ASSERT(fabs(srcTopLeft.y() - srcTop) < 1e-6);
    Q_ASSERT(fabs(srcBottomRight.x() - srcRight) < 1e-6);
    Q_ASSERT(fabs(srcBottomRight.y() - srcBottom) < 1e-6);

    const auto roundedRect = [](const QPointF& topLeft, const QPointF& bottomRight) {
        const int x = static_cast<int>(round(topLeft.x()));
        const int y = static_cast<int>(round(topLeft.y()));
        const int width = static_cast<int>(round(bottomRight.x() - topLeft.x()));
        const int height = static_cast<int>(round(bottomRight.y() - topLeft.y()));
        return QRect(x, y, width, height);
    };

    const QRect sourceRect = roundedRect(srcTopLeft, srcBottomRight);
    const QRect destinationRect = roundedRect(dstTopLeft, dstBottomRight);

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
            limitOffset();
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

        QPointF sourcePointUnderMouseBefore = screenToSource(event->posF());

        zoomLevel = newZoomLevel;

        QPointF newScreenPos = sourceToScreen(sourcePointUnderMouseBefore);
        QPointF offsetChange = (newScreenPos - event->posF()) * getImageScaler();

        offsetX -= offsetChange.rx();
        offsetY -= offsetChange.ry();

        QPointF sourcePointUnderMouseAfter = screenToSource(event->posF());

        Q_ASSERT(fabs(sourcePointUnderMouseBefore.rx() - sourcePointUnderMouseAfter.rx()) < 1e-6);
        Q_ASSERT(fabs(sourcePointUnderMouseBefore.ry() - sourcePointUnderMouseAfter.ry()) < 1e-6);

        limitOffset();

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

void QResultImageView::limitOffset()
{
    offsetX = std::max(-pixmap.width() / 2.0, std::min(pixmap.width() / 2.0, offsetX));
    offsetY = std::max(-pixmap.height() / 2.0, std::min(pixmap.height() / 2.0, offsetY));
}

QPointF QResultImageView::screenToSource(const QPointF& screenPoint) const
{
    const double imageScaler = getImageScaler();
    const QRect r(rect());
    qreal sourceX = screenPoint.x() * imageScaler - (r.width() * imageScaler - pixmap.width()) / 2 - offsetX;
    qreal sourceY = screenPoint.y() * imageScaler - (r.height() * imageScaler - pixmap.height()) / 2 - offsetY;
    return QPointF(sourceX, sourceY);
}

QPointF QResultImageView::sourceToScreen(const QPointF& sourcePoint) const
{
    const double imageScaler = getImageScaler();
    const QRect r(rect());
    qreal screenX = (r.width() - pixmap.width() / imageScaler) / 2 + (sourcePoint.x() + offsetX) / imageScaler;
    qreal screenY = (r.height() - pixmap.height() / imageScaler) / 2 + (sourcePoint.y() + offsetY) / imageScaler;
    return QPointF(screenX, screenY);
}
