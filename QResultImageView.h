#ifndef QRESULTIMAGEVIEW_H
#define QRESULTIMAGEVIEW_H

#include <QWidget>
#include <qpen.h>
#include <memory>

class QResultImageView : public QWidget
{
    Q_OBJECT

public:
    explicit QResultImageView(QWidget *parent);

    // RAII token that invokes a redraw of the registered view(s) when it goes out of scope.
    // NB: the token is expected to go out of scope before the registered views themselves!
    class DelayedRedrawToken {
    public:
        DelayedRedrawToken();
        virtual ~DelayedRedrawToken();
        void registerToBeRedrawnWhenTokenIsDestructed(QResultImageView* resultImageView, const Qt::TransformationMode& transformationMode);

    private:
        DelayedRedrawToken(const DelayedRedrawToken&) = delete; // not construction-copyable
        DelayedRedrawToken& operator=(const DelayedRedrawToken&) = delete; // non-assignment-copyable

        std::map<QResultImageView*, Qt::TransformationMode> registeredResultImageViews;
    };

    void setImage(const QImage& image, DelayedRedrawToken* delayedRedrawToken = nullptr);
    void setImagePyramid(const std::vector<QImage>& imagePyramid, DelayedRedrawToken* delayedRedrawToken = nullptr);

    struct Result {
        QPen pen1;
        std::shared_ptr<QPen> pen2; // optional
        std::vector<QPointF> contour;
    };

    typedef std::vector<Result> Results;

    void setAnnotations(const Results& annotations, DelayedRedrawToken* delayedRedrawToken = nullptr);
    void setResults(const Results& results, DelayedRedrawToken* delayedRedrawToken = nullptr);

    enum TransformationMode {
        AlwaysFastTransformation, // most responsive, but may not look great on some images
        SmoothTransformationWhenZoomedOut, // least responsive, but may look best
        DelayedSmoothTransformationWhenZoomedOut // responsive and eventually good-looking
    };

    void redrawEverything(Qt::TransformationMode transformationMode);

    void setTransformationMode(TransformationMode newTransformationMode);

    void setAnnotationsVisible(bool visible);
    void setResultsVisible(bool visible);

    void resetZoomAndPan();

    // Has the user panned the view, or zoomed in or out? False if not.
    bool isDefaultZoomAndPan() const;

    void setPixelSize(double pixelSize, const QString& unit, const bool unitIsSI);

    void panAbsolute(double offsetX, double offsetY);
    void panRelative(double offsetX, double offsetY); // TODO

    void zoom(int newZoomLevel, const QPointF* screenPoint = nullptr);

    // The magnification required to fit the full source in the destination window when zoomLevel = 0.
    double getDefaultMagnification() const;

    enum class LeftMouseMode {
        Pan,
        Annotate,
    };

    enum class RightMouseMode {
        Pan,
        ResetView
    };

    void setLeftMouseMode(LeftMouseMode leftMouseMode);
    void setRightMouseMode(RightMouseMode rightMouseMode);

    const QRectF getAnnotatedSourceRect();

signals:
    void panned();
    void zoomed();
    void mouseOnResult(size_t resultIndex);
    void mouseNotOnResult();
    void mouseAtCoordinates(QPointF sourcePoint, int pixelIndex); // pixelIndex is -1 if it's not valid
    void mouseLeft();
    void annotationUpdating();
    void annotationUpdated();
    void makeAnnotationsVisible(bool visible);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

    double getOffsetX() const;
    double getOffsetY() const;

    // The max zoom level depends on the source image size.
    int getMaxZoomLevel() const;
    int getZoomLevel() const;

private slots:
    void performSmoothTransformation();

private:
    void registerOrRedraw(DelayedRedrawToken* delayedRedrawToken, const Qt::TransformationMode& transformationMode);
    void updateViewport(Qt::TransformationMode transformationMode);
    void drawResults(QPainter& painters, const QPointF& offset);

    const QRect getAnnotatedScreenRect();

    double getScaleFactor() const;

    double getSourceImageVisibleWidth() const;
    double getSourceImageVisibleHeigth() const;

    // Zoom level selected by mouse wheel or so.
    // Returns a value in the range ]0.0, 1.0] as follows:
    // 1.0 means no zooming-in; assuming there's no offset, the full source image fits in the destination window
    // 0.0 would mean that one source pixel is represented using an infinite number of screen pixels
    double getEffectiveZoomLevel() const;

    // The effective zoom level multiplied by the default magnification.
    double getImageScaler() const;

    void limitOffset();

    void drawYardstick(QPainter& painter);

    bool rectangleSizeExceedsMinDimension() const;

    Qt::TransformationMode getInitialTransformationMode() const;
    Qt::TransformationMode getEventualTransformationMode() const;

    void considerActivatingSmoothTransformationTimer();

    QPointF screenToSourceIdeal(const QPointF& screenPoint) const;
    QPointF sourceToScreenIdeal(const QPointF& sourcePoint) const;

    QPointF screenToSourceActual(const QPointF& screenPoint) const;
    QPointF sourceToScreenActual(const QPointF& sourcePoint) const;

    void checkMousePan(const QMouseEvent* event);
    void checkMouseMark(const QMouseEvent* event);
    void checkMouseOnResult(const QMouseEvent* event);

    void setResultPolygons();

    void updateSourcePyramid();

    void updateCursor();

    std::pair<double, const QPixmap*> getSourcePixmap(double scaleFactor) const;

    QImage sourceImage;
    mutable QPixmap sourcePixmap;
    std::map<double, QImage> sourceImagePyramid;
    mutable std::map<double, QPixmap> sourcePixmapPyramid;

    QPixmap croppedSource;
    QPixmap scaledAndCroppedSource;

    QRect croppedSourceRect;
    QRect destinationRect;

    std::vector<QPolygonF> resultPolygons;

    int zoomLevel = 0;
    double offsetX = 0;
    double offsetY = 0;

    bool hasPreviousMouseCoordinates = false;
    int previousMouseX = 0;
    int previousMouseY = 0;

    bool isDrawingRectangle = false;
    int rectangleStartX = 0;
    int rectangleStartY = 0;
    int rectangleCurrentX = 0;
    int rectangleCurrentY = 0;

    size_t mouseOnResultIndex = -1;

    Results annotations;
    Results results;

    TransformationMode transformationMode = DelayedSmoothTransformationWhenZoomedOut;
    int smoothTransformationPendingCounter = 0;

    bool annotationsVisible = true;
    bool resultsVisible = true;

    double pixelSize = std::numeric_limits<double>::quiet_NaN();
    QString pixelSizeUnit;
    bool pixelSizeUnitIsSI = false;

    LeftMouseMode leftMouseMode = LeftMouseMode::Pan;
    RightMouseMode rightMouseMode = RightMouseMode::ResetView;
};

#endif // QRESULTIMAGEVIEW_H
