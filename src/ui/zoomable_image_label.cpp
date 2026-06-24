#include "ui/zoomable_image_label.h"

#include <QtGlobal>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>

namespace {

constexpr double kMinZoom = 0.25;
constexpr double kMaxZoom = 8.0;

}  // namespace

ZoomableImageLabel::ZoomableImageLabel(QWidget* parent) : QLabel(parent) {
  setAlignment(Qt::AlignCenter);
  setMouseTracking(true);
  setFocusPolicy(Qt::WheelFocus);
  setCursor(Qt::ArrowCursor);
}

void ZoomableImageLabel::setImage(const QPixmap& pixmap) {
  pixmap_ = pixmap;
  userAdjusted_ = false;
  resetView();
}

void ZoomableImageLabel::clearImage() {
  pixmap_ = QPixmap();
  offset_ = QPointF();
  zoom_ = 1.0;
  userAdjusted_ = false;
  dragging_ = false;
  setCursor(Qt::ArrowCursor);
  clear();
  update();
}

void ZoomableImageLabel::resetView() {
  updateBaseScale();
  zoom_ = 1.0;
  offset_ = QPointF();
  dragging_ = false;
  userAdjusted_ = false;
  setCursor(Qt::ArrowCursor);
  update();
}

void ZoomableImageLabel::paintEvent(QPaintEvent* event) {
  if (pixmap_.isNull()) {
    QLabel::paintEvent(event);
    return;
  }

  QPainter painter(this);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.fillRect(rect(), palette().base());
  painter.drawPixmap(imageRect(), pixmap_, pixmap_.rect());
}

void ZoomableImageLabel::resizeEvent(QResizeEvent* event) {
  QLabel::resizeEvent(event);
  updateBaseScale();
  if (!userAdjusted_) {
    offset_ = QPointF();
    zoom_ = 1.0;
  }
  clampOffset();
  update();
}

void ZoomableImageLabel::wheelEvent(QWheelEvent* event) {
  if (pixmap_.isNull()) {
    QLabel::wheelEvent(event);
    return;
  }

  const double oldZoom = zoom_;
  const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
  zoom_ = qBound(kMinZoom, zoom_ * factor, kMaxZoom);
  if (qFuzzyCompare(oldZoom, zoom_)) {
    return;
  }

  const QPointF center(width() / 2.0, height() / 2.0);
  const QPointF cursor = event->pos();
  const QPointF imagePoint =
      (cursor - center - offset_) / (baseScale_ * oldZoom);
  offset_ = cursor - center - imagePoint * (baseScale_ * zoom_);
  userAdjusted_ = true;
  clampOffset();
  update();
  event->accept();
}

void ZoomableImageLabel::mousePressEvent(QMouseEvent* event) {
  if (!pixmap_.isNull() && event->button() == Qt::LeftButton) {
    dragging_ = true;
    lastMousePos_ = event->pos();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
    return;
  }
  QLabel::mousePressEvent(event);
}

void ZoomableImageLabel::mouseMoveEvent(QMouseEvent* event) {
  if (dragging_) {
    offset_ += event->pos() - lastMousePos_;
    lastMousePos_ = event->pos();
    userAdjusted_ = true;
    clampOffset();
    update();
    event->accept();
    return;
  }
  QLabel::mouseMoveEvent(event);
}

void ZoomableImageLabel::mouseReleaseEvent(QMouseEvent* event) {
  if (dragging_ && event->button() == Qt::LeftButton) {
    dragging_ = false;
    setCursor(Qt::ArrowCursor);
    event->accept();
    return;
  }
  QLabel::mouseReleaseEvent(event);
}

void ZoomableImageLabel::mouseDoubleClickEvent(QMouseEvent* event) {
  if (!pixmap_.isNull() && event->button() == Qt::LeftButton) {
    resetView();
    event->accept();
    return;
  }
  QLabel::mouseDoubleClickEvent(event);
}

QRectF ZoomableImageLabel::imageRect() const {
  if (pixmap_.isNull()) {
    return {};
  }

  const QSizeF originalSize(pixmap_.size());
  const QSizeF scaledSize(originalSize.width() * baseScale_ * zoom_,
                          originalSize.height() * baseScale_ * zoom_);
  const QPointF topLeft((width() - scaledSize.width()) / 2.0 + offset_.x(),
                        (height() - scaledSize.height()) / 2.0 + offset_.y());
  return QRectF(topLeft, scaledSize);
}

void ZoomableImageLabel::updateBaseScale() {
  if (pixmap_.isNull() || pixmap_.width() <= 0 || pixmap_.height() <= 0) {
    baseScale_ = 1.0;
    return;
  }

  const double widthScale = static_cast<double>(qMax(1, width())) / pixmap_.width();
  const double heightScale =
      static_cast<double>(qMax(1, height())) / pixmap_.height();
  baseScale_ = qMin(widthScale, heightScale);
  if (baseScale_ <= 0.0) {
    baseScale_ = 1.0;
  }
}

void ZoomableImageLabel::clampOffset() {
  if (pixmap_.isNull()) {
    offset_ = QPointF();
    return;
  }

  const QSizeF originalSize(pixmap_.size());
  const QSizeF scaledSize(originalSize.width() * baseScale_ * zoom_,
                          originalSize.height() * baseScale_ * zoom_);
  const double maxX = qMax(0.0, (scaledSize.width() - width()) / 2.0);
  const double maxY = qMax(0.0, (scaledSize.height() - height()) / 2.0);
  offset_.setX(qBound(-maxX, offset_.x(), maxX));
  offset_.setY(qBound(-maxY, offset_.y(), maxY));
}
