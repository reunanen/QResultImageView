#include "QResultImageView.h"
#include <QPainter>
#include <QMouseEvent>
#include <QApplication>
#include <QMessageBox>
#include <qtimer.h>
#include "qt-image-flood-fill/qfloodfill.h"

QResultImageView::DelayedRedrawToken::DelayedRedrawToken()
{}

QResultImageView::DelayedRedrawToken::~DelayedRedrawToken()
{
    for (auto& i : registeredResultImageViews) {
        i.first->redrawEverything(i.second);
    }
}

void QResultImageView::DelayedRedrawToken::registerToBeRedrawnWhenTokenIsDestructed(QResultImageView* resultImageView, const Qt::TransformationMode& transformationMode)
{
    auto i = registeredResultImageViews.find(resultImageView);
    if (i == registeredResultImageViews.end()) {
        registeredResultImageViews[resultImageView] = transformationMode;
    }
    else if (i->second == Qt::FastTransformation && transformationMode == Qt::SmoothTransformation) {
        // Upgrade to a smooth transformation
        i->second = Qt::SmoothTransformation;
    }
    else {
        // Already registered - nothing to do
    }
}

QResultImageView::QResultImageView(QWidget *parent)
    : QWidget(parent)
{
    setLeftMouseMode(LeftMouseMode::Pan);
    setRightMouseMode(RightMouseMode::ResetView);

    setMouseTracking(true);
}

void QResultImageView::setImage(const QImage& image, DelayedRedrawToken* delayedRedrawToken)
{
    sourceImage = image;
    sourcePixmap = QPixmap();
    updateSourcePyramid();

    registerOrRedraw(delayedRedrawToken, getEventualTransformationMode());
}

void QResultImageView::setMask(const QImage& mask, DelayedRedrawToken* delayedRedrawToken)
{
    if (!mask.isNull()) {
        maskPixmap.convertFromImage(mask);
        updateMaskPyramid(false);
    }
    else {
        maskPixmap = QPixmap();
        maskPixmapPyramid.clear();

        croppedMask = QPixmap();
        scaledAndCroppedMask = QPixmap();
    }

    registerOrRedraw(delayedRedrawToken, getEventualTransformationMode());
}

void QResultImageView::setImagePyramid(const std::vector<QImage>& imagePyramid, DelayedRedrawToken* delayedRedrawToken)
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

    registerOrRedraw(delayedRedrawToken, getEventualTransformationMode());
}

void QResultImageView::setResults(const std::vector<Result>& results, DelayedRedrawToken* delayedRedrawToken)
{
    this->results = results;
    setResultPolygons();

    if (delayedRedrawToken == nullptr) {
        drawResultsToViewport();
        update();
    }
    else {
        delayedRedrawToken->registerToBeRedrawnWhenTokenIsDestructed(this, getEventualTransformationMode()); // in fact update would be enough even here
    }
}

void QResultImageView::registerOrRedraw(DelayedRedrawToken* delayedRedrawToken, const Qt::TransformationMode& transformationMode)
{
    if (delayedRedrawToken != nullptr) {
        delayedRedrawToken->registerToBeRedrawnWhenTokenIsDestructed(this, transformationMode);
    }
    else {
        redrawEverything(transformationMode);
    }
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

void QResultImageView::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.drawPixmap(destinationRect, scaledAndCroppedSourceWithResults);

    if (!std::isnan(pixelSize)) {
        drawYardstick(painter);
    }
}

bool isLeftButton(const QMouseEvent* event)
{
    return event->buttons() & Qt::LeftButton;
}

bool isRightButton(const QMouseEvent* event)
{
    return event->buttons() & Qt::RightButton;
}

bool isLeftOrRightButton(const QMouseEvent* event)
{
    const auto buttons = event->buttons();
    return (buttons & Qt::LeftButton) || (buttons & Qt::RightButton);
}

void QResultImageView::mousePressEvent(QMouseEvent *event)
{
    if (isLeftOrRightButton(event)) {
        checkMouseMark(event);
    }
}

void QResultImageView::mouseMoveEvent(QMouseEvent *event)
{
    const double scaleFactor = getScaleFactor();
    if (std::isnan(scaleFactor)) {
        return;
    }

    checkMousePan(event);
    checkMouseOnResult(event);

    const QPointF sourceCoordinate = screenToSourceActual(event->pos());

    // Need to truncate here; rounding isn't the correct thing to do
    QPoint point(static_cast<int>(sourceCoordinate.x()), static_cast<int>(sourceCoordinate.y()));
    int pixelIndex = -1;
    if (sourceImage.format() == QImage::Format_Indexed8 && sourceImage.valid(point)) {
        pixelIndex = sourceImage.pixelIndex(point);
    }
    emit mouseAtCoordinates(sourceCoordinate, pixelIndex);
}

void QResultImageView::mouseReleaseEvent(QMouseEvent* /*event*/)
{
    if (maskDirty) {
        emit maskUpdated();
        maskDirty = false;
    }
}

void QResultImageView::leaveEvent(QEvent*)
{
    emit mouseLeft();
}

void QResultImageView::checkMousePan(const QMouseEvent *event)
{
    if (isLeftOrRightButton(event)) {

        const auto isPan = [&]() {
            if (isLeftButton(event) && leftMouseMode == LeftMouseMode::Pan) {
                return true;
            }
            if (isRightButton(event) && rightMouseMode == RightMouseMode::Pan) {
                return true;
            }
            return false;
        };

        if (isPan()) {
            if (hasPreviousMouseCoordinates) {
                const double imageScaler = getImageScaler();
                offsetX += (event->x() - previousMouseX) * imageScaler;
                offsetY += (event->y() - previousMouseY) * imageScaler;
                limitOffset();
                redrawEverything(getInitialTransformationMode());
                considerActivatingSmoothTransformationTimer();

                emit panned();
            }
        }
        else {
            checkMouseMark(event);
        }
    }

    hasPreviousMouseCoordinates = true;
    previousMouseX = event->x();
    previousMouseY = event->y();
}

void QResultImageView::checkMouseMark(const QMouseEvent* event)
{
    Q_ASSERT(isLeftOrRightButton(event));

    if (sourceImage.size().isEmpty()) {
        return;
    }

    const bool isAnnotating = isLeftButton(event) && leftMouseMode == LeftMouseMode::Annotate;
    const bool isErasing = !isAnnotating && ((isLeftButton(event) && leftMouseMode == LeftMouseMode::EraseAnnotations) || (isRightButton(event) && rightMouseMode == RightMouseMode::EraseAnnotations));

    if (isAnnotating || isErasing) {
        if (!maskVisible) {
            const int answer = QMessageBox::question(this, tr("Can't do that - at least as such"), tr("The annotations can be edited only when visible.\n\nMake the annotations visible?"));
            if (answer == QMessageBox::Yes) {
                setMaskVisible(true);
                emit annotationsVisible(true);
            }
            return;
        }
    }

    bool update = false;
    Qt::TransformationMode transformationMode = getInitialTransformationMode();

    const auto draw = [&](const QColor& color) {
        // draw ellipse

        const double effectiveMarkingRadius = markingRadius * getImageScaler();

        const QPointF screenPoint(event->x(), event->y());
        const QPointF sourcePoint = screenToSourceActual(screenPoint);

        const auto draw = [&](QPixmap& pixmap, double scaleFactor) {

            if (floodFillMode) {
                QApplication::setOverrideCursor(Qt::WaitCursor);

                const int x = static_cast<int>(sourcePoint.x() * scaleFactor);
                const int y = static_cast<int>(sourcePoint.y() * scaleFactor);
                QPoint center(x, y);

                FloodFill(pixmap, center, color);

                QApplication::restoreOverrideCursor();

                transformationMode = getEventualTransformationMode();
            }
            else {
                QPainter painter(&pixmap);
                painter.setPen(color);
                painter.setBrush(color);
                painter.setCompositionMode(QPainter::CompositionMode_Source);

                const QPoint endPoint(static_cast<int>(sourcePoint.x() * scaleFactor), static_cast<int>(sourcePoint.y() * scaleFactor));

                const auto getStartPoint = [&]() {
                    if (hasPreviousMouseCoordinates) {
                        const QPointF previousSourcePoint(screenToSourceActual(QPoint(previousMouseX, previousMouseY)));
                        const QPoint startPoint(static_cast<int>(previousSourcePoint.x() * scaleFactor), static_cast<int>(previousSourcePoint.y() * scaleFactor));
                        return startPoint;
                    }
                    else {
                        return endPoint;
                    }
                };

                const QPoint startPoint = getStartPoint();

                const int manhattanLength = (startPoint - endPoint).manhattanLength();

                QPoint previousCenter;

                for (int i = 0; i <= manhattanLength; ++i) {
                    const QPoint center = startPoint + (endPoint - startPoint) * i / std::max(0, manhattanLength);

                    if (i == 0 || center != previousCenter) {
                        if (effectiveMarkingRadius * scaleFactor <= 0.5) {
                            painter.drawPoint(center);
                        }
                        else {
                            const int r = static_cast<int>(std::round(effectiveMarkingRadius * scaleFactor));
                            painter.drawEllipse(center, r, r);
                        }
                        previousCenter = center;
                    }
                }
            }
        };

        draw(maskPixmap, 1.0);

        for (auto& maskPixmapPyramidItem : maskPixmapPyramid) {
            draw(maskPixmapPyramidItem.second, maskPixmapPyramidItem.first);
        }

        update = true;
    };

    if (isAnnotating) {
        if (maskPixmap.isNull()) {
            QApplication::setOverrideCursor(Qt::WaitCursor);
            QApplication::processEvents(); // actually update the cursor

            maskPixmap = QPixmap(sourceImage.width(), sourceImage.height());
            maskPixmap.fill(Qt::transparent);
            updateMaskPyramid(true);

            QApplication::restoreOverrideCursor();
        }

        draw(annotationColor);
    }
    else if (isErasing) {
        if (!maskPixmap.isNull()) {
            draw(Qt::transparent);
        }
    }
    else if (isRightButton(event) && rightMouseMode == RightMouseMode::ResetView) {
        resetZoomAndPan();
    }

    if (update) {
        redrawEverything(transformationMode);
        considerActivatingSmoothTransformationTimer();
        maskDirty = true;
        emit maskUpdating();
    }
}

void QResultImageView::checkMouseOnResult(const QMouseEvent *event)
{
    const QPointF screenPoint(event->x(), event->y());
    const QPointF sourcePoint = screenToSourceActual(screenPoint);

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
    if ((event->modifiers() & Qt::ControlModifier) && (leftMouseMode == LeftMouseMode::Annotate || leftMouseMode == LeftMouseMode::EraseAnnotations)) {
        // Ask the main application to change the marking radius
        const int magnitude = std::max(abs(event->delta()) * markingRadius / 1000, 1);
        const int sign = event->delta() > 0 ? 1 : (event->delta() < 0 ? -1 : 0);
        emit newMarkingRadius(markingRadius + sign * magnitude);
    }
    else {
        const int zoomMultiplier
                = (event->modifiers() & Qt::ShiftModifier)
                ? 20
                : 4;

        const int newZoomLevel = std::min(std::max(zoomLevel + zoomMultiplier * event->delta(), 0), getMaxZoomLevel());

        const QPointF point = event->posF();

        zoom(newZoomLevel, &point);
    }
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
        QPointF sourcePointBefore = screenToSourceIdeal(point);

        zoomLevel = newZoomLevel;

        const QPointF newScreenPos = sourceToScreenIdeal(sourcePointBefore);
        const QPointF offsetChange = (newScreenPos - point) * getImageScaler();

        offsetX -= offsetChange.x();
        offsetY -= offsetChange.y();

        QPointF sourcePointAfter = screenToSourceIdeal(point);

        Q_ASSERT(fabs(sourcePointBefore.x() - sourcePointAfter.x()) < 1e-6);
        Q_ASSERT(fabs(sourcePointBefore.y() - sourcePointAfter.y()) < 1e-6);

        limitOffset();

        redrawEverything(getInitialTransformationMode());
        considerActivatingSmoothTransformationTimer();

        emit zoomed();
    }
}

void QResultImageView::resizeEvent(QResizeEvent* /*event*/)
{
    if (!std::isnan(getScaleFactor())) {
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

    if (!std::isnan(scaleFactor)) {
        updateViewport(transformationMode);
        drawResultsToViewport();
    }
    else {
        scaledAndCroppedSourceWithResults.fill(Qt::transparent);
    }

    update();
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

std::pair<double, const QPixmap*> QResultImageView::getMaskPixmap(double scaleFactor)
{
    if (scaleFactor > maskPixmapPyramid.rbegin()->first) {
        return std::make_pair(1.0, &maskPixmap);
    }
    else {
        auto i = maskPixmapPyramid.begin();
        double foundScaleFactor = i->first;
        const QPixmap* correspondingPixmap = &i->second;
        while (scaleFactor > i->first) {
            Q_ASSERT(i != maskPixmapPyramid.end());
            ++i;
            foundScaleFactor = i->first;
            correspondingPixmap = &i->second;
        }
        Q_ASSERT(i != maskPixmapPyramid.end());
        return std::make_pair(foundScaleFactor, correspondingPixmap);
    }
}

void QResultImageView::drawResultsToViewport()
{
    bool showMask = maskVisible && !scaledAndCroppedMask.isNull();
    bool showResults = resultsVisible && !results.empty();

    if (!showMask && !showResults) {
        scaledAndCroppedSourceWithResults = scaledAndCroppedSource;
    }
    else {
        scaledAndCroppedSourceWithResults = scaledAndCroppedSource.copy();

        QPainter resultPainter(&scaledAndCroppedSourceWithResults);

        if (showMask) {
            resultPainter.drawPixmap(0, 0, scaledAndCroppedMask);
        }

        if (showResults) {
            const double scaleFactor = getScaleFactor();

            const double zoomCenterX = sourceImage.width() / 2 - offsetX;
            const double zoomCenterY = sourceImage.height() / 2 - offsetY;

            const double srcVisibleWidth = getSourceImageVisibleWidth();
            const double srcVisibleHeight = getSourceImageVisibleHeigth();

            const double srcLeft = std::max(0.0, zoomCenterX - srcVisibleWidth / 2);
            const double srcTop = std::max(0.0, zoomCenterY - srcVisibleHeight / 2);

            for (const Result& result : results) {
                resultPainter.setPen(result.pen);
                if (!result.contour.empty()) {
                    std::vector<QPoint> scaledContour(result.contour.size());
                    bool allPointsAreSameWhenScaled = true;
                    QPoint firstPointScaled;
                    for (size_t i = 0, end = result.contour.size(); i < end; ++i) {
                        const QPointF& point = result.contour[i];
                        QPoint& scaledPoint = scaledContour[i];
                        scaledPoint.setX(static_cast<int>(std::round(point.x() - srcLeft) * scaleFactor));
                        scaledPoint.setY(static_cast<int>(std::round(point.y() - srcTop) * scaleFactor));
                        if (i == 0) {
                            firstPointScaled = scaledPoint;
                        }
                        else if (allPointsAreSameWhenScaled) {
                            if (scaledPoint.x() != firstPointScaled.x() || scaledPoint.y() != firstPointScaled.y()) {
                                allPointsAreSameWhenScaled = false;
                            }
                        }
                    }
                    if (allPointsAreSameWhenScaled) {
                        resultPainter.drawPoint(firstPointScaled);
                    }
                    else {
                        resultPainter.drawPolygon(scaledContour.data(), static_cast<int>(scaledContour.size()));
                    }
                }
            }
        }
    }
}

void QResultImageView::updateViewport(Qt::TransformationMode transformationMode)
{
    const double scaleFactor = getScaleFactor();

    Q_ASSERT(!std::isnan(scaleFactor));

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

    const QPointF dstTopLeft = sourceToScreenIdeal(QPointF(srcLeft, srcTop));
    const QPointF dstBottomRight = sourceToScreenIdeal(QPointF(srcRight, srcBottom));

    QPointF srcTopLeft = screenToSourceIdeal(dstTopLeft);
    QPointF srcBottomRight = screenToSourceIdeal(dstBottomRight);

    const QPointF scaledSourceTopLeft = QPointF(srcTopLeft.x() * sourceScaleFactorX, srcTopLeft.y() * sourceScaleFactorY);
    const QPointF scaledSourceBottomRight = QPointF(srcBottomRight.x() * sourceScaleFactorX, srcBottomRight.y() * sourceScaleFactorY);

    Q_ASSERT(fabs(srcTopLeft.x() - srcLeft) < 1e-6);
    Q_ASSERT(fabs(srcTopLeft.y() - srcTop) < 1e-6);
    Q_ASSERT(fabs(srcBottomRight.x() - srcRight) < 1e-6);
    Q_ASSERT(fabs(srcBottomRight.y() - srcBottom) < 1e-6);

#ifdef _DEBUG
    const double scaledSourceLeft = srcLeft * sourceScaleFactorX;
    const double scaledSourceRight = srcRight * sourceScaleFactorX;
    const double scaledSourceTop = srcTop * sourceScaleFactorY;
    const double scaledSourceBottom = srcBottom * sourceScaleFactorY;

    Q_ASSERT(fabs(scaledSourceTopLeft.x() - scaledSourceLeft) < 1e-6);
    Q_ASSERT(fabs(scaledSourceTopLeft.y() - scaledSourceTop) < 1e-6);
    Q_ASSERT(fabs(scaledSourceBottomRight.x() - scaledSourceRight) < 1e-6);
    Q_ASSERT(fabs(scaledSourceBottomRight.y() - scaledSourceBottom) < 1e-6);
#endif

    const auto roundedRect = [](const QPointF& topLeft, const QPointF& bottomRight) {
        const int x = static_cast<int>(round(topLeft.x()));
        const int y = static_cast<int>(round(topLeft.y()));
        const int width = static_cast<int>(round(bottomRight.x() - topLeft.x()));
        const int height = static_cast<int>(round(bottomRight.y() - topLeft.y()));
        return QRect(x, y, width, height);
    };

    croppedSourceRect = roundedRect(scaledSourceTopLeft, scaledSourceBottomRight);
    croppedSource = scaledSource.second->copy(croppedSourceRect);

    const int scaledWidth = static_cast<int>(std::round(scaleFactor / scaledSource.first * croppedSource.width()));
    const int scaledHeight = static_cast<int>(std::round(scaleFactor / scaledSource.first * croppedSource.height()));
    scaledAndCroppedSource = croppedSource.scaled(QSize(scaledWidth, scaledHeight), Qt::IgnoreAspectRatio, transformationMode);

    if (!maskPixmap.isNull()) {
        const std::pair<double, const QPixmap*> scaledMask = getMaskPixmap(scaleFactor);
        croppedMask = scaledMask.second->copy(croppedSourceRect);
        scaledAndCroppedMask = croppedMask.scaled(QSize(scaledWidth, scaledHeight), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }

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

QPointF QResultImageView::screenToSourceIdeal(const QPointF& screenPoint) const
{
    const double imageScaler = getImageScaler();
    const QRect r(rect());
    qreal sourceX = screenPoint.x() * imageScaler - (r.width() * imageScaler - sourceImage.width()) / 2 - offsetX;
    qreal sourceY = screenPoint.y() * imageScaler - (r.height() * imageScaler - sourceImage.height()) / 2 - offsetY;
    return QPointF(sourceX, sourceY);
}

QPointF QResultImageView::sourceToScreenIdeal(const QPointF& sourcePoint) const
{
    const double imageScaler = getImageScaler();
    const QRect r(rect());
    qreal screenX = (r.width() - sourceImage.width() / imageScaler) / 2 + (sourcePoint.x() + offsetX) / imageScaler;
    qreal screenY = (r.height() - sourceImage.height() / imageScaler) / 2 + (sourcePoint.y() + offsetY) / imageScaler;
    return QPointF(screenX, screenY);
}

QPointF QResultImageView::screenToSourceActual(const QPointF& screenPoint) const
{
    const double scaleFactor = getScaleFactor();

    Q_ASSERT(!std::isnan(scaleFactor));

    const std::pair<double, const QPixmap*> scaledSource = getSourcePixmap(scaleFactor);

    // these two should be approximately equal
    const double sourceScaleFactorX = scaledSource.second->width() / static_cast<double>(sourceImage.width());
    const double sourceScaleFactorY = scaledSource.second->height() / static_cast<double>(sourceImage.height());

    const qreal sourceX = (screenPoint.x() - destinationRect.x()) * croppedSourceRect.width() / sourceScaleFactorX / destinationRect.width() + croppedSourceRect.x() / sourceScaleFactorX;
    const qreal sourceY = (screenPoint.y() - destinationRect.y()) * croppedSourceRect.height() / sourceScaleFactorY / destinationRect.height() + croppedSourceRect.y() / sourceScaleFactorY;

    return QPointF(sourceX, sourceY);
}

QPointF QResultImageView::sourceToScreenActual(const QPointF& sourcePoint) const
{
    const double scaleFactor = getScaleFactor();

    Q_ASSERT(!std::isnan(scaleFactor));

    const std::pair<double, const QPixmap*> scaledSource = getSourcePixmap(scaleFactor);

    // these two should be approximately equal
    const double sourceScaleFactorX = scaledSource.second->width() / static_cast<double>(sourceImage.width());
    const double sourceScaleFactorY = scaledSource.second->height() / static_cast<double>(sourceImage.height());

    const qreal sourceX = (sourcePoint.x() - croppedSourceRect.x() / sourceScaleFactorX) * destinationRect.width() / croppedSourceRect.width() * sourceScaleFactorX + destinationRect.x();
    const qreal sourceY = (sourcePoint.y() - croppedSourceRect.y() / sourceScaleFactorY) * destinationRect.height() / croppedSourceRect.height() * sourceScaleFactorY + destinationRect.y();

    return QPointF(sourceX, sourceY);
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

void QResultImageView::setMaskVisible(bool visible)
{
    if (maskVisible != visible) {
        maskVisible = visible;

        if (!maskPixmap.isNull()) {
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

void QResultImageView::setPixelSize(double pixelSize, const QString& unit, bool unitIsSI)
{
    this->pixelSize = pixelSize;
    this->pixelSizeUnit = unit;
    this->pixelSizeUnitIsSI = unitIsSI;
    update();
}

void QResultImageView::drawYardstick(QPainter& painter)
{
    const double imageScaler = getImageScaler();

    if (std::isnan(imageScaler)) {
        return;
    }

    const QRect r = rect();

    const int margin = 20;

    const auto getYardstickSize = [&](int rectDimension) {
        const double maxYardstickSize = (rectDimension - 2 * margin) * pixelSize * imageScaler;

        // round down to the nearest power of 10
        double yardstickSize = pow(10, floor(log10(maxYardstickSize)));

        if (2 * yardstickSize <= maxYardstickSize) {
            yardstickSize = 2 * yardstickSize;
        }
        if (2.5 * yardstickSize <= maxYardstickSize) {
            yardstickSize = 2.5 * yardstickSize;
        }

        return yardstickSize;
    };

    const auto getYardstickText = [this](double yardstickSize) {
        if (yardstickSize < 1e-3) {
            if (pixelSizeUnitIsSI) {
                return QString::number(yardstickSize * 1e6, 'g') + " µ" + pixelSizeUnit;
            }
            else {
                return QString::number(yardstickSize, 'f', 6) + " " + pixelSizeUnit;
            }
        }
        else if (yardstickSize < 1) {
            if (pixelSizeUnitIsSI) {
                return QString::number(yardstickSize * 1e3, 'f', 0) + " m" + pixelSizeUnit;
            }
            else {
                return QString::number(yardstickSize, 'f', 3) + " " + pixelSizeUnit;
            }
        }
        else {
            return QString::number(yardstickSize, 'f', 0) + " " + pixelSizeUnit;
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
        const double yardstickSizeX = getYardstickSize(r.width());

        const int y = r.height() - margin;
        const int w = std::round(yardstickSizeX / pixelSize / imageScaler);

        painter.setPen(Qt::white);
        painter.drawLine(margin, y - 1, margin + w, y - 1);

        painter.setPen(Qt::black);
        painter.drawLine(margin, y, margin + w, y);

        drawOutlinedText(margin, y, w, margin, Qt::AlignRight | Qt::AlignTop, getYardstickText(yardstickSizeX));
    }

    if (r.height() > 8 * margin && r.width() > 2 * margin) {
        const double yardstickSizeY = getYardstickSize(r.height());

        const int origin = r.height() - margin;
        const int h = std::round(yardstickSizeY / pixelSize / imageScaler);

        painter.setPen(Qt::white);
        painter.drawLine(margin + 1, origin - h, margin + 1, origin - 1);

        painter.setPen(Qt::black);
        painter.drawLine(margin, origin - h, margin, origin);

        painter.rotate(-90);
        drawOutlinedText(-origin, 0, h, margin, Qt::AlignRight | Qt::AlignBottom, getYardstickText(yardstickSizeY));
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

void QResultImageView::updateMaskPyramid(bool isEmpty)
{
    //maskImagePyramid.clear();
    maskPixmapPyramid.clear();

    const Qt::TransformationMode mode = transformationMode == AlwaysFastTransformation
            ? Qt::FastTransformation
            : Qt::SmoothTransformation;

    double scaleFactor = 1.0;
    double width = sourceImage.width();
    double height = sourceImage.height();

    const QPixmap* previous = &maskPixmap;
    const double step = 2.0;

    while (width > 50 && height > 50) {
        scaleFactor /= step;
        width /= step;
        height /= step;

        const QSize scaledSize(std::round(width), std::round(height));

        if (isEmpty) {
            maskPixmapPyramid[scaleFactor] = QPixmap(scaledSize);
            maskPixmapPyramid[scaleFactor].fill(Qt::transparent);
        }
        else {
            maskPixmapPyramid[scaleFactor] = previous->scaled(scaledSize, Qt::IgnoreAspectRatio, mode);
        }

        previous = &maskPixmapPyramid.rbegin()->second;
    }
}

void QResultImageView::setLeftMouseMode(LeftMouseMode leftMouseMode)
{
    this->leftMouseMode = leftMouseMode;
    updateCursor();
}

void QResultImageView::setRightMouseMode(RightMouseMode rightMouseMode)
{
    this->rightMouseMode = rightMouseMode;
}

void QResultImageView::updateCursor()
{
    // TODO: make cursor depend on marking radius (or show what would be marked directly on the pixmap)

    const auto getAnnotationCursor = [this]() {
        QPixmap pixmap(2 * markingRadius + 1, 2 * markingRadius + 1);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setPen(Qt::black);
        if (leftMouseMode == LeftMouseMode::Annotate) {
            painter.setBrush(annotationColor);
        }
        else {
            Q_ASSERT(leftMouseMode == LeftMouseMode::EraseAnnotations);
            painter.setBrush(Qt::transparent);
        }
        painter.drawEllipse(0, 0, pixmap.width() - 1, pixmap.height() - 1);

        QCursor cursor(pixmap, markingRadius, markingRadius);
        return cursor;
    };

    switch(leftMouseMode) {
    case LeftMouseMode::Pan: setCursor(Qt::SizeAllCursor); break;
    case LeftMouseMode::Annotate: setCursor(floodFillMode ? bucketCursor : getAnnotationCursor()); break;
    case LeftMouseMode::EraseAnnotations: setCursor(floodFillMode ? bucketCursor : getAnnotationCursor()); break;
    default: Q_ASSERT(false);
    }
}

void QResultImageView::setAnnotationColor(QColor color)
{
    this->annotationColor = color;
    updateCursor();
}

void QResultImageView::setMarkingRadius(int newMarkingRadius)
{
    markingRadius = newMarkingRadius;
    updateCursor();
}

void QResultImageView::setFloodFillMode(bool floodFill)
{
    floodFillMode = floodFill;
    updateCursor();
}

const QPixmap& QResultImageView::getMask()
{
    return maskPixmap;
}

void QResultImageView::setBucketCursor(const QCursor& cursor)
{
    bucketCursor = cursor;
}
