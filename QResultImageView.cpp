#include "QResultImageView.h"
#include <QPainter>
#include <QMouseEvent>
#include <qtimer.h>

QResultImageView::QResultImageView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
}

void QResultImageView::setImage(const QImage& image)
{
    source.convertFromImage(image);
    redrawEverything(getDesiredTransformationMode());
}

void QResultImageView::setResults(const std::vector<Result>& results)
{
    this->results = results;
    drawResultsOnScaledSourceImage();
    updateCroppedSourceImageAndDestinationRect();
    update();
}

void QResultImageView::setImageAndResults(const QImage& image, const Results& results)
{
    source.convertFromImage(image);
    this->results = results;

    redrawEverything(getDesiredTransformationMode());
}

void QResultImageView::setZoomedOutTransformationMode(Qt::TransformationMode transformationMode)
{
    if (zoomedOutTransformationMode != transformationMode) {
        zoomedOutTransformationMode = transformationMode;
        redrawEverything(getDesiredTransformationMode());
    }
}

void QResultImageView::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.drawPixmap(destinationRect, croppedSource);
}

void QResultImageView::mouseMoveEvent(QMouseEvent *event)
{
    if (hasPreviousMouseCoordinates) {
        if (event->buttons() & Qt::LeftButton) {
            const double imageScaler = getImageScaler();
            offsetX += (event->x() - previousMouseX) * imageScaler;
            offsetY += (event->y() - previousMouseY) * imageScaler;
            limitOffset();
            updateCroppedSourceImageAndDestinationRect();
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

        redrawEverything(Qt::FastTransformation);
        considerActivatingSmoothTransformationTimer();
    }
}

void QResultImageView::resizeEvent(QResizeEvent* event)
{
    if (!isnan(getScaleFactor())) {
        redrawEverything(Qt::FastTransformation);
        considerActivatingSmoothTransformationTimer();
    }
}

double QResultImageView::getScaleFactor() const
{
    const int srcFullWidth = source.width();
    const int srcFullHeight = source.height();

    const QRect r = rect();

    if (srcFullWidth <= 0 || srcFullHeight <= 0 || r.width() <= 0 || r.height() <= 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    return std::min(1.0, 1.0 / getImageScaler());
}

void QResultImageView::redrawEverything(Qt::TransformationMode transformationMode)
{
    updateScaledSourceImage(transformationMode);
    drawResultsOnScaledSourceImage();
    updateCroppedSourceImageAndDestinationRect();
    update();
}

void QResultImageView::updateScaledSourceImage(Qt::TransformationMode transformationMode)
{
    const double scaleFactor = getScaleFactor();

    if (isnan(scaleFactor)) {
        return;
    }

    const int scaledWidth = static_cast<int>(ceil(scaleFactor * source.width()));
    const int scaledHeight = static_cast<int>(ceil(scaleFactor * source.height()));

    scaledSource = source.scaled(QSize(scaledWidth, scaledHeight), Qt::IgnoreAspectRatio, transformationMode);
}

void QResultImageView::drawResultsOnScaledSourceImage()
{
    if (results.empty()) {
        scaledSourceWithResults = scaledSource;
    }
    else {
        scaledSourceWithResults = scaledSource.copy();
    }

    const double scaleFactor = getScaleFactor();

    QPainter resultPainter(&scaledSourceWithResults);
    for (const Result& result : results) {
        resultPainter.setPen(result.pen);
        if (!result.contour.empty()) {
            std::vector<QPoint> scaledContour(result.contour.size());
            for (size_t i = 0, end = result.contour.size(); i < end; ++i) {
                const QPointF& point = result.contour[i];
                QPoint& scaledPoint = scaledContour[i];
                scaledPoint.setX(static_cast<int>(std::round(point.x() * scaleFactor)));
                scaledPoint.setY(static_cast<int>(std::round(point.y() * scaleFactor)));
            }
            resultPainter.drawPolygon(scaledContour.data(), static_cast<int>(scaledContour.size()));
        }
    }
}

void QResultImageView::updateCroppedSourceImageAndDestinationRect()
{
    const double zoomCenterX = source.width() / 2 - offsetX;
    const double zoomCenterY = source.height() / 2 - offsetY;

    const double srcVisibleWidth = getSourceImageVisibleWidth();
    const double srcVisibleHeight = getSourceImageVisibleHeigth();

    // these two should be approximately equal
    const double sourceScaleFactorX = scaledSource.width() / static_cast<double>(source.width());
    const double sourceScaleFactorY = scaledSource.height() / static_cast<double>(source.height());

    const double srcLeft = std::max(0.0, zoomCenterX - srcVisibleWidth / 2);
    const double srcRight = std::min(static_cast<double>(source.width()), srcLeft + srcVisibleWidth);
    const double srcTop = std::max(0.0, zoomCenterY - srcVisibleHeight / 2);
    const double srcBottom = std::min(static_cast<double>(source.height()), srcTop + srcVisibleHeight);

    const double scaledSourceLeft = srcLeft * sourceScaleFactorX;
    const double scaledSourceRight = srcRight * sourceScaleFactorX;
    const double scaledSourceTop = srcTop * sourceScaleFactorY;
    const double scaledSourceBottom = srcBottom * sourceScaleFactorY;

    const QPointF dstTopLeft = sourceToScreen(QPointF(srcLeft, srcTop));
    const QPointF dstBottomRight = sourceToScreen(QPointF(srcRight, srcBottom));

    QPointF srcTopLeft = screenToSource(dstTopLeft);
    QPointF srcBottomRight = screenToSource(dstBottomRight);

    const QPointF scaledSourceTopLeft = QPointF(srcTopLeft.rx() * sourceScaleFactorX, srcTopLeft.ry() * sourceScaleFactorY);
    const QPointF scaledSourceBottomRight = QPointF(srcBottomRight.rx() * sourceScaleFactorX, srcBottomRight.ry() * sourceScaleFactorY);

    Q_ASSERT(fabs(srcTopLeft.x() - srcLeft) < 1e-6);
    Q_ASSERT(fabs(srcTopLeft.y() - srcTop) < 1e-6);
    Q_ASSERT(fabs(srcBottomRight.x() - srcRight) < 1e-6);
    Q_ASSERT(fabs(srcBottomRight.y() - srcBottom) < 1e-6);

    Q_ASSERT(fabs(scaledSourceTopLeft.x() - scaledSourceLeft) < 1e-6);
    Q_ASSERT(fabs(scaledSourceTopLeft.y() - scaledSourceTop) < 1e-6);
    Q_ASSERT(fabs(scaledSourceBottomRight.x() - scaledSourceRight) < 1e-6);
    Q_ASSERT(fabs(scaledSourceBottomRight.y() - scaledSourceBottom) < 1e-6);

    const auto roundedRect = [](const QPointF& topLeft, const QPointF& bottomRight) {
        const int x = static_cast<int>(round(topLeft.x()));
        const int y = static_cast<int>(round(topLeft.y()));
        const int width = static_cast<int>(round(bottomRight.x() - topLeft.x()));
        const int height = static_cast<int>(round(bottomRight.y() - topLeft.y()));
        return QRect(x, y, width, height);
    };

    const QRect scaledSourceRect = roundedRect(scaledSourceTopLeft, scaledSourceBottomRight);
    croppedSource = scaledSourceWithResults.copy(scaledSourceRect);

    destinationRect = roundedRect(dstTopLeft, dstBottomRight);
}

double QResultImageView::getSourceImageVisibleWidth() const
{
    const QRect r = rect();
    return r.width() * getImageScaler();
}

double QResultImageView::getSourceImageVisibleHeigth() const
{
    const QRect r = rect();
    return r.height() * getImageScaler();
}

double QResultImageView::getDefaultMagnification() const
{
    if (source.size().isEmpty()) {
        return 1.0;
    }

    const QRect r = rect();

    const double magnificationX = source.width() / static_cast<double>(r.width());
    const double magnificationY = source.height() / static_cast<double>(r.height());
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
    return maxZoomLevelMultiplier * std::max(0, std::min(source.width(), source.height()));
}

void QResultImageView::limitOffset()
{
    offsetX = std::max(-source.width() / 2.0, std::min(source.width() / 2.0, offsetX));
    offsetY = std::max(-source.height() / 2.0, std::min(source.height() / 2.0, offsetY));
}

QPointF QResultImageView::screenToSource(const QPointF& screenPoint) const
{
    const double imageScaler = getImageScaler();
    const QRect r(rect());
    qreal sourceX = screenPoint.x() * imageScaler - (r.width() * imageScaler - source.width()) / 2 - offsetX;
    qreal sourceY = screenPoint.y() * imageScaler - (r.height() * imageScaler - source.height()) / 2 - offsetY;
    return QPointF(sourceX, sourceY);
}

QPointF QResultImageView::sourceToScreen(const QPointF& sourcePoint) const
{
    const double imageScaler = getImageScaler();
    const QRect r(rect());
    qreal screenX = (r.width() - source.width() / imageScaler) / 2 + (sourcePoint.x() + offsetX) / imageScaler;
    qreal screenY = (r.height() - source.height() / imageScaler) / 2 + (sourcePoint.y() + offsetY) / imageScaler;
    return QPointF(screenX, screenY);
}

void QResultImageView::performSmoothTransformation()
{
    --smoothTransformationPendingCounter;    

    if (smoothTransformationPendingCounter == 0 && isSmoothTransformationDesired()) {
        redrawEverything(Qt::SmoothTransformation);
    }
}

Qt::TransformationMode QResultImageView::getDesiredTransformationMode() const
{
    if (isSmoothTransformationDesired()) {
        return Qt::SmoothTransformation;
    }
    else {
        return Qt::FastTransformation;
    }
}

bool QResultImageView::isSmoothTransformationDesired() const
{
    if (zoomedOutTransformationMode == Qt::FastTransformation) {
        return false;
    }

    const double imageScaler = getImageScaler();
    return imageScaler > 1.0;
}

void QResultImageView::considerActivatingSmoothTransformationTimer()
{
    if (isSmoothTransformationDesired()) {
        ++smoothTransformationPendingCounter;
        QTimer::singleShot(100, this, SLOT(performSmoothTransformation()));
    }
}
