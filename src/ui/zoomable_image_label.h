#ifndef OPENLIST_SORTER_UI_ZOOMABLE_IMAGE_LABEL_H
#define OPENLIST_SORTER_UI_ZOOMABLE_IMAGE_LABEL_H

#include <QLabel>
#include <QPixmap>
#include <QPointF>

class ZoomableImageLabel : public QLabel {
 public:
  explicit ZoomableImageLabel(QWidget* parent = nullptr);

  void setImage(const QPixmap& pixmap);
  void clearImage();
  void resetView();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

 private:
  QRectF imageRect() const;
  void updateBaseScale();
  void clampOffset();

  QPixmap pixmap_;
  double baseScale_{1.0};
  double zoom_{1.0};
  QPointF offset_;
  QPointF lastMousePos_;
  bool dragging_{};
  bool userAdjusted_{};
};

#endif
