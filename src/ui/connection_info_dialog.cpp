#include "ui/connection_info_dialog.h"

#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

ConnectionInfoDialog::ConnectionInfoDialog(const QString& endpoint,
                                           bool connected,
                                           QWidget* parent)
    : QDialog(parent) {
  setWindowTitle("连接信息");
  setFixedSize(560, 300);
  setModal(true);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(22, 22, 22, 22);
  root->setSpacing(14);

  auto* title = new QLabel("当前连接");
  title->setObjectName("dialogTitle");
  root->addWidget(title);

  auto* form = new QFormLayout();
  auto* endpointLabel = new QLabel(endpoint.isEmpty() ? "未连接" : endpoint);
  endpointLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  stateLabel_ = new QLabel(connected ? "已连接" : "未连接");
  latencyLabel_ = new QLabel(connected ? "检测中..." : "未连接");
  ipLabel_ = new QLabel(connected ? "检测中..." : "未连接");
  versionLabel_ = new QLabel(connected ? "检测中..." : "未连接");
  siteTitleLabel_ = new QLabel(connected ? "检测中..." : "未连接");
  messageLabel_ = new QLabel(" ");
  messageLabel_->setObjectName("subtleLabel");
  messageLabel_->setWordWrap(true);
  ipLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  versionLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  siteTitleLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  form->addRow("Endpoint", endpointLabel);
  form->addRow("状态", stateLabel_);
  form->addRow("延迟", latencyLabel_);
  form->addRow("IP", ipLabel_);
  form->addRow("远端版本", versionLabel_);
  form->addRow("站点标题", siteTitleLabel_);
  root->addLayout(form);
  root->addWidget(messageLabel_);
}

void ConnectionInfoDialog::setDiagnostics(
    const ConnectionDiagnostics& diagnostics) {
  if (stateLabel_) {
    stateLabel_->setText(diagnostics.connected ? "已连接" : "未连接");
  }
  if (latencyLabel_) {
    latencyLabel_->setText(diagnostics.latencyMs >= 0
                               ? QString::number(diagnostics.latencyMs) + " ms"
                               : "未知");
  }
  if (ipLabel_) {
    ipLabel_->setText(diagnostics.ipAddress.isEmpty()
                          ? "未知"
                          : diagnostics.ipAddress);
  }
  if (versionLabel_) {
    versionLabel_->setText(diagnostics.version.isEmpty()
                               ? "未知"
                               : diagnostics.version);
  }
  if (siteTitleLabel_) {
    siteTitleLabel_->setText(diagnostics.siteTitle.isEmpty()
                                 ? "未知"
                                 : diagnostics.siteTitle);
  }
  if (messageLabel_) {
    if (diagnostics.publicSettingsOk) {
      messageLabel_->setText("公共设置接口可用。");
    } else if (!diagnostics.message.isEmpty()) {
      messageLabel_->setText("公共设置接口不可用: " + diagnostics.message);
    } else {
      messageLabel_->setText(" ");
    }
  }
}
