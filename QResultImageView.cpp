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
    source.convertFromImage(image);
    updateScaledSourceImage();
    update();
}

void QResultImageView::setResults(const std::vector<Result>& results)
{
    this->results = results;
    updateScaledAndTranslatedResults();
    update();
}

void QResultImageView::setImageAndResults(const QImage& image, const Results& results)
{
    source.convertFromImage(image);
    this->results = results;

    updateScaledSourceImage();
    updateScaledAndTranslatedResults();

    update();
}

void QResultImageView::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.drawPixmap(destinationRect, croppedSource);

    for (const Result& scaledAndTranslatedResult : scaledAndTranslatedResults) {
        painter.setPen(scaledAndTranslatedResult.pen);
        if (!scaledAndTranslatedResult.contour.empty()) {
            painter.drawPolygon(scaledAndTranslatedResult.contour.data(), static_cast<int>(scaledAndTranslatedResult.contour.size()));
        }
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
            updateScaledAndTranslatedResults();
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

        updateScaledSourceImage();
        updateScaledAndTranslatedResults();
        update();
    }
}

void QResultImageView::resizeEvent(QResizeEvent* event)
{
    updateScaledSourceImage();
    updateScaledAndTranslatedResults();
    update();
}

void QResultImageView::updateScaledSourceImage()
{
    const int srcFullWidth = source.width();
    const int srcFullHeight = source.height();

    const QRect r = rect();

    if (srcFullWidth <= 0 || srcFullHeight <= 0 || r.width() <= 0 || r.height() <= 0) {
        return;
    }

    const double scaleFactorX = r.width() / static_cast<double>(srcFullWidth);
    const double scaleFactorY = r.height() / static_cast<double>(srcFullHeight);
    const double scaleFactor = std::max(scaleFactorX, scaleFactorY);

    const int scaledWidth = static_cast<int>(ceil(scaleFactor * srcFullWidth));
    const int scaledHeight = static_cast<int>(ceil(scaleFactor * srcFullHeight));

    scaledSource = source.scaled(QSize(scaledWidth, scaledHeight), Qt::IgnoreAspectRatio, Qt::FastTransformation);

    updateCroppedSourceImageAndDestinationRect();
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
    croppedSource = scaledSource.copy(scaledSourceRect);

    destinationRect = roundedRect(dstTopLeft, dstBottomRight);
}

void QResultImageView::updateScaledAndTranslatedResults()
{
    scaledAndTranslatedResults.resize(results.size());

    const double imageScaler = getImageScaler();

    for (size_t i = 0, end = results.size(); i < end; ++i) {
        const Result& result = results[i];
        Result& scaledAndTranslatedResult = scaledAndTranslatedResults[i];

        scaledAndTranslatedResult.pen = result.pen;
        scaledAndTranslatedResult.contour.resize(result.contour.size());
        for (size_t j = 0, end = result.contour.size(); j < end; ++j) {
            scaledAndTranslatedResult.contour[j] = sourceToScreen(result.contour[j]);
        }
    }
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
