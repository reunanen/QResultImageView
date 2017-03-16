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

    void setZoomedOutTransformationMode(Qt::TransformationMode transformationMode);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void performSmoothTransformation();

private:
    void redrawEverything(Qt::TransformationMode transformationMode);

    void updateScaledSourceImage(Qt::TransformationMode transformationMode);
    double getScaleFactor() const;
    void drawResultsOnScaledSourceImage();
    void updateCroppedSourceImageAndDestinationRect();

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

    Qt::TransformationMode getDesiredTransformationMode() const;

    bool isSmoothTransformationDesired() const;

    void considerActivatingSmoothTransformationTimer();

    QPointF screenToSource(const QPointF& screenPoint) const;
    QPointF sourceToScreen(const QPointF& sourcePoint) const;

    QPixmap source;
    QPixmap scaledSource;
    QPixmap scaledSourceWithResults;
    QPixmap croppedSource;

    QRect destinationRect;

    int zoomLevel = 0;
    double offsetX = 0;
    double offsetY = 0;

    bool hasPreviousMouseCoordinates = 0;
    int previousMouseX = 0;
    int previousMouseY = 0;

    Results results;

    Qt::TransformationMode zoomedOutTransformationMode = Qt::SmoothTransformation;
    int smoothTransformationPendingCounter = 0;
};

#endif // QRESULTIMAGEVIEW_H
