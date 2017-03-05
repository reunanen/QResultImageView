#ifndef QRESULTIMAGEVIEW_H
#define QRESULTIMAGEVIEW_H

#include <QWidget>

class QResultImageView : public QWidget
{
    Q_OBJECT

public:
    explicit QResultImageView(QWidget *parent);

    void setImage(const QImage& image);

protected:
    void paintEvent(QPaintEvent* event);

private:
    QPixmap pixmap;
};

#endif // QRESULTIMAGEVIEW_H
