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
    sourceImage = image;
    sourcePixmap = QPixmap();
    updateSourcePyramid();

    redrawEverything(getEventualTransformationMode());
}

void QResultImageView::setImagePyramid(const std::vector<QImage>& imagePyramid)
{
    if (!imagePyramid.empty()) {
        sourceImage = imagePyramid[0];
    }
    else {
        sourceImage = QImage();
    }
    sourcePixmap = QPixmap();

    sourceImagePyramid.clear();
    sourcePixmapPyramid.clear();

    for (size_t i = 1, end = imagePyramid.size(); i < end; ++i) {
        const double scaleFactor = std::sqrt(imagePyramid[i].width() * imagePyramid[i].height() / static_cast<double>(sourceImage.width() * sourceImage.height()));
        sourceImagePyramid[scaleFactor] = imagePyramid[i];
    }

    redrawEverything(getEventualTransformationMode());
}

void QResultImageView::setResults(const std::vector<Result>& results)
{
    this->results = results;
    setResultPolygons();

    drawResultsToViewport();
    update();
}

void QResultImageView::setImageAndResults(const QImage& image, const Results& results)
{
    sourceImage = image;
    sourcePixmap = QPixmap();
    updateSourcePyramid();

    this->results = results;
    setResultPolygons();

    redrawEverything(getEventualTransformationMode());
}

void QResultImageView::setImagePyramidAndResults(const std::vector<QImage>& imagePyramid, const Results& results)
{
    if (!imagePyramid.empty()) {
        sourceImage = imagePyramid[0];
    }
    else {
        sourceImage = QImage();
    }
    sourcePixmap = QPixmap();

    sourceImagePyramid.clear();
    sourcePixmapPyramid.clear();

    for (size_t i = 1, end = imagePyramid.size(); i < end; ++i) {
        const double scaleFactor = std::sqrt(imagePyramid[i].width() * imagePyramid[i].height() / static_cast<double>(sourceImage.width() * sourceImage.height()));
        sourceImagePyramid[scaleFactor] = imagePyramid[i];
    }

    this->results = results;
    setResultPolygons();

    redrawEverything(getEventualTransformationMode());
}

void QResultImageView::setTransformationMode(TransformationMode newTransformationMode)
{
    if (newTransformationMode != transformationMode) {

        const bool needToUpdateSourcePyramid = (newTransformationMode == AlwaysFastTransformation || transformationMode == AlwaysFastTransformation);

        transformationMode = newTransformationMode;

        if (needToUpdateSourcePyramid) {
            updateSourcePyramid();
        }

        redrawEverything(getEventualTransformationMode());
    }
}

void QResultImageView::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.drawPixmap(destinationRect, scaledAndCroppedSourceWithResults);

    if (!isnan(pixelSize_m)) {
        drawYardstick(painter);
    }
}

void QResultImageView::mouseMoveEvent(QMouseEvent *event)
{
    checkMousePan(event);
    checkMouseOnResult(event);

    const QPointF sourceCoordinate = screenToSource(event->pos());

    // Need to truncate here; rounding isn't the correct thing to do
    QPoint point(static_cast<int>(sourceCoordinate.x()), static_cast<int>(sourceCoordinate.y()));
    int pixelIndex = -1;
    if (sourceImage.valid(point)) {
        pixelIndex = sourceImage.pixelIndex(point);
    }
    emit mouseAtCoordinates(sourceCoordinate, pixelIndex);
}

void QResultImageView::leaveEvent(QEvent*)
{
    emit mouseLeft();
}

void QResultImageView::checkMousePan(const QMouseEvent *event)
{
    if (hasPreviousMouseCoordinates) {
        if (event->buttons() & Qt::LeftButton) {
            const double imageScaler = getImageScaler();
            offsetX += (event->x() - previousMouseX) * imageScaler;
            offsetY += (event->y() - previousMouseY) * imageScaler;
            limitOffset();
            redrawEverything(getInitialTransformationMode());
            considerActivatingSmoothTransformationTimer();

            emit panned();
        }
    }

    hasPreviousMouseCoordinates = true;
    previousMouseX = event->x();
    previousMouseY = event->y();
}

void QResultImageView::checkMouseOnResult(const QMouseEvent *event)
{
    const QPointF screenPoint(event->x(), event->y());
    const QPointF sourcePoint = screenToSource(screenPoint);

    size_t newMouseOnResultIndex = -1;

    for (size_t i = 0, end = resultPolygons.size(); i < end; ++i) {
        const QPolygonF& polygon = resultPolygons[i];
        if (polygon.containsPoint(sourcePoint, Qt::OddEvenFill)) {
            newMouseOnResultIndex = i;
            break;
        }
    }

    if (newMouseOnResultIndex != mouseOnResultIndex) {
        if (mouseOnResultIndex != -1 || newMouseOnResultIndex == -1) {
            emit mouseNotOnResult();
        }
        if (newMouseOnResultIndex != -1) {
            emit mouseOnResult(newMouseOnResultIndex);
        }
        mouseOnResultIndex = newMouseOnResultIndex;
    }
}

void QResultImageView::wheelEvent(QWheelEvent* event)
{
    const int zoomMultiplier
            = (event->modifiers() & Qt::ShiftModifier)
            ? 20
            : 4;

    const int newZoomLevel = std::min(std::max(zoomLevel + zoomMultiplier * event->delta(), 0), getMaxZoomLevel());

    const QPointF point = event->posF();

    zoom(newZoomLevel, &point);
}

void QResultImageView::zoom(int newZoomLevel, const QPointF* screenPoint)
{
    if (newZoomLevel != zoomLevel) {

        const auto getScreenPoint = [this, screenPoint]() {
            if (screenPoint) {
                return QPointF(*screenPoint);
            }
            else {
                const QRect r = rect();
                return QPointF(r.width() / 2.f, r.height() / 2.f);
            }
        };

        const QPointF point = getScreenPoint();
        QPointF sourcePointBefore = screenToSource(point);

        zoomLevel = newZoomLevel;

        const QPointF newScreenPos = sourceToScreen(sourcePointBefore);
        QPointF offsetChange = (newScreenPos - point) * getImageScaler();

        offsetX -= offsetChange.rx();
        offsetY -= offsetChange.ry();

        QPointF sourcePointAfter = screenToSource(point);

        Q_ASSERT(fabs(sourcePointBefore.rx() - sourcePointAfter.rx()) < 1e-6);
        Q_ASSERT(fabs(sourcePointBefore.ry() - sourcePointAfter.ry()) < 1e-6);

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
    const int srcFullWidth = sourceImage.width();
    const int srcFullHeight = sourceImage.height();

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
        updateViewport(transformationMode);
        drawResultsToViewport();
        update();
    }
}

std::pair<double, const QPixmap*> QResultImageView::getSourcePixmap(double scaleFactor) const
{
    if (scaleFactor > sourceImagePyramid.rbegin()->first) {
        if (sourcePixmap.width() == 0 && sourcePixmap.height() == 0) {
            sourcePixmap.convertFromImage(sourceImage);
        }
        return std::make_pair(1.0, &sourcePixmap);
    }
    else {
        auto i = sourceImagePyramid.begin();
        double foundScaleFactor = i->first;
        const QImage* correspondingImage = &i->second;
        while (scaleFactor > i->first) {
            Q_ASSERT(i != sourceImagePyramid.end());
            ++i;
            foundScaleFactor = i->first;
            correspondingImage = &i->second;
        }
        Q_ASSERT(i != sourceImagePyramid.end());

        auto j = sourcePixmapPyramid.find(foundScaleFactor);
        if (j == sourcePixmapPyramid.end()) {
            sourcePixmapPyramid[foundScaleFactor].convertFromImage(*correspondingImage);
            j = sourcePixmapPyramid.find(foundScaleFactor);
        }

        const QPixmap* correspondingPixmap = &j->second;

        return std::make_pair(foundScaleFactor, correspondingPixmap);
    }
}

void QResultImageView::drawResultsToViewport()
{
    if (results.empty() || !resultsVisible) {
        scaledAndCroppedSourceWithResults = scaledAndCroppedSource;
    }
    else {
        scaledAndCroppedSourceWithResults = scaledAndCroppedSource.copy();

        const double scaleFactor = getScaleFactor();

        const double zoomCenterX = sourceImage.width() / 2 - offsetX;
        const double zoomCenterY = sourceImage.height() / 2 - offsetY;

        const double srcVisibleWidth = getSourceImageVisibleWidth();
        const double srcVisibleHeight = getSourceImageVisibleHeigth();

        const double srcLeft = std::max(0.0, zoomCenterX - srcVisibleWidth / 2);
        const double srcTop = std::max(0.0, zoomCenterY - srcVisibleHeight / 2);

        QPainter resultPainter(&scaledAndCroppedSourceWithResults);
        for (const Result& result : results) {
            resultPainter.setPen(result.pen);
            if (!result.contour.empty()) {
                std::vector<QPoint> scaledContour(result.contour.size());
                for (size_t i = 0, end = result.contour.size(); i < end; ++i) {
                    const QPointF& point = result.contour[i];
                    QPoint& scaledPoint = scaledContour[i];
                    scaledPoint.setX(static_cast<int>(std::round(point.x() - srcLeft) * scaleFactor));
                    scaledPoint.setY(static_cast<int>(std::round(point.y() - srcTop) * scaleFactor));
                }
                resultPainter.drawPolygon(scaledContour.data(), static_cast<int>(scaledContour.size()));
            }
        }
    }
}

void QResultImageView::updateViewport(Qt::TransformationMode transformationMode)
{
    const double scaleFactor = getScaleFactor();

    Q_ASSERT(!isnan(scaleFactor));

    const std::pair<double, const QPixmap*> scaledSource = getSourcePixmap(scaleFactor);

    const double zoomCenterX = sourceImage.width() / 2 - offsetX;
    const double zoomCenterY = sourceImage.height() / 2 - offsetY;

    const double srcVisibleWidth = getSourceImageVisibleWidth();
    const double srcVisibleHeight = getSourceImageVisibleHeigth();

    // these two should be approximately equal
    const double sourceScaleFactorX = scaledSource.second->width() / static_cast<double>(sourceImage.width());
    const double sourceScaleFactorY = scaledSource.second->height() / static_cast<double>(sourceImage.height());

    const double srcLeft = std::max(0.0, zoomCenterX - srcVisibleWidth / 2);
    const double srcRight = std::min(static_cast<double>(sourceImage.width()), srcLeft + srcVisibleWidth);
    const double srcTop = std::max(0.0, zoomCenterY - srcVisibleHeight / 2);
    const double srcBottom = std::min(static_cast<double>(sourceImage.height()), srcTop + srcVisibleHeight);

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

    const QRect croppedSourceRect = roundedRect(scaledSourceTopLeft, scaledSourceBottomRight);
    croppedSource = scaledSource.second->copy(croppedSourceRect);

    const int scaledWidth = static_cast<int>(std::round(scaleFactor / scaledSource.first * croppedSource.width()));
    const int scaledHeight = static_cast<int>(std::round(scaleFactor / scaledSource.first * croppedSource.height()));
    scaledAndCroppedSource = croppedSource.scaled(QSize(scaledWidth, scaledHeight), Qt::IgnoreAspectRatio, transformationMode);

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
    if (sourceImage.size().isEmpty()) {
        return 1.0;
    }

    const QRect r = rect();

    const double magnificationX = sourceImage.width() / static_cast<double>(r.width());
    const double magnificationY = sourceImage.height() / static_cast<double>(r.height());
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
    return maxZoomLevelMultiplier * std::max(0, std::min(sourceImage.width(), sourceImage.height()));
}

void QResultImageView::limitOffset()
{
    offsetX = std::max(-sourceImage.width() / 2.0, std::min(sourceImage.width() / 2.0, offsetX));
    offsetY = std::max(-sourceImage.height() / 2.0, std::min(sourceImage.height() / 2.0, offsetY));
}

QPointF QResultImageView::screenToSource(const QPointF& screenPoint) const
{
    const double imageScaler = getImageScaler();
    const QRect r(rect());
    qreal sourceX = screenPoint.x() * imageScaler - (r.width() * imageScaler - sourceImage.width()) / 2 - offsetX;
    qreal sourceY = screenPoint.y() * imageScaler - (r.height() * imageScaler - sourceImage.height()) / 2 - offsetY;
    return QPointF(sourceX, sourceY);
}

QPointF QResultImageView::sourceToScreen(const QPointF& sourcePoint) const
{
    const double imageScaler = getImageScaler();
    const QRect r(rect());
    qreal screenX = (r.width() - sourceImage.width() / imageScaler) / 2 + (sourcePoint.x() + offsetX) / imageScaler;
    qreal screenY = (r.height() - sourceImage.height() / imageScaler) / 2 + (sourcePoint.y() + offsetY) / imageScaler;
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
            drawResultsToViewport();
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

void QResultImageView::panAbsolute(double offsetX, double offsetY)
{
    if (offsetX == this->offsetX && offsetY == this->offsetY) {
        return;
    }

    this->offsetX = offsetX;
    this->offsetY = offsetY;

    limitOffset();
    redrawEverything(getInitialTransformationMode());
    considerActivatingSmoothTransformationTimer();

    emit panned();
}

double QResultImageView::getOffsetX() const
{
    return offsetX;
}

double QResultImageView::getOffsetY() const
{
    return offsetY;
}

int QResultImageView::getZoomLevel() const
{
    return zoomLevel;
}

void QResultImageView::setResultPolygons()
{
    // set result polygons to be used for the mouse-on-result test
    resultPolygons.resize(results.size());
    for (size_t i = 0, end = results.size(); i < end; ++i) {
        QPolygonF& resultPolygon = resultPolygons[i];
        const Result& result = results[i];
        resultPolygon.resize(static_cast<int>(result.contour.size()));
        for (size_t j = 0, end = result.contour.size(); j < end; ++j) {
            resultPolygon[static_cast<int>(j)] = result.contour[j];
        }
    }
}

void QResultImageView::updateSourcePyramid()
{
    sourceImagePyramid.clear();
    sourcePixmapPyramid.clear();

    const Qt::TransformationMode mode = transformationMode == AlwaysFastTransformation
            ? Qt::FastTransformation
            : Qt::SmoothTransformation;

    double scaleFactor = 1.0;
    double width = sourceImage.width();
    double height = sourceImage.height();

    const QImage* previous = &sourceImage;
    const double step = 2.0;

    while (width > 50 && height > 50) {
        scaleFactor /= step;
        width /= step;
        height /= step;

        sourceImagePyramid[scaleFactor] = previous->scaled(QSize(std::round(width), std::round(height)), Qt::IgnoreAspectRatio, mode);

        previous = &sourceImagePyramid.rbegin()->second;
    }
}
