#include "ui/connection_info_dialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ConnectionInfoDialog::ConnectionInfoDialog(const QString& endpoint,
                                           bool connected,
                                           QWidget* parent)
    : QDialog(parent) {
  setWindowTitle("连接信息");
  setMinimumWidth(440);
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
  auto* stateLabel = new QLabel(connected ? "已连接" : "未连接");
  form->addRow("Endpoint", endpointLabel);
  form->addRow("状态", stateLabel);
  root->addLayout(form);

  auto* buttonRow = new QHBoxLayout();
  buttonRow->addStretch(1);
  auto* reconnectButton = new QPushButton("重新连接...");
  reconnectButton->setObjectName("primaryButton");
  auto* closeButton = new QPushButton("关闭");
  buttonRow->addWidget(closeButton);
  buttonRow->addWidget(reconnectButton);
  root->addLayout(buttonRow);

  QObject::connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);
  QObject::connect(reconnectButton, &QPushButton::clicked, this, [this]() {
    reconnectRequested_ = true;
    accept();
  });
}

bool ConnectionInfoDialog::reconnectRequested() const {
  return reconnectRequested_;
}

