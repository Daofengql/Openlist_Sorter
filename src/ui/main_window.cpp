#include "ui/main_window.h"

#include <functional>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayoutItem>
#include <QMediaContent>
#include <QMenuBar>
#include <QMessageBox>
#include <QResizeEvent>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

#include "preview/image_decoder.h"
#include "ui/connection_dialog.h"
#include "ui/connection_info_dialog.h"
#include "ui/remote_directory_dialog.h"

namespace {

QLabel* makeStatLabel(const QString& objectName) {
  auto* label = new QLabel();
  label->setObjectName(objectName);
  label->setAlignment(Qt::AlignCenter);
  label->setMinimumHeight(58);
  return label;
}

QString statText(const QString& label, int value) {
  return "<span class=\"stat-title\">" + label +
         "</span><br><span class=\"stat-number\">" + QString::number(value) +
         "</span>";
}

QString escapeHtml(const QString& text) {
  QString escaped = text;
  escaped.replace('&', "&amp;");
  escaped.replace('<', "&lt;");
  escaped.replace('>', "&gt;");
  escaped.replace('"', "&quot;");
  return escaped;
}

QString typeLabel(const RemoteEntry& entry) {
  if (entry.isDir) {
    return "目录";
  }
  const QString suffix = lowerSuffix(entry.name);
  if (suffix.isEmpty()) {
    return "未知文件";
  }
  return suffix.toUpper() + " 文件";
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  buildUi();
  connectSignals();
  applyModernStyle();
  loadSettings();
  refreshStats();
  updateModeControls();
}

bool MainWindow::ensureInitialConnection() {
  return showConnectionDialog(true);
}

void MainWindow::closeEvent(QCloseEvent* event) {
  saveSettings();
  QMainWindow::closeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  updateImagePreviewScale();
}

void MainWindow::buildUi() {
  setWindowTitle("OpenList Sorter");
  resize(1360, 820);

  auto* central = new QWidget();
  auto* rootLayout = new QVBoxLayout(central);
  rootLayout->setContentsMargins(16, 16, 16, 16);
  rootLayout->setSpacing(14);

  buildMenuBar();
  buildTaskPanel(rootLayout);
  rootLayout->addWidget(buildStatsPanel());

  auto* splitter = new QSplitter(Qt::Horizontal);
  splitter->setChildrenCollapsible(false);
  splitter->addWidget(buildFilePanel());
  splitter->addWidget(buildPreviewPanel());
  splitter->addWidget(buildTagPanel());
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setStretchFactor(2, 0);
  splitter->setSizes({280, 760, 320});
  rootLayout->addWidget(splitter, 1);

  setCentralWidget(central);

  operationStatusLabel_ = new QLabel("就绪");
  operationStatusLabel_->setObjectName("subtleLabel");
  endpointStatusLabel_ = new QLabel("Endpoint: 未连接");
  endpointStatusLabel_->setObjectName("subtleLabel");
  statusBar()->addWidget(operationStatusLabel_, 1);
  statusBar()->addPermanentWidget(endpointStatusLabel_);
}

void MainWindow::buildMenuBar() {
  QMenu* fileMenu = menuBar()->addMenu("文件(&F)");
  QAction* connectionInfoAction = fileMenu->addAction("连接信息...");
  QAction* reconnectAction = fileMenu->addAction("重新连接...");
  fileMenu->addSeparator();
  QAction* exitAction = fileMenu->addAction("退出");

  QMenu* editMenu = menuBar()->addMenu("编辑(&E)");
  QAction* clearCurrentAction = editMenu->addAction("清空当前文件选择");
  QAction* clearAllPendingAction = editMenu->addAction("清空全部未提交选择");

  QMenu* helpMenu = menuBar()->addMenu("帮助(&H)");
  QAction* aboutAction = helpMenu->addAction("关于 OpenList Sorter");

  QObject::connect(connectionInfoAction, &QAction::triggered, this,
                   &MainWindow::showConnectionInfoDialog);
  QObject::connect(reconnectAction, &QAction::triggered, this, [this]() {
    showConnectionDialog();
  });
  QObject::connect(exitAction, &QAction::triggered, this, &QWidget::close);
  QObject::connect(clearCurrentAction, &QAction::triggered, this,
                   &MainWindow::clearCurrentSelection);
  QObject::connect(clearAllPendingAction, &QAction::triggered, this,
                   &MainWindow::clearAllPendingSelections);
  QObject::connect(aboutAction, &QAction::triggered, this,
                   &MainWindow::showAboutDialog);
}

void MainWindow::buildTaskPanel(QVBoxLayout* rootLayout) {
  auto* panel = new QWidget();
  panel->setObjectName("surface");
  auto* layout = new QGridLayout(panel);
  layout->setContentsMargins(16, 14, 16, 14);
  layout->setHorizontalSpacing(10);
  layout->setVerticalSpacing(10);

  sourcePathEdit_ = new QLineEdit("/");
  sourcePathEdit_->setReadOnly(true);
  browseSourceButton_ = new QPushButton("选择源目录");
  enterModeButton_ = new QPushButton("进入分类模式");
  saveButton_ = new QPushButton("保存待提交");
  saveButton_->setObjectName("primaryButton");
  overwriteCheck_ = new QCheckBox("覆盖同名文件");

  auto* title = new QLabel("分类任务");
  title->setObjectName("sectionTitle");
  layout->addWidget(title, 0, 0);
  layout->addWidget(new QLabel("源目录"), 0, 1);
  layout->addWidget(sourcePathEdit_, 0, 2, 1, 3);
  layout->addWidget(browseSourceButton_, 0, 6);
  layout->addWidget(enterModeButton_, 0, 7);
  layout->addWidget(saveButton_, 0, 8);
  layout->addWidget(overwriteCheck_, 0, 9);

  rootLayout->addWidget(panel);
}

QWidget* MainWindow::buildStatsPanel() {
  auto* panel = new QWidget();
  auto* layout = new QHBoxLayout(panel);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  totalLabel_ = makeStatLabel("statCard");
  submittedLabel_ = makeStatLabel("statCard");
  pendingLabel_ = makeStatLabel("statCard");
  unprocessedLabel_ = makeStatLabel("statCard");
  currentIndexLabel_ = makeStatLabel("statCardAccent");

  layout->addWidget(totalLabel_);
  layout->addWidget(submittedLabel_);
  layout->addWidget(pendingLabel_);
  layout->addWidget(unprocessedLabel_);
  layout->addWidget(currentIndexLabel_);
  return panel;
}

QWidget* MainWindow::buildFilePanel() {
  auto* panel = new QWidget();
  panel->setObjectName("surface");
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(14, 14, 14, 14);
  layout->setSpacing(10);

  auto* title = new QLabel("当前目录文件");
  title->setObjectName("sectionTitle");
  fileList_ = new QListWidget();
  fileList_->setSelectionMode(QAbstractItemView::SingleSelection);
  fileList_->setAlternatingRowColors(true);

  layout->addWidget(title);
  layout->addWidget(fileList_, 1);
  return panel;
}

QWidget* MainWindow::buildPreviewPanel() {
  auto* panel = new QWidget();
  panel->setObjectName("surface");
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(16, 14, 16, 14);
  layout->setSpacing(10);

  previewTitleLabel_ = new QLabel("预览");
  previewTitleLabel_->setObjectName("sectionTitle");
  currentPathLabel_ = new QLabel("请选择源目录并进入分类模式");
  currentPathLabel_->setObjectName("pathLabel");
  currentPathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  currentPathLabel_->setWordWrap(true);

  previewStack_ = new QStackedWidget();
  previewStack_->setObjectName("previewArea");

  emptyPage_ = new QWidget();
  auto* emptyLayout = new QVBoxLayout(emptyPage_);
  emptyLabel_ = new QLabel("等待加载文件");
  emptyLabel_->setAlignment(Qt::AlignCenter);
  emptyLabel_->setObjectName("emptyLabel");
  emptyLayout->addWidget(emptyLabel_, 1);

  imagePage_ = new QWidget();
  auto* imageLayout = new QVBoxLayout(imagePage_);
  imageLayout->setContentsMargins(0, 0, 0, 0);
  imageLabel_ = new QLabel();
  imageLabel_->setAlignment(Qt::AlignCenter);
  imageLabel_->setMinimumSize(320, 240);
  imageLabel_->setObjectName("imagePreview");
  imageLayout->addWidget(imageLabel_, 1);

  videoPage_ = new QWidget();
  auto* videoLayout = new QVBoxLayout(videoPage_);
  videoLayout->setContentsMargins(0, 0, 0, 0);
  videoWidget_ = new QVideoWidget();
  videoWidget_->setMinimumSize(320, 240);
  playPauseButton_ = new QPushButton("播放");
  playPauseButton_->setObjectName("primaryButton");
  videoLayout->addWidget(videoWidget_, 1);
  videoLayout->addWidget(playPauseButton_, 0, Qt::AlignRight);
  mediaPlayer_ = new QMediaPlayer(this, QMediaPlayer::VideoSurface);
  mediaPlayer_->setVideoOutput(videoWidget_);

  infoPage_ = new QWidget();
  auto* infoLayout = new QVBoxLayout(infoPage_);
  infoLabel_ = new QLabel();
  infoLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  infoLabel_->setTextFormat(Qt::RichText);
  infoLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  infoLabel_->setWordWrap(true);
  infoLabel_->setObjectName("infoLabel");
  infoLayout->addWidget(infoLabel_, 1);

  previewStack_->addWidget(emptyPage_);
  previewStack_->addWidget(imagePage_);
  previewStack_->addWidget(videoPage_);
  previewStack_->addWidget(infoPage_);

  auto* navRow = new QHBoxLayout();
  prevButton_ = new QPushButton("上一个");
  nextButton_ = new QPushButton("下一个");
  navRow->addWidget(prevButton_);
  navRow->addStretch(1);
  navRow->addWidget(nextButton_);

  layout->addWidget(previewTitleLabel_);
  layout->addWidget(currentPathLabel_);
  layout->addWidget(previewStack_, 1);
  layout->addLayout(navRow);
  return panel;
}

QWidget* MainWindow::buildTagPanel() {
  auto* panel = new QWidget();
  panel->setObjectName("surface");
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(14, 14, 14, 14);
  layout->setSpacing(10);

  auto* title = new QLabel("分类 tag");
  title->setObjectName("sectionTitle");
  tagTable_ = new QTableWidget(0, 2);
  tagTable_->setHorizontalHeaderLabels({"Tag", "目标目录"});
  tagTable_->horizontalHeader()->setStretchLastSection(true);
  tagTable_->verticalHeader()->hide();
  tagTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tagTable_->setSelectionMode(QAbstractItemView::SingleSelection);
  tagTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tagTable_->setAlternatingRowColors(true);
  tagTable_->setMinimumHeight(180);

  auto* tagButtonRow = new QHBoxLayout();
  addTagButton_ = new QPushButton("新增");
  editTagButton_ = new QPushButton("编辑");
  removeTagButton_ = new QPushButton("删除");
  tagButtonRow->addWidget(addTagButton_);
  tagButtonRow->addWidget(editTagButton_);
  tagButtonRow->addWidget(removeTagButton_);

  auto* currentTitle = new QLabel("当前文件选择");
  currentTitle->setObjectName("sectionTitle");
  tagCheckContainer_ = new QWidget();
  tagCheckLayout_ = new QVBoxLayout(tagCheckContainer_);
  tagCheckLayout_->setContentsMargins(0, 0, 0, 0);
  tagCheckLayout_->setSpacing(6);
  tagScrollArea_ = new QScrollArea();
  tagScrollArea_->setWidgetResizable(true);
  tagScrollArea_->setWidget(tagCheckContainer_);
  tagScrollArea_->setMinimumHeight(220);

  multiTagNotice_ = new QLabel("多选 tag 时，保存会复制到多个目标目录。");
  multiTagNotice_->setObjectName("warningLabel");
  multiTagNotice_->setWordWrap(true);
  multiTagNotice_->hide();

  layout->addWidget(title);
  layout->addWidget(tagTable_);
  layout->addLayout(tagButtonRow);
  layout->addSpacing(8);
  layout->addWidget(currentTitle);
  layout->addWidget(tagScrollArea_, 1);
  layout->addWidget(multiTagNotice_);
  return panel;
}

void MainWindow::connectSignals() {
  QObject::connect(browseSourceButton_, &QPushButton::clicked, this,
                   &MainWindow::chooseSourceDirectory);
  QObject::connect(enterModeButton_, &QPushButton::clicked, this, [this]() {
    if (classificationMode_) {
      exitClassificationMode();
    } else {
      enterClassificationMode();
    }
  });
  QObject::connect(saveButton_, &QPushButton::clicked, this,
                   &MainWindow::savePending);
  QObject::connect(addTagButton_, &QPushButton::clicked, this,
                   &MainWindow::addTag);
  QObject::connect(editTagButton_, &QPushButton::clicked, this,
                   &MainWindow::editSelectedTag);
  QObject::connect(removeTagButton_, &QPushButton::clicked, this,
                   &MainWindow::removeSelectedTag);
  QObject::connect(prevButton_, &QPushButton::clicked, this, [this]() {
    setCurrentIndex(currentIndex_ - 1);
  });
  QObject::connect(nextButton_, &QPushButton::clicked, this, [this]() {
    setCurrentIndex(currentIndex_ + 1);
  });
  QObject::connect(fileList_, &QListWidget::currentRowChanged, this,
                   [this](int row) {
                     if (row >= 0 && row != currentIndex_) {
                       setCurrentIndex(row);
                     }
                   });
  QObject::connect(playPauseButton_, &QPushButton::clicked, this, [this]() {
    if (mediaPlayer_->state() == QMediaPlayer::PlayingState) {
      mediaPlayer_->pause();
    } else {
      mediaPlayer_->play();
    }
  });
  QObject::connect(mediaPlayer_, &QMediaPlayer::stateChanged, this,
                   [this](QMediaPlayer::State state) {
                     playPauseButton_->setText(
                         state == QMediaPlayer::PlayingState ? "暂停" : "播放");
                   });
}

void MainWindow::applyModernStyle() {
  qApp->setStyleSheet(R"(
    QMainWindow, QWidget {
      background: #eef2f7;
      color: #172033;
      font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
      font-size: 14px;
    }
    QWidget#surface {
      background: #ffffff;
      border: 1px solid #d9e1ee;
      border-radius: 8px;
    }
    QLabel#sectionTitle {
      color: #101828;
      font-size: 16px;
      font-weight: 700;
      background: transparent;
    }
    QLabel#dialogTitle {
      color: #101828;
      font-size: 22px;
      font-weight: 800;
      background: transparent;
    }
    QLabel#subtleLabel, QLabel#pathLabel {
      color: #667085;
      background: transparent;
    }
    QLabel#pathLabel {
      padding: 6px 0;
    }
    QLabel#emptyLabel {
      color: #98a2b3;
      font-size: 18px;
      background: transparent;
    }
    QLabel#infoLabel {
      background: #f8fafc;
      border: 1px solid #e4e7ec;
      border-radius: 8px;
      padding: 16px;
      line-height: 150%;
    }
    QLabel#warningLabel {
      color: #9a3412;
      background: #fff7ed;
      border: 1px solid #fed7aa;
      border-radius: 8px;
      padding: 10px;
    }
    QLabel#statCard, QLabel#statCardAccent {
      background: #ffffff;
      border: 1px solid #d9e1ee;
      border-radius: 8px;
      padding: 8px 12px;
    }
    QLabel#statCardAccent {
      border-color: #7c9cff;
      background: #f5f7ff;
    }
    QLabel .stat-title {
      color: #667085;
      font-size: 12px;
    }
    QLabel .stat-number {
      color: #101828;
      font-size: 22px;
      font-weight: 800;
    }
    QLineEdit {
      background: #ffffff;
      border: 1px solid #cbd5e1;
      border-radius: 7px;
      padding: 7px 9px;
      selection-background-color: #4f7cff;
    }
    QLineEdit:focus {
      border-color: #4f7cff;
    }
    QPushButton {
      background: #f8fafc;
      border: 1px solid #cbd5e1;
      border-radius: 7px;
      padding: 7px 12px;
    }
    QPushButton:hover {
      background: #eef4ff;
      border-color: #9bb7ff;
    }
    QPushButton:disabled {
      color: #98a2b3;
      background: #f2f4f7;
      border-color: #e4e7ec;
    }
    QPushButton#primaryButton {
      color: #ffffff;
      background: #375dfb;
      border-color: #375dfb;
      font-weight: 700;
    }
    QPushButton#primaryButton:hover {
      background: #2447d8;
      border-color: #2447d8;
    }
    QMenuBar {
      background: #ffffff;
      border-bottom: 1px solid #d9e1ee;
      padding: 2px;
    }
    QMenuBar::item {
      background: transparent;
      padding: 6px 10px;
      border-radius: 6px;
    }
    QMenuBar::item:selected {
      background: #eef4ff;
    }
    QMenu {
      background: #ffffff;
      border: 1px solid #d9e1ee;
      padding: 6px;
    }
    QMenu::item {
      padding: 7px 28px 7px 12px;
      border-radius: 6px;
    }
    QMenu::item:selected {
      background: #eef4ff;
      color: #172033;
    }
    QStatusBar {
      background: #ffffff;
      border-top: 1px solid #d9e1ee;
    }
    QListWidget, QTableWidget, QScrollArea, QStackedWidget#previewArea {
      background: #ffffff;
      border: 1px solid #e4e7ec;
      border-radius: 8px;
    }
    QListWidget::item {
      padding: 8px;
      border-bottom: 1px solid #f2f4f7;
    }
    QListWidget::item:selected {
      background: #e9efff;
      color: #172033;
    }
    QHeaderView::section {
      background: #f8fafc;
      border: 0;
      border-bottom: 1px solid #e4e7ec;
      padding: 8px;
      font-weight: 700;
    }
    QTableWidget::item {
      padding: 6px;
    }
    QCheckBox {
      background: transparent;
      spacing: 8px;
    }
    QSplitter::handle {
      background: #eef2f7;
    }
  )");
}

void MainWindow::loadSettings() {
  QSettings settings;
  sourcePath_ = normalizeRemotePath(settings.value("task/sourcePath", "/").toString());
  sourcePathEdit_->setText(sourcePath_);

  const int tagCount = settings.beginReadArray("tags");
  tags_.clear();
  for (int i = 0; i < tagCount; ++i) {
    settings.setArrayIndex(i);
    TagMapping tag;
    tag.name = settings.value("name").toString();
    tag.targetPath = normalizeRemotePath(settings.value("targetPath").toString());
    if (!tag.name.isEmpty() && !tag.targetPath.isEmpty()) {
      tags_.push_back(tag);
    }
  }
  settings.endArray();
  refreshTagTable();
  rebuildTagChecks();
}

void MainWindow::saveSettings() const {
  QSettings settings;
  settings.setValue("task/sourcePath", sourcePath_);

  settings.beginWriteArray("tags");
  for (int i = 0; i < tags_.size(); ++i) {
    settings.setArrayIndex(i);
    settings.setValue("name", tags_[i].name);
    settings.setValue("targetPath", tags_[i].targetPath);
  }
  settings.endArray();
}

bool MainWindow::showConnectionDialog(bool initialLaunch) {
  ConnectionDialog dialog(&client_, initialLaunch ? nullptr : this);
  if (dialog.exec() != QDialog::Accepted) {
    updateEndpointStatus();
    updateModeControls();
    return false;
  }

  files_.clear();
  currentIndex_ = -1;
  classificationMode_ = false;
  rebuildFileList();
  rebuildTagChecks();
  refreshStats();
  showInfoPage("等待开始", "请选择源目录并进入分类模式。");
  updateEndpointStatus();
  setOperationStatus("已连接: " + client_.baseUrl());
  updateModeControls();
  saveSettings();
  return true;
}

void MainWindow::showConnectionInfoDialog() {
  ConnectionInfoDialog dialog(client_.baseUrl(), client_.hasConnection(), this);
  if (dialog.exec() == QDialog::Accepted && dialog.reconnectRequested()) {
    showConnectionDialog();
  }
}

void MainWindow::showAboutDialog() {
  QMessageBox::about(
      this, "关于 OpenList Sorter",
      "OpenList Sorter\n\n"
      "一个用于 OpenList/Alist 的远程文件打标归档工具。\n\n"
      "当前版本侧重连续预览、tag 暂存、批量移动和复制。");
}

void MainWindow::clearCurrentSelection() {
  FileItem* file = currentFile();
  if (!file || file->submitted) {
    return;
  }
  file->selectedTags.clear();
  file->failed = false;
  file->lastError.clear();
  updateFileListRow(currentIndex_);
  rebuildTagChecks();
  refreshStats();
}

void MainWindow::clearAllPendingSelections() {
  for (int i = 0; i < files_.size(); ++i) {
    FileItem& file = files_[i];
    if (!file.submitted) {
      file.selectedTags.clear();
      file.failed = false;
      file.lastError.clear();
      updateFileListRow(i);
    }
  }
  rebuildTagChecks();
  refreshStats();
}

void MainWindow::setOperationStatus(const QString& message) {
  if (operationStatusLabel_) {
    operationStatusLabel_->setText(message);
  }
}

void MainWindow::updateEndpointStatus() {
  if (!endpointStatusLabel_) {
    return;
  }
  endpointStatusLabel_->setText(client_.hasConnection()
                                    ? "Endpoint: " + client_.baseUrl()
                                    : "Endpoint: 未连接");
}

void MainWindow::chooseSourceDirectory() {
  if (!client_.hasConnection()) {
    QMessageBox::warning(this, "未连接", "请先连接 OpenList。");
    return;
  }
  RemoteDirectoryDialog dialog(&client_, sourcePath_, this);
  if (dialog.exec() == QDialog::Accepted) {
    sourcePath_ = dialog.selectedPath();
    sourcePathEdit_->setText(sourcePath_);
    saveSettings();
  }
}

void MainWindow::enterClassificationMode() {
  if (!client_.hasConnection()) {
    QMessageBox::warning(this, "未连接", "请先连接 OpenList。");
    return;
  }
  if (sourcePath_.isEmpty()) {
    QMessageBox::warning(this, "未选择目录", "请先选择待分类源目录。");
    return;
  }
  classificationMode_ = true;
  updateModeControls();
  loadSourceDirectory();
}

void MainWindow::exitClassificationMode() {
  int pending = 0;
  for (const FileItem& file : files_) {
    if (!file.submitted && !file.selectedTags.isEmpty()) {
      ++pending;
    }
  }

  if (pending > 0 &&
      QMessageBox::question(
          this, "退出分类模式",
          QString("当前有 %1 个已选择但未保存的文件。退出会放弃这些本地选择，继续吗？")
              .arg(pending)) != QMessageBox::Yes) {
    return;
  }

  mediaPlayer_->stop();
  mediaPlayer_->setMedia(QMediaContent());
  classificationMode_ = false;
  files_.clear();
  currentIndex_ = -1;
  currentPixmap_ = QPixmap();
  rebuildFileList();
  rebuildTagChecks();
  refreshStats();
  showInfoPage("已退出分类模式", "可以重新选择源目录，然后再次进入分类模式。");
  setOperationStatus("已退出分类模式");
  updateModeControls();
}

void MainWindow::loadSourceDirectory() {
  files_.clear();
  currentIndex_ = -1;
  rebuildFileList();
  rebuildTagChecks();
  refreshStats();
  showInfoPage("正在加载", "正在读取源目录: " + escapeHtml(sourcePath_));

  client_.listDirectory(
      sourcePath_, false,
      [this](bool ok, const QString& message, const QString& loadedPath,
             const QVector<RemoteEntry>& entries) {
        if (loadedPath != sourcePath_) {
          return;
        }
        files_.clear();
        if (!ok) {
          classificationMode_ = false;
          updateModeControls();
          showInfoPage("加载失败", escapeHtml(message));
          refreshStats();
          return;
        }

        for (const RemoteEntry& entry : entries) {
          if (entry.isDir) {
            continue;
          }
          FileItem item;
          item.entry = entry;
          files_.push_back(item);
        }

        rebuildFileList();
        refreshStats();
        if (!files_.isEmpty()) {
          setCurrentIndex(0);
        } else {
          setOperationStatus("当前目录没有可分类文件，可以退出分类模式后重新选择目录。");
          updateModeControls();
          showInfoPage("没有文件",
                       "当前目录没有可分类文件。目录不会进入本次分类列表。");
        }
      });
}

void MainWindow::setCurrentIndex(int index) {
  if (files_.isEmpty()) {
    currentIndex_ = -1;
    refreshStats();
    rebuildTagChecks();
    return;
  }
  if (index < 0 || index >= files_.size()) {
    return;
  }
  currentIndex_ = index;
  if (fileList_->currentRow() != currentIndex_) {
    fileList_->setCurrentRow(currentIndex_);
  }
  renderCurrentFile();
  rebuildTagChecks();
  refreshStats();
}

void MainWindow::renderCurrentFile() {
  const FileItem* item = currentFile();
  currentPixmap_ = QPixmap();
  imageLabel_->clear();
  mediaPlayer_->stop();
  mediaPlayer_->setMedia(QMediaContent());

  if (!item) {
    previewTitleLabel_->setText("预览");
    currentPathLabel_->setText("没有当前文件");
    previewStack_->setCurrentWidget(emptyPage_);
    return;
  }

  previewTitleLabel_->setText(item->entry.name);
  currentPathLabel_->setText(item->entry.path);

  if (item->entry.rawUrl.isEmpty() &&
      (isImageFileName(item->entry.name) || isVideoFileName(item->entry.name))) {
    showInfoPage("正在准备预览", formatEntryInfo(*item));
    const QString expectedPath = item->entry.path;
    client_.getFileInfo(
        expectedPath,
        [this, expectedPath](bool ok, const QString& message,
                             const RemoteEntry& entry) {
          if (currentFilePath() != expectedPath) {
            return;
          }
          if (!ok || entry.rawUrl.isEmpty()) {
            showInfoPage("无法预览",
                         formatEntryInfo(*currentFile()) +
                             "<br><br><b>预览地址获取失败：</b>" +
                             escapeHtml(message));
            return;
          }
          if (currentIndex_ >= 0 && currentIndex_ < files_.size()) {
            files_[currentIndex_].entry.rawUrl = entry.rawUrl;
          }
          const FileItem* refreshed = currentFile();
          if (!refreshed) {
            return;
          }
          if (isImageFileName(refreshed->entry.name)) {
            renderImage(*refreshed, entry.rawUrl);
          } else if (isVideoFileName(refreshed->entry.name)) {
            renderVideo(*refreshed, entry.rawUrl);
          }
        });
    return;
  }

  if (isImageFileName(item->entry.name) && !item->entry.rawUrl.isEmpty()) {
    renderImage(*item, item->entry.rawUrl);
    return;
  }
  if (isVideoFileName(item->entry.name) && !item->entry.rawUrl.isEmpty()) {
    renderVideo(*item, item->entry.rawUrl);
    return;
  }
  showInfoPage("文件信息", formatEntryInfo(*item));
}

void MainWindow::renderImage(const FileItem& item, const QString& rawUrl) {
  showInfoPage("正在加载图片", formatEntryInfo(item));
  const QString expectedPath = item.entry.path;
  const QString fileName = item.entry.name;
  client_.downloadUrl(QUrl(rawUrl),
                      [this, expectedPath, fileName](
                          bool ok, const QString& message,
                          const QByteArray& data) {
                        if (currentFilePath() != expectedPath) {
                          return;
                        }
                        if (!ok) {
                          showInfoPage("图片预览失败",
                                       formatEntryInfo(*currentFile()) +
                                           "<br><br><b>错误：</b>" +
                                           escapeHtml(message));
                          return;
                        }
                        const ImageDecodeResult decoded =
                            ImageDecoder::decodeToPixmap(data, fileName);
                        if (!decoded.ok) {
                          showInfoPage(
                              "图片预览失败",
                              formatEntryInfo(*currentFile()) +
                                  "<br><br><b>错误：</b>" +
                                  escapeHtml(decoded.message) +
                                  "<br><br><b>当前 Qt 支持格式：</b>" +
                                  escapeHtml(
                                      ImageDecoder::supportedFormatsSummary()));
                          return;
                        }
                        currentPixmap_ = decoded.pixmap;
                        previewTitleLabel_->setText(fileName + " / " +
                                                    decoded.decoderName);
                        previewStack_->setCurrentWidget(imagePage_);
                        updateImagePreviewScale();
                      });
}

void MainWindow::renderVideo(const FileItem& item, const QString& rawUrl) {
  previewStack_->setCurrentWidget(videoPage_);
  playPauseButton_->setText("播放");
  mediaPlayer_->setMedia(QUrl(rawUrl));
  mediaPlayer_->pause();
  Q_UNUSED(item);
}

void MainWindow::showInfoPage(const QString& title, const QString& body) {
  previewTitleLabel_->setText(title);
  infoLabel_->setText(body);
  previewStack_->setCurrentWidget(infoPage_);
}

QString MainWindow::formatEntryInfo(const FileItem& item) const {
  QString modifiedText = item.entry.modified.isValid()
                             ? item.entry.modified.toLocalTime().toString("yyyy-MM-dd HH:mm:ss")
                             : "未知";
  QString createdText = item.entry.created.isValid()
                            ? item.entry.created.toLocalTime().toString("yyyy-MM-dd HH:mm:ss")
                            : "未知";

  QString tagsText = item.selectedTags.isEmpty()
                         ? "未选择"
                         : escapeHtml(item.selectedTags.join(", "));
  QString status = escapeHtml(statusTextForFile(item));
  if (item.failed && !item.lastError.isEmpty()) {
    status += " / " + escapeHtml(item.lastError);
  }

  return QString(
             "<b>文件名：</b>%1<br>"
             "<b>路径：</b>%2<br>"
             "<b>类型：</b>%3<br>"
             "<b>大小：</b>%4<br>"
             "<b>修改时间：</b>%5<br>"
             "<b>创建时间：</b>%6<br>"
             "<b>已选 tag：</b>%7<br>"
             "<b>状态：</b>%8")
      .arg(escapeHtml(item.entry.name))
      .arg(escapeHtml(item.entry.path))
      .arg(escapeHtml(typeLabel(item.entry)))
      .arg(escapeHtml(formatBytes(item.entry.size)))
      .arg(escapeHtml(modifiedText))
      .arg(escapeHtml(createdText))
      .arg(tagsText)
      .arg(status);
}

void MainWindow::updateImagePreviewScale() {
  if (currentPixmap_.isNull() || !imageLabel_) {
    return;
  }
  const QSize targetSize =
      imageLabel_->size().expandedTo(QSize(320, 240)) - QSize(20, 20);
  imageLabel_->setPixmap(currentPixmap_.scaled(
      targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::rebuildFileList() {
  fileList_->clear();
  for (int i = 0; i < files_.size(); ++i) {
    const FileItem& item = files_[i];
    auto* row = new QListWidgetItem();
    row->setData(Qt::UserRole, i);
    fileList_->addItem(row);
    updateFileListRow(i);
  }
}

void MainWindow::updateFileListRow(int index) {
  if (index < 0 || index >= files_.size() || index >= fileList_->count()) {
    return;
  }
  const FileItem& item = files_[index];
  const QString tags =
      item.selectedTags.isEmpty() ? "无 tag" : item.selectedTags.join(", ");
  QListWidgetItem* row = fileList_->item(index);
  row->setText(QString("%1  %2\n%3")
                   .arg(statusTextForFile(item), item.entry.name, tags));
}

void MainWindow::refreshStats() {
  int submitted = 0;
  int pending = 0;
  int unprocessed = 0;
  for (const FileItem& item : files_) {
    if (item.submitted) {
      ++submitted;
    } else if (!item.selectedTags.isEmpty()) {
      ++pending;
    } else {
      ++unprocessed;
    }
  }

  totalLabel_->setText(statText("总文件", files_.size()));
  submittedLabel_->setText(statText("已提交", submitted));
  pendingLabel_->setText(statText("待保存", pending));
  unprocessedLabel_->setText(statText("未处理", unprocessed));
  currentIndexLabel_->setText(
      "<span class=\"stat-title\">当前</span><br><span class=\"stat-number\">" +
      QString("%1 / %2")
          .arg(currentIndex_ >= 0 ? currentIndex_ + 1 : 0)
          .arg(files_.size()) +
      "</span>");

  prevButton_->setEnabled(currentIndex_ > 0);
  nextButton_->setEnabled(currentIndex_ >= 0 && currentIndex_ < files_.size() - 1);
  saveButton_->setEnabled(classificationMode_ && pending > 0 && !submitInProgress_);
}

void MainWindow::refreshTagTable() {
  tagTable_->setRowCount(tags_.size());
  for (int i = 0; i < tags_.size(); ++i) {
    tagTable_->setItem(i, 0, new QTableWidgetItem(tags_[i].name));
    tagTable_->setItem(i, 1, new QTableWidgetItem(tags_[i].targetPath));
  }
  tagTable_->resizeColumnsToContents();
}

void MainWindow::rebuildTagChecks() {
  suppressTagChange_ = true;
  tagCheckBoxes_.clear();
  while (QLayoutItem* child = tagCheckLayout_->takeAt(0)) {
    if (QWidget* widget = child->widget()) {
      widget->deleteLater();
    }
    delete child;
  }

  FileItem* item = currentFile();
  if (tags_.isEmpty()) {
    auto* label = new QLabel("请先新增 tag，并为 tag 选择目标目录。");
    label->setObjectName("subtleLabel");
    label->setWordWrap(true);
    tagCheckLayout_->addWidget(label);
  } else {
    for (const TagMapping& tag : tags_) {
      auto* check = new QCheckBox(tag.name + "\n" + tag.targetPath);
      check->setToolTip(tag.targetPath);
      check->setEnabled(item && !item->submitted && classificationMode_);
      if (item && item->selectedTags.contains(tag.name)) {
        check->setChecked(true);
      }
      QObject::connect(check, &QCheckBox::toggled, this,
                       &MainWindow::onTagSelectionChanged);
      tagCheckLayout_->addWidget(check);
      tagCheckBoxes_.push_back(check);
    }
  }
  tagCheckLayout_->addStretch(1);
  suppressTagChange_ = false;
  onTagSelectionChanged();
}

void MainWindow::onTagSelectionChanged() {
  if (suppressTagChange_) {
    return;
  }

  FileItem* item = currentFile();
  QStringList selected;
  for (QCheckBox* check : tagCheckBoxes_) {
    if (check && check->isChecked()) {
      selected.push_back(check->text().section('\n', 0, 0));
    }
  }

  if (item && !item->submitted) {
    item->selectedTags = selected;
    item->failed = false;
    item->lastError.clear();
    updateFileListRow(currentIndex_);
    refreshStats();
  }

  multiTagNotice_->setVisible(selected.size() > 1);
}

void MainWindow::updateModeControls() {
  const bool connected = client_.hasConnection();
  browseSourceButton_->setEnabled(connected && !classificationMode_ && !submitInProgress_);
  enterModeButton_->setEnabled(connected && !submitInProgress_);
  enterModeButton_->setText(classificationMode_ ? "退出分类模式" : "进入分类模式");
  sourcePathEdit_->setEnabled(!classificationMode_);
  addTagButton_->setEnabled(!submitInProgress_);
  editTagButton_->setEnabled(!submitInProgress_);
  removeTagButton_->setEnabled(!submitInProgress_);
  updateEndpointStatus();
  refreshStats();
}

void MainWindow::addTag() {
  TagMapping tag;
  if (!editTagDialog(&tag, "新增 tag")) {
    return;
  }
  for (const TagMapping& existing : tags_) {
    if (existing.name == tag.name) {
      QMessageBox::warning(this, "tag 已存在", "请使用不同的 tag 名称。");
      return;
    }
  }
  tags_.push_back(tag);
  refreshTagTable();
  rebuildTagChecks();
  saveSettings();
}

void MainWindow::editSelectedTag() {
  const int row = tagTable_->currentRow();
  if (row < 0 || row >= tags_.size()) {
    QMessageBox::information(this, "未选择 tag", "请先在 tag 表中选择一行。");
    return;
  }

  TagMapping edited = tags_[row];
  const QString oldName = edited.name;
  if (!editTagDialog(&edited, "编辑 tag")) {
    return;
  }
  for (int i = 0; i < tags_.size(); ++i) {
    if (i != row && tags_[i].name == edited.name) {
      QMessageBox::warning(this, "tag 已存在", "请使用不同的 tag 名称。");
      return;
    }
  }

  tags_[row] = edited;
  if (oldName != edited.name) {
    for (FileItem& file : files_) {
      for (QString& selectedTag : file.selectedTags) {
        if (selectedTag == oldName) {
          selectedTag = edited.name;
        }
      }
    }
  }
  refreshTagTable();
  rebuildTagChecks();
  rebuildFileList();
  refreshStats();
  saveSettings();
}

void MainWindow::removeSelectedTag() {
  const int row = tagTable_->currentRow();
  if (row < 0 || row >= tags_.size()) {
    QMessageBox::information(this, "未选择 tag", "请先在 tag 表中选择一行。");
    return;
  }
  const QString tagName = tags_[row].name;
  if (QMessageBox::question(this, "删除 tag",
                            "确定删除 tag “" + tagName +
                                "” 吗？当前会话中已选择该 tag 的文件也会取消选择。") !=
      QMessageBox::Yes) {
    return;
  }
  tags_.remove(row);
  for (FileItem& file : files_) {
    file.selectedTags.removeAll(tagName);
  }
  refreshTagTable();
  rebuildTagChecks();
  rebuildFileList();
  refreshStats();
  saveSettings();
}

bool MainWindow::editTagDialog(TagMapping* tag, const QString& title) {
  QDialog dialog(this);
  dialog.setWindowTitle(title);
  dialog.setMinimumWidth(520);

  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  auto* nameEdit = new QLineEdit(tag->name);
  auto* targetEdit = new QLineEdit(tag->targetPath.isEmpty() ? "/" : tag->targetPath);
  targetEdit->setReadOnly(true);
  auto* browseButton = new QPushButton("选择目录");

  auto* targetRow = new QHBoxLayout();
  targetRow->addWidget(targetEdit, 1);
  targetRow->addWidget(browseButton);

  form->addRow("Tag 名称", nameEdit);
  form->addRow("目标目录", targetRow);
  layout->addLayout(form);

  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);

  QObject::connect(browseButton, &QPushButton::clicked, &dialog, [&]() {
    if (!client_.hasConnection()) {
      QMessageBox::warning(&dialog, "未连接", "请先连接 OpenList。");
      return;
    }
    RemoteDirectoryDialog picker(&client_, targetEdit->text(), &dialog);
    if (picker.exec() == QDialog::Accepted) {
      targetEdit->setText(picker.selectedPath());
    }
  });
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                   &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }

  tag->name = nameEdit->text().trimmed();
  tag->targetPath = normalizeRemotePath(targetEdit->text());
  if (tag->name.isEmpty()) {
    QMessageBox::warning(this, "tag 名称为空", "请输入 tag 名称。");
    return false;
  }
  if (tag->targetPath.isEmpty()) {
    QMessageBox::warning(this, "目标目录为空", "请选择目标目录。");
    return false;
  }
  return true;
}

QString MainWindow::targetPathForTag(const QString& tagName) const {
  for (const TagMapping& tag : tags_) {
    if (tag.name == tagName) {
      return tag.targetPath;
    }
  }
  return {};
}

void MainWindow::savePending() {
  QVector<int> pendingIndexes;
  bool hasMultiTag = false;
  for (int i = 0; i < files_.size(); ++i) {
    if (!files_[i].submitted && !files_[i].selectedTags.isEmpty()) {
      pendingIndexes.push_back(i);
      hasMultiTag = hasMultiTag || files_[i].selectedTags.size() > 1;
    }
  }
  if (pendingIndexes.isEmpty()) {
    QMessageBox::information(this, "没有待保存项", "当前没有已选择但未提交的文件。");
    return;
  }
  if (hasMultiTag &&
      QMessageBox::question(this, "多 tag 复制提示",
                            "有文件选择了多个 tag，保存时会复制到多个目标目录，原文件会保留在源目录。继续吗？") !=
          QMessageBox::Yes) {
    return;
  }

  submitInProgress_ = true;
  updateModeControls();
  setOperationStatus(QString("正在提交 %1 个文件...").arg(pendingIndexes.size()));
  submitNextFile(pendingIndexes, 0);
}

void MainWindow::submitNextFile(const QVector<int>& pendingIndexes,
                                int position) {
  if (position >= pendingIndexes.size()) {
    submitInProgress_ = false;
    updateModeControls();
    setOperationStatus("保存完成");
    QMessageBox::information(this, "保存完成", "待提交操作已经处理完成。");
    return;
  }

  const int fileIndex = pendingIndexes[position];
  if (fileIndex < 0 || fileIndex >= files_.size()) {
    submitNextFile(pendingIndexes, position + 1);
    return;
  }

  setOperationStatus(
      QString("正在提交 %1 / %2: %3")
          .arg(position + 1)
          .arg(pendingIndexes.size())
          .arg(files_[fileIndex].entry.name));

  submitFile(fileIndex, [this, pendingIndexes, position, fileIndex](
                            bool ok, const QString& message) {
    if (fileIndex >= 0 && fileIndex < files_.size()) {
      FileItem& file = files_[fileIndex];
      if (ok) {
        file.submitted = true;
        file.failed = false;
        file.lastError.clear();
      } else {
        file.submitted = false;
        file.failed = true;
        file.lastError = message;
      }
      updateFileListRow(fileIndex);
      if (fileIndex == currentIndex_) {
        rebuildTagChecks();
        renderCurrentFile();
      }
      refreshStats();
    }
    submitNextFile(pendingIndexes, position + 1);
  });
}

void MainWindow::submitFile(int index,
                            OpenListClient::SimpleCallback callback) {
  if (index < 0 || index >= files_.size()) {
    callback(false, "内部索引错误");
    return;
  }

  FileItem& file = files_[index];
  if (file.selectedTags.isEmpty()) {
    callback(true, "跳过未选择 tag 的文件");
    return;
  }

  for (const QString& tagName : file.selectedTags) {
    if (targetPathForTag(tagName).isEmpty()) {
      callback(false, "tag 没有绑定目标目录: " + tagName);
      return;
    }
  }

  if (file.selectedTags.size() == 1) {
    client_.moveFile(sourcePath_, file.entry.name,
                     targetPathForTag(file.selectedTags.first()),
                     overwriteCheck_->isChecked(), callback);
    return;
  }

  submitCopyTargets(index, file.selectedTags, 0, callback);
}

void MainWindow::submitCopyTargets(int fileIndex,
                                   const QStringList& selectedTags,
                                   int tagPosition,
                                   OpenListClient::SimpleCallback callback) {
  if (fileIndex < 0 || fileIndex >= files_.size()) {
    callback(false, "内部索引错误");
    return;
  }
  if (tagPosition >= selectedTags.size()) {
    callback(true, "复制完成");
    return;
  }

  const QString targetPath = targetPathForTag(selectedTags[tagPosition]);
  if (targetPath.isEmpty()) {
    callback(false, "tag 没有绑定目标目录: " + selectedTags[tagPosition]);
    return;
  }

  client_.copyFile(sourcePath_, files_[fileIndex].entry.name, targetPath,
                   overwriteCheck_->isChecked(),
                   [this, fileIndex, selectedTags, tagPosition, callback](
                       bool ok, const QString& message) {
                     if (!ok) {
                       callback(false, message);
                       return;
                     }
                     submitCopyTargets(fileIndex, selectedTags, tagPosition + 1,
                                       callback);
                   });
}

QString MainWindow::currentFilePath() const {
  const FileItem* item = currentFile();
  return item ? item->entry.path : QString();
}

FileItem* MainWindow::currentFile() {
  if (currentIndex_ < 0 || currentIndex_ >= files_.size()) {
    return nullptr;
  }
  return &files_[currentIndex_];
}

const FileItem* MainWindow::currentFile() const {
  if (currentIndex_ < 0 || currentIndex_ >= files_.size()) {
    return nullptr;
  }
  return &files_[currentIndex_];
}
