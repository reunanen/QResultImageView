#ifndef QRESULTIMAGEVIEW_H
#define QRESULTIMAGEVIEW_H

#include <QWidget>
#include <qpen.h>

class QResultImageView : public QWidget
{
    Q_OBJECT

public:
    explicit QResultImageView(QWidget *parent);

    void setImage(const QImage& image);

    struct Result {
        QPen pen;
        std::vector<QPointF> contour;
    };

    typedef std::vector<Result> Results;

    void setResults(const Results& results);

    void setImageAndResults(const QImage& image, const Results& results);

    enum TransformationMode {
        AlwaysFastTransformation, // most responsive, but may not look great on some images
        SmoothTransformationWhenZoomedOut, // least responsive, but may look best
        DelayedSmoothTransformationWhenZoomedOut // responsive and eventually good-looking
    };

    void setTransformationMode(TransformationMode newTransformationMode);

    void setResultsVisible(bool visible);

    void resetZoomAndPan();

    // Has the user panned the view, or zoomed in or out? False if not.
    bool isDefaultZoomAndPan() const;

    void setPixelSizeInMeters(double pixelSizeInMeters);

    void panAbsolute(double offsetX, double offsetY);
    void panRelative(double offsetX, double offsetY); // TODO

signals:
    void panned();
    void zoomed();
    void mouseOnResult(size_t resultIndex);
    void mouseNotOnResult();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

    double getOffsetX() const;
    double getOffsetY() const;

private slots:
    void performSmoothTransformation();

private:
    void redrawEverything(Qt::TransformationMode transformationMode);

    void updateViewport(Qt::TransformationMode transformationMode);
    void drawResultsToViewport();

    double getScaleFactor() const;

    double getSourceImageVisibleWidth() const;
    double getSourceImageVisibleHeigth() const;

    // The magnification required to fit the full source in the destination window when zoomLevel = 0.
    double getDefaultMagnification() const;

    // Zoom level selected by mouse wheel or so.
    // Returns a value in the range ]0.0, 1.0] as follows:
    // 1.0 means no zooming-in; assuming there's no offset, the full source image fits in the destination window
    // 0.0 would mean that one source pixel is represented using an infinite number of screen pixels
    double getEffectiveZoomLevel() const;

    // The effective zoom level multiplied by the default magnification.
    double getImageScaler() const;

    // The max zoom level depends on the source image size.
    int getMaxZoomLevel() const;

    void limitOffset();

    void drawYardstick(QPainter& painter);

    Qt::TransformationMode getInitialTransformationMode() const;
    Qt::TransformationMode getEventualTransformationMode() const;

    void considerActivatingSmoothTransformationTimer();

    QPointF screenToSource(const QPointF& screenPoint) const;
    QPointF sourceToScreen(const QPointF& sourcePoint) const;

    void checkMousePan(const QMouseEvent* event);
    void checkMouseOnResult(const QMouseEvent* event);

    void setResultPolygons();

    void updateSourcePyramid();

    std::pair<double, const QPixmap*> getSourcePixmap(double scaleFactor) const;

    QPixmap source;
    std::map<double, QPixmap> sourcePyramid;
    QPixmap croppedSource;
    QPixmap scaledAndCroppedSource;
    QPixmap scaledAndCroppedSourceWithResults;

    QRect destinationRect;

    std::vector<QPolygonF> resultPolygons;

    int zoomLevel = 0;
    double offsetX = 0;
    double offsetY = 0;

    bool hasPreviousMouseCoordinates = 0;
    int previousMouseX = 0;
    int previousMouseY = 0;

    size_t mouseOnResultIndex = -1;

    Results results;

    TransformationMode transformationMode = DelayedSmoothTransformationWhenZoomedOut;
    int smoothTransformationPendingCounter = 0;

    bool resultsVisible = true;

    double pixelSize_m = std::numeric_limits<double>::quiet_NaN();
};

#endif // QRESULTIMAGEVIEW_H
