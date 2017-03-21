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
    redrawEverything(getEventualTransformationMode());
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

    redrawEverything(getEventualTransformationMode());
}

void QResultImageView::setTransformationMode(TransformationMode mode)
{
    if (transformationMode != mode) {
        transformationMode = mode;
        redrawEverything(getEventualTransformationMode());
    }
}

void QResultImageView::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.drawPixmap(destinationRect, croppedSource);

    if (!isnan(pixelSize_m)) {
        drawYardstick(painter);
    }
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

            emit panned();
        }
    }

    hasPreviousMouseCoordinates = true;
    previousMouseX = event->x();
    previousMouseY = event->y();
}

void QResultImageView::wheelEvent(QWheelEvent* event)
{
    const int newZoomLevel = std::min(std::max(zoomLevel + 4 * event->delta(), 0), getMaxZoomLevel());
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

        redrawEverything(getInitialTransformationMode());
        considerActivatingSmoothTransformationTimer();

        emit zoomed();
    }
}

void QResultImageView::resizeEvent(QResizeEvent* event)
{
    if (!isnan(getScaleFactor())) {
        redrawEverything(getInitialTransformationMode());
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
    const double scaleFactor = getScaleFactor();

    if (!isnan(scaleFactor)) {
        updateScaledSourceImage(transformationMode);
        drawResultsOnScaledSourceImage();
        updateCroppedSourceImageAndDestinationRect();
        update();
    }
}

void QResultImageView::updateScaledSourceImage(Qt::TransformationMode transformationMode)
{
    const double scaleFactor = getScaleFactor();

    Q_ASSERT(!isnan(scaleFactor));

    const int scaledWidth = static_cast<int>(ceil(scaleFactor * source.width()));
    const int scaledHeight = static_cast<int>(ceil(scaleFactor * source.height()));

    scaledSource = source.scaled(QSize(scaledWidth, scaledHeight), Qt::IgnoreAspectRatio, transformationMode);
}

void QResultImageView::drawResultsOnScaledSourceImage()
{
    if (results.empty() || !resultsVisible) {
        scaledSourceWithResults = scaledSource;
    }
    else {
        scaledSourceWithResults = scaledSource.copy();

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

// adapted from https://en.wikipedia.org/wiki/Smoothstep
double smoothstep(double x)
{
    return x * x * (3 - 2 * x);
}

double QResultImageView::getEffectiveZoomLevel() const
{
    const double maxZoomLevel = getMaxZoomLevel();
    const double minEffectiveZoomLevel = 200.0 / maxZoomLevel; // pretty much empirical
    const double linearPart = zoomLevel / maxZoomLevel;
    const double nonlinearPart = smoothstep(sqrt(zoomLevel/ maxZoomLevel));
    const double linearPartWeight = 0.1;
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

    if (smoothTransformationPendingCounter == 0 && getEventualTransformationMode() == Qt::SmoothTransformation) {
        redrawEverything(Qt::SmoothTransformation);
    }
}

Qt::TransformationMode QResultImageView::getInitialTransformationMode() const
{
    const double imageScaler = getImageScaler();

    if (imageScaler > 1.0 && transformationMode == SmoothTransformationWhenZoomedOut) {
        return Qt::SmoothTransformation;
    }
    else {
        return Qt::FastTransformation;
    }
}

Qt::TransformationMode QResultImageView::getEventualTransformationMode() const
{
    const double imageScaler = getImageScaler();

    if (imageScaler > 1.0 && transformationMode != AlwaysFastTransformation) {
        return Qt::SmoothTransformation;
    }
    else {
        return Qt::FastTransformation;
    }
}

void QResultImageView::considerActivatingSmoothTransformationTimer()
{
    if (getEventualTransformationMode() == Qt::SmoothTransformation) {
        ++smoothTransformationPendingCounter;
        QTimer::singleShot(100, this, SLOT(performSmoothTransformation()));
    }
}

void QResultImageView::setResultsVisible(bool visible)
{
    if (resultsVisible != visible) {
        resultsVisible = visible;

        if (!results.empty()) {
            drawResultsOnScaledSourceImage();
            updateCroppedSourceImageAndDestinationRect();
            update();
        }
    }
}

void QResultImageView::resetZoomAndPan()
{
    offsetX = 0;
    offsetY = 0;
    zoomLevel = 0;

    redrawEverything(getEventualTransformationMode());
}

bool QResultImageView::isDefaultZoomAndPan() const
{
    return offsetX == 0 && offsetY == 0 && zoomLevel == 0;
}

void QResultImageView::setPixelSizeInMeters(double pixelSizeInMeters)
{
    pixelSize_m = pixelSizeInMeters;
    update();
}

void QResultImageView::drawYardstick(QPainter& painter)
{
    const double imageScaler = getImageScaler();

    if (isnan(imageScaler)) {
        return;
    }

    const QRect r = rect();

    const int margin = 20;

    const auto getYardstickSize_m = [&](int rectDimension) {
        const double maxYardstickSize_m = (rectDimension - 2 * margin) * pixelSize_m * imageScaler;

        // round down to the nearest power of 10
        double yardstickSize_m = pow(10, floor(log10(maxYardstickSize_m)));

        if (2 * yardstickSize_m <= maxYardstickSize_m) {
            yardstickSize_m = 2 * yardstickSize_m;
        }
        if (2.5 * yardstickSize_m <= maxYardstickSize_m) {
            yardstickSize_m = 2.5 * yardstickSize_m;
        }

        return yardstickSize_m;
    };

    const auto getYardstickText = [](double yardstickSize_m) {
        if (yardstickSize_m < 1e-3) {
            return QString::number(yardstickSize_m * 1e6, 'g') + " um";
        }
        else if (yardstickSize_m < 1) {
            return QString::number(yardstickSize_m * 1e3, 'f', 0) + " mm";
        }
        else {
            return QString::number(yardstickSize_m, 'f', 0) + " m";
        }
    };

    const auto drawOutlinedText = [&painter](int x, int y, int w, int h, int flags, const QString& text)
    {
        painter.setPen(Qt::white);
        for (int i = -1; i <= 1; ++i) {
            for (int j = -1; j <= 1; ++j) {
                if (i != 0 && j != 0) {
                    painter.drawText(x + i, y + j, w, h, flags, text);
                }
            }
        }
        painter.setPen(Qt::black);
        painter.drawText(x, y, w, h, flags, text);
    };

    if (r.width() > 8 * margin && r.height() > 2 * margin) {
        const double yardstickSizeX_m = getYardstickSize_m(r.width());

        const int y = r.height() - margin;
        const int w = std::round(yardstickSizeX_m / pixelSize_m / imageScaler);

        painter.setPen(Qt::white);
        painter.drawLine(margin, y - 1, margin + w, y - 1);

        painter.setPen(Qt::black);
        painter.drawLine(margin, y, margin + w, y);

        drawOutlinedText(margin, y, w, margin, Qt::AlignRight | Qt::AlignTop, getYardstickText(yardstickSizeX_m));
    }

    if (r.height() > 8 * margin && r.width() > 2 * margin) {
        const double yardstickSizeY_m = getYardstickSize_m(r.height());

        const int origin = r.height() - margin;
        const int h = std::round(yardstickSizeY_m / pixelSize_m / imageScaler);

        painter.setPen(Qt::white);
        painter.drawLine(margin + 1, origin - h, margin + 1, origin - 1);

        painter.setPen(Qt::black);
        painter.drawLine(margin, origin - h, margin, origin);

        painter.rotate(-90);
        drawOutlinedText(-origin, 0, h, margin, Qt::AlignRight | Qt::AlignBottom, getYardstickText(yardstickSizeY_m));
        painter.rotate(90);
    }
}
