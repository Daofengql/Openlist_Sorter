#include "ui/remote_directory_dialog.h"

#include <QHBoxLayout>
#include <QListWidgetItem>
#include <QVBoxLayout>

RemoteDirectoryDialog::RemoteDirectoryDialog(OpenListClient* client,
                                             const QString& startPath,
                                             QWidget* parent)
    : QDialog(parent),
      client_(client),
      currentPath_(normalizeRemotePath(startPath)),
      selectedPath_(normalizeRemotePath(startPath)) {
  buildUi();
  connectSignals();
  loadDirectory(currentPath_, false);
}

QString RemoteDirectoryDialog::selectedPath() const {
  return selectedPath_;
}

void RemoteDirectoryDialog::buildUi() {
  setWindowTitle("选择远程目录");
  setMinimumSize(620, 500);
  setModal(true);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(18, 18, 18, 18);
  layout->setSpacing(12);

  auto* topRow = new QHBoxLayout();
  rootButton_ = new QPushButton("根目录");
  upButton_ = new QPushButton("上一级");
  refreshButton_ = new QPushButton("刷新");
  pathEdit_ = new QLineEdit();
  pathEdit_->setReadOnly(true);
  pathEdit_->setPlaceholderText("/");

  topRow->addWidget(rootButton_);
  topRow->addWidget(upButton_);
  topRow->addWidget(refreshButton_);
  topRow->addWidget(pathEdit_, 1);
  layout->addLayout(topRow);

  directoryList_ = new QListWidget();
  directoryList_->setAlternatingRowColors(true);
  directoryList_->setSelectionMode(QAbstractItemView::SingleSelection);
  layout->addWidget(directoryList_, 1);

  statusLabel_ = new QLabel();
  statusLabel_->setWordWrap(true);
  statusLabel_->setObjectName("subtleLabel");
  layout->addWidget(statusLabel_);

  auto* buttonRow = new QHBoxLayout();
  buttonRow->addStretch(1);
  cancelButton_ = new QPushButton("取消");
  selectButton_ = new QPushButton("选择当前目录");
  selectButton_->setObjectName("primaryButton");
  buttonRow->addWidget(cancelButton_);
  buttonRow->addWidget(selectButton_);
  layout->addLayout(buttonRow);
}

void RemoteDirectoryDialog::connectSignals() {
  QObject::connect(rootButton_, &QPushButton::clicked, this,
                   [this]() { loadDirectory("/", false); });
  QObject::connect(upButton_, &QPushButton::clicked, this, [this]() {
    loadDirectory(parentRemotePath(currentPath_), false);
  });
  QObject::connect(refreshButton_, &QPushButton::clicked, this,
                   [this]() { loadDirectory(currentPath_, true); });
  QObject::connect(cancelButton_, &QPushButton::clicked, this,
                   &QDialog::reject);
  QObject::connect(selectButton_, &QPushButton::clicked, this, [this]() {
    selectedPath_ = currentPath_;
    accept();
  });
  QObject::connect(directoryList_, &QListWidget::itemDoubleClicked, this,
                   [this](QListWidgetItem* item) {
                     if (!item) {
                       return;
                     }
                     const QString path =
                         item->data(Qt::UserRole).toString();
                     if (!path.isEmpty()) {
                       loadDirectory(path, false);
                     }
                   });
}

void RemoteDirectoryDialog::loadDirectory(const QString& path, bool refresh) {
  if (!client_ || !client_->hasConnection()) {
    showMessage("请先连接 OpenList。");
    return;
  }

  const QString requestedPath = normalizeRemotePath(path);
  currentPath_ = requestedPath;
  pathEdit_->setText(currentPath_);
  directoryList_->clear();
  directoryList_->addItem("正在加载目录...");
  setBusy(true);

  client_->listDirectory(
      requestedPath, refresh,
      [this](bool ok, const QString& message, const QString& loadedPath,
             const QVector<RemoteEntry>& entries) {
        setBusy(false);
        if (loadedPath != currentPath_) {
          return;
        }

        directoryList_->clear();
        if (!ok) {
          showMessage("目录加载失败: " + message);
          return;
        }

        int directoryCount = 0;
        for (const RemoteEntry& entry : entries) {
          if (!entry.isDir) {
            continue;
          }
          auto* item = new QListWidgetItem("📁  " + entry.name);
          item->setData(Qt::UserRole, entry.path);
          directoryList_->addItem(item);
          ++directoryCount;
        }

        if (directoryCount == 0) {
          directoryList_->addItem("当前目录没有子目录");
        }
        showMessage(QString("当前目录: %1，子目录: %2")
                        .arg(currentPath_)
                        .arg(directoryCount));
      });
}

void RemoteDirectoryDialog::setBusy(bool busy) {
  rootButton_->setEnabled(!busy);
  upButton_->setEnabled(!busy);
  refreshButton_->setEnabled(!busy);
  selectButton_->setEnabled(!busy);
}

void RemoteDirectoryDialog::showMessage(const QString& message) {
  statusLabel_->setText(message);
}
