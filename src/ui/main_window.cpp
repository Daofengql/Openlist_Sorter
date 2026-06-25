#include "ui/main_window.h"

#include <functional>

#include <QAbstractScrollArea>
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QFutureWatcher>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayoutItem>
#include <QMediaContent>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QResizeEvent>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QStandardPaths>
#include <QStyle>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

#include "core/connection_profile.h"
#include "preview/image_converter.h"
#include "preview/image_decoder.h"
#include "preview/preview_cache.h"
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

struct ImageCacheLoadResult {
  bool ok{};
  QImage image;
};

struct ThumbnailLoadResult {
  bool ok{};
  bool fromCache{};
  QImage image;
  QString message;
};

ImageCacheLoadResult loadCachedImageTask(const RemoteEntry& entry) {
  ImageCacheLoadResult result;
  result.ok = PreviewCache::loadImage(entry, &result.image);
  return result;
}

ThumbnailLoadResult loadCachedThumbnailTask(const RemoteEntry& entry) {
  ThumbnailLoadResult result;
  result.fromCache = true;
  result.ok = PreviewCache::loadThumbnail(entry, &result.image);
  return result;
}

ImageDecodeResult decodeImageTask(const QByteArray& data,
                                  const QString& fileName,
                                  const RemoteEntry& cacheEntry) {
  ImageDecodeResult result = ImageDecoder::decodeToImage(data, fileName);
  if (result.ok && ImageDecoder::isHeifLikeData(data, fileName)) {
    PreviewCache::storeImage(cacheEntry, result.image);
  }
  return result;
}

ThumbnailLoadResult decodeThumbnailTask(const QByteArray& data,
                                        const QString& fileName,
                                        const RemoteEntry& cacheEntry) {
  ThumbnailLoadResult result;
  result.ok = false;
  ImageDecodeResult decoded = ImageDecoder::decodeToImage(data, fileName);
  if (!decoded.ok) {
    result.message = decoded.message;
    return result;
  }
  result.ok = true;
  result.image = decoded.image;
  PreviewCache::storeThumbnail(cacheEntry, result.image);
  return result;
}

ImageConversionResult convertImageToWebpTask(const QByteArray& data,
                                             const QString& fileName,
                                             const RemoteEntry& cacheEntry) {
  if (ImageDecoder::isHeifLikeData(data, fileName)) {
    QByteArray decodedCacheData;
    if (!PreviewCache::loadImageData(cacheEntry, &decodedCacheData)) {
      ImageDecodeResult decoded = ImageDecoder::decodeToImage(data, fileName);
      if (!decoded.ok) {
        ImageConversionResult result;
        result.hashSource = "HEIF 解码 JPG 缓存";
        result.message = "HEIF/AVIF 解码缓存生成失败: " + decoded.message;
        return result;
      }
      if (!PreviewCache::storeImage(cacheEntry, decoded.image) ||
          !PreviewCache::loadImageData(cacheEntry, &decodedCacheData)) {
        ImageConversionResult result;
        result.hashSource = "HEIF 解码 JPG 缓存";
        result.message = "HEIF/AVIF 解码缓存生成失败: 无法写入满质量 JPG 缓存";
        return result;
      }
    }

    return ImageConverter::convertToWebp(
        decodedCacheData, QFileInfo(fileName).completeBaseName() + ".jpg",
        "HEIF 解码 JPG 缓存");
  }

  ImageConversionResult rawResult =
      ImageConverter::convertToWebp(data, fileName, "原始文件");
  return rawResult;
}

QUrl resolvedOpenListUrl(const QString& baseUrl, const QString& urlText) {
  const QString trimmed = urlText.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }

  QUrl url(trimmed);
  if (url.isValid() && !url.isRelative()) {
    return url;
  }

  QUrl base(baseUrl);
  if (!base.path().endsWith('/')) {
    base.setPath(base.path() + "/");
  }
  return base.resolved(QUrl(trimmed));
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  buildUi();
  connectSignals();
  applyModernStyle();
  loadSettings();
  clearPreviewCacheSilently("启动清理");
  refreshStats();
  updateModeControls();
}

bool MainWindow::ensureInitialConnection() {
  return showConnectionDialog(true);
}

void MainWindow::closeEvent(QCloseEvent* event) {
  saveSettings();
  clearPreviewCacheSilently("退出清理");
  QMainWindow::closeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
}

void MainWindow::buildUi() {
  setWindowTitle("OpenList Sorter");
  resize(1440, 860);

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
  operationProgressBar_ = new QProgressBar();
  operationProgressBar_->setObjectName("statusProgress");
  operationProgressBar_->setRange(0, 100);
  operationProgressBar_->setFixedWidth(180);
  operationProgressBar_->setFixedHeight(10);
  operationProgressBar_->setTextVisible(false);
  operationProgressBar_->hide();
  endpointStatusLabel_ = new QLabel("Endpoint: 未连接");
  endpointStatusLabel_->setObjectName("subtleLabel");
  statusBar()->addWidget(operationStatusLabel_, 1);
  statusBar()->addPermanentWidget(operationProgressBar_);
  statusBar()->addPermanentWidget(endpointStatusLabel_);
}

void MainWindow::buildMenuBar() {
  QMenu* fileMenu = menuBar()->addMenu("文件(&F)");
  QAction* settingsAction = fileMenu->addAction("设置...");
  QAction* connectionInfoAction = fileMenu->addAction("连接信息...");
  fileMenu->addSeparator();
  QAction* logoutAction = fileMenu->addAction("注销");
  fileMenu->addSeparator();
  QAction* exitAction = fileMenu->addAction("退出");

  QMenu* editMenu = menuBar()->addMenu("编辑(&E)");
  QAction* clearCurrentAction = editMenu->addAction("清空当前文件选择");
  QAction* clearAllPendingAction = editMenu->addAction("清空全部未提交选择");
  editMenu->addSeparator();
  QAction* clearPreviewCacheAction = editMenu->addAction("清理预览缓存...");

  QMenu* helpMenu = menuBar()->addMenu("帮助(&H)");
  QAction* githubAction = helpMenu->addAction("GitHub 仓库");
  QAction* blogAction = helpMenu->addAction("作者博客");
  helpMenu->addSeparator();
  QAction* aboutAction = helpMenu->addAction("关于 OpenList Sorter");

  QObject::connect(settingsAction, &QAction::triggered, this,
                   &MainWindow::showSettingsDialog);
  QObject::connect(connectionInfoAction, &QAction::triggered, this,
                   &MainWindow::showConnectionInfoDialog);
  QObject::connect(logoutAction, &QAction::triggered, this, &MainWindow::logout);
  QObject::connect(exitAction, &QAction::triggered, this, &QWidget::close);
  QObject::connect(clearCurrentAction, &QAction::triggered, this,
                   &MainWindow::clearCurrentSelection);
  QObject::connect(clearAllPendingAction, &QAction::triggered, this,
                   &MainWindow::clearAllPendingSelections);
  QObject::connect(clearPreviewCacheAction, &QAction::triggered, this,
                   &MainWindow::clearPreviewCache);
  QObject::connect(githubAction, &QAction::triggered, this,
                   &MainWindow::openProjectHomepage);
  QObject::connect(blogAction, &QAction::triggered, this,
                   &MainWindow::openAuthorBlog);
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
  downloadPathEdit_ = new QLineEdit(panel);
  downloadPathEdit_->setReadOnly(true);
  downloadPathEdit_->setPlaceholderText("默认下载目录");
  downloadPathEdit_->hide();
  browseDownloadPathButton_ = new QPushButton("下载目录", panel);
  browseDownloadPathButton_->hide();
  downloadCurrentButton_ = new QPushButton("下载当前");
  convertCurrentButton_ = new QPushButton("仅转换当前");
  deleteCurrentButton_ = new QPushButton("删除当前");
  enterModeButton_ = new QPushButton("进入分类模式");
  overwriteCheck_ = new QCheckBox("覆盖同名文件", panel);
  overwriteCheck_->hide();
  convertImageMoveCheck_ = new QCheckBox("保存时转 WebP", panel);
  convertImageMoveCheck_->hide();
  convertImageMoveCheck_->setToolTip(
      "图片保存时先转成 WebP 上传到目标目录，成功后删除 OpenList 源文件。");

  auto* title = new QLabel("分类任务");
  title->setObjectName("sectionTitle");
  layout->addWidget(title, 0, 0);
  layout->addWidget(browseSourceButton_, 0, 1);
  layout->addWidget(sourcePathEdit_, 0, 2, 1, 4);
  layout->addWidget(enterModeButton_, 0, 6);
  layout->addWidget(convertCurrentButton_, 0, 7);
  layout->setColumnStretch(2, 1);
  layout->setColumnStretch(3, 1);
  layout->setColumnStretch(4, 1);
  layout->setColumnStretch(5, 1);

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
  panel->setMinimumWidth(260);
  panel->setMaximumWidth(360);
  panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(14, 14, 14, 14);
  layout->setSpacing(10);

  auto* title = new QLabel("当前目录文件");
  title->setObjectName("sectionTitle");
  auto* pageRow = new QHBoxLayout();
  pageRow->setContentsMargins(0, 0, 0, 0);
  pageRow->setSpacing(8);
  prevPageButton_ = new QPushButton();
  nextPageButton_ = new QPushButton();
  prevPageButton_->setIcon(style()->standardIcon(QStyle::SP_ArrowLeft));
  nextPageButton_->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
  prevPageButton_->setToolTip("上一页");
  nextPageButton_->setToolTip("下一页");
  prevPageButton_->setFixedSize(34, 30);
  nextPageButton_->setFixedSize(34, 30);
  prevPageButton_->setIconSize(QSize(16, 16));
  nextPageButton_->setIconSize(QSize(16, 16));
  pageInfoLabel_ = new QLabel("第 0 / 0 页");
  pageInfoLabel_->setObjectName("subtleLabel");
  pageInfoLabel_->setAlignment(Qt::AlignCenter);
  pageRow->addWidget(prevPageButton_);
  pageRow->addWidget(pageInfoLabel_, 1);
  pageRow->addWidget(nextPageButton_);

  fileList_ = new QListWidget();
  fileList_->setSelectionMode(QAbstractItemView::SingleSelection);
  fileList_->setAlternatingRowColors(true);
  fileList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  fileList_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
  fileList_->setTextElideMode(Qt::ElideRight);
  fileList_->setWordWrap(false);

  layout->addWidget(title);
  layout->addLayout(pageRow);
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
  previewTitleLabel_->setMinimumWidth(0);
  previewTitleLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  currentPathLabel_ = new QLabel("请选择源目录并进入分类模式");
  currentPathLabel_->setObjectName("pathLabel");
  currentPathLabel_->setMinimumWidth(0);
  currentPathLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
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
  imageLabel_ = new ZoomableImageLabel();
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
  downloadCurrentButton_->setText("");
  downloadCurrentButton_->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  downloadCurrentButton_->setToolTip("下载当前文件");
  downloadCurrentButton_->setFixedSize(38, 34);
  downloadCurrentButton_->setIconSize(QSize(18, 18));
  deleteCurrentButton_->setText("");
  deleteCurrentButton_->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
  deleteCurrentButton_->setToolTip("删除当前远程文件");
  deleteCurrentButton_->setObjectName("destructiveButton");
  deleteCurrentButton_->setFixedSize(38, 34);
  deleteCurrentButton_->setIconSize(QSize(18, 18));
  navRow->addWidget(prevButton_);
  navRow->addStretch(1);
  navRow->addWidget(downloadCurrentButton_);
  navRow->addWidget(deleteCurrentButton_);
  navRow->addSpacing(10);
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
  panel->setMinimumWidth(300);
  panel->setMaximumWidth(390);
  panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(14, 14, 14, 14);
  layout->setSpacing(10);

  auto* title = new QLabel("分类 tag");
  title->setObjectName("sectionTitle");
  tagTable_ = new QTableWidget(0, 2);
  tagTable_->setHorizontalHeaderLabels({"Tag", "目标目录"});
  tagTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  tagTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  tagTable_->verticalHeader()->hide();
  tagTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tagTable_->setSelectionMode(QAbstractItemView::SingleSelection);
  tagTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tagTable_->setAlternatingRowColors(true);
  tagTable_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  tagTable_->setTextElideMode(Qt::ElideRight);
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
  tagScrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  tagScrollArea_->setWidget(tagCheckContainer_);
  tagScrollArea_->setMinimumHeight(220);

  multiTagNotice_ = new QLabel("多选 tag 时，保存会复制到多个目标目录。");
  multiTagNotice_->setObjectName("warningLabel");
  multiTagNotice_->setWordWrap(true);
  multiTagNotice_->hide();
  saveButton_ = new QPushButton("保存待提交");
  saveButton_->setObjectName("primaryButton");
  saveButton_->setMinimumHeight(36);

  layout->addWidget(title);
  layout->addWidget(tagTable_);
  layout->addLayout(tagButtonRow);
  layout->addSpacing(8);
  layout->addWidget(currentTitle);
  layout->addWidget(tagScrollArea_, 1);
  layout->addWidget(multiTagNotice_);
  layout->addWidget(saveButton_);
  return panel;
}

void MainWindow::connectSignals() {
  QObject::connect(browseSourceButton_, &QPushButton::clicked, this,
                   &MainWindow::chooseSourceDirectory);
  QObject::connect(browseDownloadPathButton_, &QPushButton::clicked, this,
                   &MainWindow::chooseDownloadDirectory);
  QObject::connect(downloadCurrentButton_, &QPushButton::clicked, this,
                   &MainWindow::downloadCurrentFile);
  QObject::connect(convertCurrentButton_, &QPushButton::clicked, this,
                   &MainWindow::convertCurrentFileOnly);
  QObject::connect(deleteCurrentButton_, &QPushButton::clicked, this,
                   &MainWindow::deleteCurrentFile);
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
  QObject::connect(prevPageButton_, &QPushButton::clicked, this, [this]() {
    setCurrentPage(currentPage_ - 1);
  });
  QObject::connect(nextPageButton_, &QPushButton::clicked, this, [this]() {
    setCurrentPage(currentPage_ + 1);
  });
  QObject::connect(fileList_, &QListWidget::currentRowChanged, this,
                   [this](int row) {
                     QListWidgetItem* item = fileList_->item(row);
                     if (!item) {
                       return;
                     }
                     bool ok = false;
                     const int index = item->data(Qt::UserRole).toInt(&ok);
                     if (ok && index >= 0 && index != currentIndex_) {
                       setCurrentIndex(index);
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
    QLabel {
      background: transparent;
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
      background: #f8fafc;
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
    QPushButton#destructiveButton {
      color: #ffffff;
      background: #dc2626;
      border-color: #dc2626;
      font-weight: 700;
    }
    QPushButton#destructiveButton:hover {
      background: #b91c1c;
      border-color: #b91c1c;
    }
    QPushButton#destructiveButton:disabled {
      color: #fee2e2;
      background: #fecaca;
      border-color: #fecaca;
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
    QProgressBar#statusProgress {
      background: #eef2f7;
      border: 0;
      border-radius: 5px;
    }
    QProgressBar#statusProgress::chunk {
      background: #375dfb;
      border-radius: 5px;
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
    QScrollBar:vertical {
      background: #f8fafc;
      width: 10px;
      margin: 2px;
      border: 0;
      border-radius: 5px;
    }
    QScrollBar:horizontal {
      background: #f8fafc;
      height: 10px;
      margin: 2px;
      border: 0;
      border-radius: 5px;
    }
    QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
      background: #cbd5e1;
      border-radius: 5px;
      min-height: 32px;
      min-width: 32px;
    }
    QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover {
      background: #94a3b8;
    }
    QScrollBar::add-line, QScrollBar::sub-line,
    QScrollBar::add-page, QScrollBar::sub-page {
      background: transparent;
      border: 0;
      width: 0;
      height: 0;
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
  const QString defaultDownloadPath =
      QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  downloadPathEdit_->setText(
      settings.value("task/downloadPath", defaultDownloadPath).toString());
  overwriteCheck_->setChecked(settings.value("task/overwrite", false).toBool());
  convertImageMoveCheck_->setChecked(
      settings.value("task/convertImageMove", false).toBool());
  preloadConcurrency_ =
      qBound(1, settings.value("task/preloadConcurrency", 2).toInt(), 10);

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
  settings.setValue("task/downloadPath", downloadPathEdit_->text());
  settings.setValue("task/overwrite", overwriteCheck_->isChecked());
  settings.setValue("task/convertImageMove",
                    convertImageMoveCheck_->isChecked());
  settings.setValue("task/preloadConcurrency", preloadConcurrency_);

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
  currentPage_ = 0;
  classificationMode_ = false;
  rebuildFileList();
  rebuildTagChecks();
  refreshStats();
  showInfoPage("等待开始", "请选择源目录并进入分类模式。");
  updateEndpointStatus();
  setOperationStatus("已连接: " + client_.baseUrl());
  refreshConnectionDiagnostics();
  updateModeControls();
  saveSettings();
  return true;
}

void MainWindow::showConnectionInfoDialog() {
  ConnectionInfoDialog dialog(client_.baseUrl(), client_.hasConnection(), this);
  if (client_.hasConnection()) {
    QPointer<ConnectionInfoDialog> dialogPtr(&dialog);
    client_.fetchConnectionDiagnostics(
        [dialogPtr](const ConnectionDiagnostics& diagnostics) {
          if (dialogPtr) {
            dialogPtr->setDiagnostics(diagnostics);
          }
        });
  }
  dialog.exec();
}

void MainWindow::showSettingsDialog() {
  QDialog dialog(this);
  dialog.setWindowTitle("设置");
  dialog.setMinimumWidth(560);

  auto* root = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();

  auto* downloadEdit = new QLineEdit(downloadPathEdit_->text());
  downloadEdit->setReadOnly(true);
  auto* browseButton = new QPushButton("选择目录");
  auto* downloadRow = new QHBoxLayout();
  downloadRow->addWidget(downloadEdit, 1);
  downloadRow->addWidget(browseButton);
  form->addRow("默认下载目录", downloadRow);

  auto* overwriteCheck = new QCheckBox("覆盖同名文件");
  overwriteCheck->setChecked(overwriteCheck_->isChecked());
  auto* convertCheck = new QCheckBox("保存时转 WebP");
  convertCheck->setChecked(convertImageMoveCheck_->isChecked());
  convertCheck->setToolTip(
      "图片保存时先转成 WebP 上传到目标目录，成功后删除 OpenList 源文件。");
  auto* preloadConcurrencySpin = new QSpinBox();
  preloadConcurrencySpin->setRange(1, 10);
  preloadConcurrencySpin->setValue(preloadConcurrency_);
  preloadConcurrencySpin->setSuffix(" 个");
  preloadConcurrencySpin->setToolTip(
      "切换分页时，同时下载和处理当前页图片缓存的任务数量。");
  form->addRow("保存行为", overwriteCheck);
  form->addRow("图片处理", convertCheck);
  form->addRow("分页预热并发", preloadConcurrencySpin);
  root->addLayout(form);

  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  root->addWidget(buttons);

  QObject::connect(browseButton, &QPushButton::clicked, &dialog, [&]() {
    const QString startPath = downloadEdit->text().isEmpty()
                                  ? QStandardPaths::writableLocation(
                                        QStandardPaths::DownloadLocation)
                                  : downloadEdit->text();
    const QString directory = QFileDialog::getExistingDirectory(
        &dialog, "选择默认下载目录", startPath);
    if (!directory.isEmpty()) {
      downloadEdit->setText(QDir::toNativeSeparators(directory));
    }
  });
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                   &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  downloadPathEdit_->setText(downloadEdit->text());
  overwriteCheck_->setChecked(overwriteCheck->isChecked());
  convertImageMoveCheck_->setChecked(convertCheck->isChecked());
  const int oldPreloadConcurrency = preloadConcurrency_;
  preloadConcurrency_ = preloadConcurrencySpin->value();
  saveSettings();
  updateModeControls();
  if (classificationMode_ && oldPreloadConcurrency != preloadConcurrency_ &&
      !submitInProgress_ && !downloadInProgress_ && !deleteInProgress_) {
    preloadCurrentPage();
  }
}

void MainWindow::showAboutDialog() {
  QMessageBox::about(
      this, "关于 OpenList Sorter",
      "OpenList Sorter\n\n"
      "一个用于 OpenList/Alist 的远程文件打标归档工具。\n\n"
      "当前版本侧重连续预览、tag 暂存、批量移动和复制。");
}

void MainWindow::openProjectHomepage() {
  QDesktopServices::openUrl(
      QUrl("https://github.com/Daofengql/Openlist_Sorter"));
}

void MainWindow::openAuthorBlog() {
  QDesktopServices::openUrl(QUrl("https://www.silverdragon.cn"));
}

void MainWindow::logout() {
  if (submitInProgress_ || downloadInProgress_ || deleteInProgress_) {
    QMessageBox::information(
        this, "暂时无法注销",
        "当前仍有提交、下载或删除任务正在进行。请等待任务完成后再注销。");
    return;
  }

  int pending = 0;
  for (const FileItem& file : files_) {
    if (!file.submitted && !file.selectedTags.isEmpty()) {
      ++pending;
    }
  }

  QString detail =
      "注销后将断开与 OpenList 的连接，关闭当前主窗口，并关闭下次启动时的自动登录。"
      "\n\n已保存的连接地址和凭据会保留，之后可以手动重新连接。";
  if (pending > 0) {
    detail += QString("\n\n当前有 %1 个已选择但未保存的分类项，注销后这些本地选择不会保留。")
                  .arg(pending);
  }

  QMessageBox confirm(this);
  confirm.setIcon(QMessageBox::Question);
  confirm.setWindowTitle("确认注销");
  confirm.setText("确认注销当前会话？");
  confirm.setInformativeText(detail);
  confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  confirm.setDefaultButton(QMessageBox::No);
  if (auto* yesButton = confirm.button(QMessageBox::Yes)) {
    yesButton->setText("注销");
  }
  if (auto* noButton = confirm.button(QMessageBox::No)) {
    noButton->setText("取消");
  }

  if (confirm.exec() != QMessageBox::Yes) {
    return;
  }

  ConnectionSettingsStore::disableAutoLogin();
  close();
}

void MainWindow::refreshConnectionDiagnostics() {
  if (!client_.hasConnection()) {
    connectionLatencyText_.clear();
    connectionIpText_.clear();
    connectionVersionText_.clear();
    updateEndpointStatus();
    return;
  }

  connectionLatencyText_ = "检测中";
  connectionIpText_ = "检测中";
  connectionVersionText_ = "检测中";
  updateEndpointStatus();

  QPointer<MainWindow> window(this);
  client_.fetchConnectionDiagnostics(
      [window](const ConnectionDiagnostics& diagnostics) {
        if (!window) {
          return;
        }
        window->connectionLatencyText_ =
            diagnostics.latencyMs >= 0
                ? QString::number(diagnostics.latencyMs) + " ms"
                : "未知";
        window->connectionIpText_ =
            diagnostics.ipAddress.isEmpty() ? "未知" : diagnostics.ipAddress;
        window->connectionVersionText_ =
            diagnostics.version.isEmpty() ? "未知" : diagnostics.version;
        window->updateEndpointStatus();
      });
}

void MainWindow::clearPreviewCache() {
  const PreviewCacheStats cacheStats = PreviewCache::stats();
  const QString cachePath =
      QDir::toNativeSeparators(PreviewCache::cacheDirectoryPath());

  if (cacheStats.fileCount == 0) {
    setOperationStatus("预览缓存为空");
    QMessageBox::information(
        this, "预览缓存为空",
        "当前没有可清理的预览缓存。\n\n缓存位置:\n" + cachePath);
    return;
  }

  QMessageBox confirm(this);
  confirm.setIcon(QMessageBox::Question);
  confirm.setWindowTitle("清理预览缓存");
  confirm.setText("确定要清理预览缓存吗？");
  confirm.setInformativeText(
      QString("将删除 %1 个缓存文件，占用 %2。\n"
              "清理后，图片会在下次预览时重新生成缓存。")
          .arg(cacheStats.fileCount)
          .arg(formatBytes(cacheStats.totalBytes)));
  confirm.setDetailedText("缓存位置:\n" + cachePath);
  confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  confirm.setDefaultButton(QMessageBox::No);
  if (auto* yesButton = confirm.button(QMessageBox::Yes)) {
    yesButton->setText("清理");
  }
  if (auto* noButton = confirm.button(QMessageBox::No)) {
    noButton->setText("取消");
  }

  if (confirm.exec() != QMessageBox::Yes) {
    return;
  }

  QString errorMessage;
  if (!PreviewCache::clear(&errorMessage)) {
    setOperationStatus("清理预览缓存失败");
    QMessageBox::warning(this, "清理失败", errorMessage);
    return;
  }

  const QString clearedText =
      QString("已清理预览缓存: %1 个文件，%2")
          .arg(cacheStats.fileCount)
          .arg(formatBytes(cacheStats.totalBytes));
  setOperationStatus(clearedText);
  setProgressVisible(false);
  QMessageBox::information(this, "清理完成", clearedText);
}

void MainWindow::clearPreviewCacheSilently(const QString& reason) {
  const PreviewCacheStats cacheStats = PreviewCache::stats();
  QString errorMessage;
  if (!PreviewCache::clear(&errorMessage)) {
    setOperationStatus(reason + "失败: " + errorMessage);
    return;
  }
  if (cacheStats.fileCount > 0) {
    setOperationStatus(QString("%1: 已清理 %2 个缓存文件，%3")
                           .arg(reason)
                           .arg(cacheStats.fileCount)
                           .arg(formatBytes(cacheStats.totalBytes)));
  }
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

void MainWindow::setProgressVisible(bool visible) {
  if (!operationProgressBar_) {
    return;
  }
  operationProgressBar_->setVisible(visible);
  if (!visible) {
    operationProgressBar_->setRange(0, 100);
    operationProgressBar_->setValue(0);
  }
}

void MainWindow::setProgressValue(qint64 bytesReceived,
                                  qint64 bytesTotal,
                                  const QString& label) {
  if (!operationProgressBar_) {
    return;
  }
  if (bytesTotal > 0) {
    operationProgressBar_->setRange(0, 100);
    operationProgressBar_->setValue(
        qBound(0, static_cast<int>(bytesReceived * 100 / bytesTotal), 100));
    setOperationStatus(QString("%1: %2 / %3")
                           .arg(label)
                           .arg(formatBytes(bytesReceived))
                           .arg(formatBytes(bytesTotal)));
  } else {
    operationProgressBar_->setRange(0, 0);
    setOperationStatus(QString("%1: 已读取 %2")
                           .arg(label)
                           .arg(formatBytes(bytesReceived)));
  }
  operationProgressBar_->show();
}

void MainWindow::updateEndpointStatus() {
  if (!endpointStatusLabel_) {
    return;
  }
  if (!client_.hasConnection()) {
    endpointStatusLabel_->setText("Endpoint: 未连接");
    return;
  }

  QStringList parts;
  parts << "Endpoint: " + client_.baseUrl();
  if (!connectionLatencyText_.isEmpty()) {
    parts << "延迟: " + connectionLatencyText_;
  }
  if (!connectionVersionText_.isEmpty()) {
    parts << "版本: " + connectionVersionText_;
  }
  endpointStatusLabel_->setText(parts.join("  |  "));
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

void MainWindow::chooseDownloadDirectory() {
  const QString startPath = downloadPathEdit_->text().isEmpty()
                                ? QStandardPaths::writableLocation(
                                      QStandardPaths::DownloadLocation)
                                : downloadPathEdit_->text();
  const QString directory = QFileDialog::getExistingDirectory(
      this, "选择默认下载目录", startPath);
  if (directory.isEmpty()) {
    return;
  }

  downloadPathEdit_->setText(QDir::toNativeSeparators(directory));
  saveSettings();
}

void MainWindow::downloadCurrentFile() {
  const FileItem* item = currentFile();
  if (!item) {
    QMessageBox::information(this, "没有当前文件", "请先选择一个文件。");
    return;
  }
  if (downloadInProgress_) {
    return;
  }

  QString downloadDirectory = downloadPathEdit_->text().trimmed();
  if (downloadDirectory.isEmpty()) {
    chooseDownloadDirectory();
    downloadDirectory = downloadPathEdit_->text().trimmed();
    if (downloadDirectory.isEmpty()) {
      return;
    }
  }
  if (!QDir().mkpath(downloadDirectory)) {
    QMessageBox::warning(this, "下载目录不可用",
                         "无法创建或访问下载目录:\n" + downloadDirectory);
    return;
  }
  downloadCurrentFileFromUrl(*item, {});
}

void MainWindow::downloadCurrentFileFromUrl(const FileItem& item,
                                            const QUrl& url) {
  const QString downloadDirectory = downloadPathEdit_->text().trimmed();
  const QString targetPath =
      QDir(downloadDirectory).filePath(item.entry.name);
  if (QFileInfo::exists(targetPath) &&
      QMessageBox::question(this, "覆盖本地文件",
                            "本地已存在同名文件，是否覆盖？\n" +
                                QDir::toNativeSeparators(targetPath)) !=
          QMessageBox::Yes) {
    return;
  }

  ++preloadGeneration_;
  preloadInProgress_ = false;
  downloadInProgress_ = true;
  updateModeControls();
  setOperationStatus("正在下载: " + item.entry.name);
  Q_UNUSED(url);
  fetchFileData(item, [this, targetPath, fileName = item.entry.name](
                          bool ok, const QString& message,
                          const QByteArray& data) {
    downloadInProgress_ = false;
    updateModeControls();
    if (!ok) {
      setOperationStatus("下载失败: " + fileName);
      QMessageBox::warning(this, "下载失败", message);
      return;
    }

    QSaveFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly)) {
      setOperationStatus("下载保存失败: " + fileName);
      QMessageBox::warning(this, "保存失败", file.errorString());
      return;
    }
    file.write(data);
    if (!file.commit()) {
      setOperationStatus("下载保存失败: " + fileName);
      QMessageBox::warning(this, "保存失败", file.errorString());
      return;
    }

    setOperationStatus("已下载: " + QDir::toNativeSeparators(targetPath));
  }, "正在下载 " + item.entry.name);
}

void MainWindow::deleteCurrentFile() {
  const FileItem* item = currentFile();
  if (!item) {
    QMessageBox::information(this, "没有当前文件", "请先选择一个文件。");
    return;
  }
  if (item->submitted) {
    QMessageBox::information(
        this, "文件已提交",
        "当前文件已经提交过，源位置可能已经发生变化。请刷新目录后再删除。");
    return;
  }
  if (submitInProgress_ || downloadInProgress_ || deleteInProgress_) {
    return;
  }

  const int deleteIndex = currentIndex_;
  const RemoteEntry entry = item->entry;
  QMessageBox confirm(this);
  confirm.setIcon(QMessageBox::Warning);
  confirm.setWindowTitle("确认删除远程文件");
  confirm.setText("确认从 OpenList 删除当前文件？");
  confirm.setInformativeText(
      "此操作会直接删除远程文件，无法通过本程序撤销。\n\n文件: " +
      entry.name + "\n路径: " + entry.path);
  confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  confirm.setDefaultButton(QMessageBox::No);
  if (auto* yesButton = confirm.button(QMessageBox::Yes)) {
    yesButton->setText("删除");
  }
  if (auto* noButton = confirm.button(QMessageBox::No)) {
    noButton->setText("取消");
  }
  if (confirm.exec() != QMessageBox::Yes) {
    return;
  }

  QMessageBox finalConfirm(this);
  finalConfirm.setIcon(QMessageBox::Critical);
  finalConfirm.setWindowTitle("最终确认删除");
  finalConfirm.setText("请再次确认是否删除该远程文件。");
  finalConfirm.setInformativeText(
      "确认后将立即向 OpenList 提交删除请求。\n\n远程路径: " +
      entry.path);
  finalConfirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  finalConfirm.setDefaultButton(QMessageBox::No);
  if (auto* yesButton = finalConfirm.button(QMessageBox::Yes)) {
    yesButton->setText("确认删除");
  }
  if (auto* noButton = finalConfirm.button(QMessageBox::No)) {
    noButton->setText("取消");
  }
  if (finalConfirm.exec() != QMessageBox::Yes) {
    return;
  }

  ++preloadGeneration_;
  preloadInProgress_ = false;
  preloadQueue_.clear();
  preloadQueuePosition_ = 0;
  preloadActiveCount_ = 0;
  preloadCompletedCount_ = 0;
  setProgressVisible(false);
  mediaPlayer_->stop();
  mediaPlayer_->setMedia(QMediaContent());
  deleteInProgress_ = true;
  setOperationStatus("正在删除远程文件: " + entry.name);
  updateModeControls();

  client_.removeFile(parentRemotePath(entry.path), entry.name,
                     [this, deleteIndex, entry](bool ok,
                                                const QString& message) {
                       deleteInProgress_ = false;
                       if (!ok) {
                         setOperationStatus("删除失败: " + entry.name);
                         if (deleteIndex >= 0 && deleteIndex < files_.size() &&
                             files_[deleteIndex].entry.path == entry.path) {
                           files_[deleteIndex].failed = true;
                           files_[deleteIndex].lastError = message;
                           updateFileListRow(deleteIndex);
                         }
                         updateModeControls();
                         QMessageBox::warning(this, "删除失败", message);
                         return;
                       }

                       int removedIndex = deleteIndex;
                       if (removedIndex < 0 || removedIndex >= files_.size() ||
                           files_[removedIndex].entry.path != entry.path) {
                         removedIndex = -1;
                         for (int i = 0; i < files_.size(); ++i) {
                           if (files_[i].entry.path == entry.path) {
                             removedIndex = i;
                             break;
                           }
                         }
                       }
                       if (removedIndex >= 0 && removedIndex < files_.size()) {
                         files_.remove(removedIndex);
                       }

                       currentPixmap_ = QPixmap();
                       imageLabel_->clearImage();
                       if (files_.isEmpty()) {
                         currentIndex_ = -1;
                         currentPage_ = 0;
                         rebuildFileList();
                         rebuildTagChecks();
                         refreshStats();
                         showInfoPage("已删除", "当前目录没有剩余可分类文件。");
                       } else {
                         const int nextIndex =
                             qMin(removedIndex < 0 ? deleteIndex : removedIndex,
                                  files_.size() - 1);
                         currentIndex_ = -1;
                         currentPage_ = qBound(0, nextIndex / pageSize_,
                                               (files_.size() + pageSize_ - 1) /
                                                       pageSize_ -
                                                   1);
                         rebuildFileList();
                         setCurrentIndex(nextIndex);
                       }

                       setOperationStatus("已删除远程文件: " + entry.name);
                       updateModeControls();
                     });
}

void MainWindow::fetchFileData(const FileItem& item,
                               OpenListClient::DownloadCallback callback,
                               const QString& progressLabel) {
  QByteArray cachedData;
  if (PreviewCache::loadRawData(item.entry, &cachedData)) {
    if (!progressLabel.isEmpty()) {
      setProgressVisible(false);
      setOperationStatus(progressLabel + ": 已从缓存读取");
    }
    callback(true, "cached", cachedData);
    return;
  }

  if (!item.entry.rawUrl.isEmpty()) {
    fetchFileDataFromUrl(item.entry,
                         resolvedOpenListUrl(client_.baseUrl(), item.entry.rawUrl),
                         callback, progressLabel);
    return;
  }

  const QString expectedPath = item.entry.path;
  setOperationStatus("正在获取下载地址: " + item.entry.name);
  client_.getFileInfo(
      expectedPath,
      [this, item, expectedPath, callback, progressLabel](
          bool ok, const QString& message, const RemoteEntry& entry) {
        if (!ok || entry.rawUrl.isEmpty()) {
          callback(false, ok ? "OpenList 没有返回下载地址。" : message, {});
          return;
        }
        RemoteEntry cacheEntry = item.entry;
        cacheEntry.rawUrl = entry.rawUrl;
        cacheEntry.thumb = entry.thumb;
        fetchFileDataFromUrl(
            cacheEntry, resolvedOpenListUrl(client_.baseUrl(), entry.rawUrl),
            callback, progressLabel);
      });
}

void MainWindow::fetchFileDataFromUrl(
    const RemoteEntry& entry,
    const QUrl& url,
    OpenListClient::DownloadCallback callback,
    const QString& progressLabel) {
  if (!url.isValid()) {
    callback(false, "文件下载地址无效。", {});
    return;
  }

  const QString label =
      progressLabel.isEmpty() ? "正在读取 " + entry.name : progressLabel;
  client_.downloadUrl(
      url,
      [this, entry, callback](bool ok, const QString& message,
                              const QByteArray& data) {
        setProgressVisible(false);
        if (ok) {
          PreviewCache::storeRawData(entry, data);
        }
        callback(ok, message, data);
      },
      [this, label](qint64 bytesReceived, qint64 bytesTotal) {
        setProgressValue(bytesReceived, bytesTotal, label);
      });
}

void MainWindow::convertCurrentFileOnly() {
  const FileItem* item = currentFile();
  if (!item) {
    QMessageBox::information(this, "没有当前文件", "请先选择一个文件。");
    return;
  }
  if (!isImageFileName(item->entry.name)) {
    QMessageBox::information(this, "不是图片", "当前文件不是可转换的图片。");
    return;
  }
  if (submitInProgress_ || downloadInProgress_ || deleteInProgress_) {
    return;
  }

  if (QMessageBox::warning(
          this, "仅转换当前图片",
          "此操作会把当前图片转成 WebP 上传回原目录，成功后删除 OpenList 上的原文件。\n\n"
          "原文件名和原格式不会保留。继续吗？",
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
      QMessageBox::Yes) {
    return;
  }

  const int fileIndex = currentIndex_;
  QStringList targetDirs;
  targetDirs << parentRemotePath(item->entry.path);
  ++preloadGeneration_;
  preloadInProgress_ = false;
  setProgressVisible(false);
  submitInProgress_ = true;
  updateModeControls();
  submitConvertedImage(
      fileIndex, targetDirs,
      [this, fileIndex](bool ok, const QString& message) {
        submitInProgress_ = false;
        if (fileIndex >= 0 && fileIndex < files_.size()) {
          FileItem& file = files_[fileIndex];
          file.submitted = ok;
          file.failed = !ok;
          file.lastError = ok ? QString() : message;
          updateFileListRow(fileIndex);
          if (fileIndex == currentIndex_) {
            rebuildTagChecks();
            if (ok) {
              showInfoPage("转换完成", escapeHtml(message));
            } else {
              renderCurrentFile();
            }
          }
          refreshStats();
        }
        updateModeControls();
        setOperationStatus(ok ? message : "转换失败: " + message);
        if (!ok) {
          QMessageBox::warning(this, "转换失败", message);
        }
      });
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
  clearPreviewCacheSilently("进入分类前清理缓存");
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
  ++preloadGeneration_;
  preloadInProgress_ = false;
  setProgressVisible(false);
  classificationMode_ = false;
  files_.clear();
  currentIndex_ = -1;
  currentPage_ = 0;
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
  currentPage_ = 0;
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
          preloadCurrentPage();
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
  ensurePageForIndex(index);
  currentIndex_ = index;
  const int row = currentIndex_ - pageStartIndex();
  if (row >= 0 && row < fileList_->count() && fileList_->currentRow() != row) {
    fileList_->setCurrentRow(row);
  }
  renderCurrentFile();
  rebuildTagChecks();
  refreshStats();
  updateModeControls();
}

void MainWindow::renderCurrentFile() {
  const FileItem* item = currentFile();
  currentPixmap_ = QPixmap();
  imageLabel_->clearImage();
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
            files_[currentIndex_].entry.thumb = entry.thumb;
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
  const QString thumbUrl = item.entry.thumb;
  const RemoteEntry cacheEntry = item.entry;
  setOperationStatus("正在准备图片预览: " + fileName);

  auto startDownload = [this, expectedPath, fileName, cacheEntry, rawUrl]() {
    FileItem fetchItem;
    fetchItem.entry = cacheEntry;
    fetchItem.entry.rawUrl = rawUrl;
    fetchFileData(
        fetchItem,
        [this, expectedPath, fileName, cacheEntry](
            bool ok, const QString& message, const QByteArray& data) {
          if (currentFilePath() != expectedPath) {
            return;
          }
          if (!ok) {
            showInfoPage("图片预览失败",
                         formatEntryInfo(*currentFile()) +
                             "<br><br><b>错误：</b>" + escapeHtml(message));
            return;
          }

          setOperationStatus("正在解码图片预览: " + fileName);
          auto* watcher = new QFutureWatcher<ImageDecodeResult>(this);
          QObject::connect(
              watcher, &QFutureWatcher<ImageDecodeResult>::finished, this,
              [this, watcher, expectedPath, fileName]() {
                const ImageDecodeResult decoded = watcher->result();
                watcher->deleteLater();
                if (currentFilePath() != expectedPath) {
                  return;
                }
                if (!decoded.ok) {
                  showInfoPage(
                      "图片预览失败",
                      formatEntryInfo(*currentFile()) +
                          "<br><br><b>错误：</b>" +
                          escapeHtml(decoded.message) +
                          "<br><br><b>当前 Qt 支持格式：</b>" +
                          escapeHtml(ImageDecoder::supportedFormatsSummary()));
                  return;
                }

                currentPixmap_ = QPixmap::fromImage(decoded.image);
                previewTitleLabel_->setText(fileName + " / " +
                                            decoded.decoderName);
                previewStack_->setCurrentWidget(imagePage_);
                setOperationStatus("图片预览已加载: " + fileName);
                updateImagePreviewScale();
              });
          watcher->setFuture(QtConcurrent::run(decodeImageTask, data, fileName,
                                               cacheEntry));
        },
        "正在读取原图 " + fileName);
  };

  auto startThumbnail = [this, expectedPath, fileName, cacheEntry, thumbUrl,
                         startDownload]() {
    Q_UNUSED(expectedPath);
    Q_UNUSED(fileName);
    Q_UNUSED(cacheEntry);
    Q_UNUSED(thumbUrl);
    startDownload();
  };

  auto* cacheWatcher = new QFutureWatcher<ImageCacheLoadResult>(this);
  QObject::connect(
      cacheWatcher, &QFutureWatcher<ImageCacheLoadResult>::finished, this,
      [this, cacheWatcher, expectedPath, fileName, cacheEntry, startThumbnail]() {
        const ImageCacheLoadResult cached = cacheWatcher->result();
        cacheWatcher->deleteLater();
        if (currentFilePath() != expectedPath) {
          return;
        }
        if (cached.ok) {
          currentPixmap_ = QPixmap::fromImage(cached.image);
          previewTitleLabel_->setText(fileName + " / 缓存预览");
          previewStack_->setCurrentWidget(imagePage_);
          setOperationStatus("已从缓存加载图片预览: " + fileName);
          updateImagePreviewScale();
          return;
        }
        Q_UNUSED(cacheEntry);
        startThumbnail();
      });
  cacheWatcher->setFuture(QtConcurrent::run(loadCachedImageTask, cacheEntry));
}

void MainWindow::renderVideo(const FileItem& item, const QString& rawUrl) {
  previewStack_->setCurrentWidget(videoPage_);
  playPauseButton_->setText("播放");
  mediaPlayer_->setMedia(resolvedOpenListUrl(client_.baseUrl(), rawUrl));
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

bool MainWindow::imageLargeEnoughForPreview(const QImage& image) const {
  if (image.isNull()) {
    return false;
  }

  QSize targetSize(720, 420);
  if (imageLabel_) {
    targetSize =
        (imageLabel_->size() - QSize(20, 20)).expandedTo(QSize(480, 320));
  }

  const bool fitsViewport =
      image.width() >= targetSize.width() * 0.75 &&
      image.height() >= targetSize.height() * 0.75;
  const int longEdge = qMax(image.width(), image.height());
  const int shortEdge = qMin(image.width(), image.height());
  return fitsViewport || (longEdge >= 720 && shortEdge >= 360);
}

void MainWindow::updateImagePreviewScale() {
  if (currentPixmap_.isNull() || !imageLabel_) {
    return;
  }
  imageLabel_->setImage(currentPixmap_);
}

void MainWindow::rebuildFileList() {
  if (currentPage_ < 0) {
    currentPage_ = 0;
  }
  const int pageCount =
      files_.isEmpty() ? 0 : (files_.size() + pageSize_ - 1) / pageSize_;
  if (pageCount > 0 && currentPage_ >= pageCount) {
    currentPage_ = pageCount - 1;
  }

  QSignalBlocker blocker(fileList_);
  fileList_->clear();
  for (int i = pageStartIndex(); i < pageEndIndex(); ++i) {
    const FileItem& item = files_[i];
    auto* row = new QListWidgetItem();
    row->setData(Qt::UserRole, i);
    fileList_->addItem(row);
    updateFileListRow(i);
  }
  if (currentIndex_ >= pageStartIndex() && currentIndex_ < pageEndIndex()) {
    fileList_->setCurrentRow(currentIndex_ - pageStartIndex());
  }
  refreshFilePageControls();
}

void MainWindow::updateFileListRow(int index) {
  if (index < pageStartIndex() || index >= pageEndIndex()) {
    return;
  }
  const int rowIndex = index - pageStartIndex();
  if (index < 0 || index >= files_.size() || rowIndex >= fileList_->count()) {
    return;
  }
  const FileItem& item = files_[index];
  const QString tags =
      item.selectedTags.isEmpty() ? "无 tag" : item.selectedTags.join(", ");
  QListWidgetItem* row = fileList_->item(rowIndex);
  row->setText(QString("%1. %2  %3\n%4")
                   .arg(index + 1)
                   .arg(statusTextForFile(item), item.entry.name, tags));
}

int MainWindow::pageStartIndex() const {
  if (files_.isEmpty()) {
    return 0;
  }
  return qBound(0, currentPage_ * pageSize_, files_.size());
}

int MainWindow::pageEndIndex() const {
  if (files_.isEmpty()) {
    return 0;
  }
  return qMin(files_.size(), pageStartIndex() + pageSize_);
}

void MainWindow::setCurrentPage(int page) {
  const int pageCount =
      files_.isEmpty() ? 0 : (files_.size() + pageSize_ - 1) / pageSize_;
  if (pageCount <= 0) {
    currentPage_ = 0;
    rebuildFileList();
    return;
  }

  const int boundedPage = qBound(0, page, pageCount - 1);
  if (currentPage_ == boundedPage) {
    refreshFilePageControls();
    return;
  }

  currentPage_ = boundedPage;
  rebuildFileList();
  if (currentIndex_ < pageStartIndex() || currentIndex_ >= pageEndIndex()) {
    setCurrentIndex(pageStartIndex());
  }
  preloadCurrentPage();
}

void MainWindow::ensurePageForIndex(int index) {
  if (index < 0 || index >= files_.size()) {
    return;
  }

  const int page = index / pageSize_;
  if (page != currentPage_) {
    currentPage_ = page;
    rebuildFileList();
    preloadCurrentPage();
  }
}

void MainWindow::refreshFilePageControls() {
  const int pageCount =
      files_.isEmpty() ? 0 : (files_.size() + pageSize_ - 1) / pageSize_;
  if (pageInfoLabel_) {
    if (pageCount == 0) {
      pageInfoLabel_->setText("第 0 / 0 页");
    } else {
      pageInfoLabel_->setText(
          QString("第 %1 / %2 页  %3-%4 / %5")
              .arg(currentPage_ + 1)
              .arg(pageCount)
              .arg(pageStartIndex() + 1)
              .arg(pageEndIndex())
              .arg(files_.size()));
    }
  }
  if (prevPageButton_) {
    prevPageButton_->setEnabled(pageCount > 0 && currentPage_ > 0);
  }
  if (nextPageButton_) {
    nextPageButton_->setEnabled(pageCount > 0 && currentPage_ < pageCount - 1);
  }
}

void MainWindow::preloadCurrentPage() {
  ++preloadGeneration_;
  preloadQueue_.clear();
  preloadQueuePosition_ = 0;
  preloadActiveCount_ = 0;
  preloadCompletedCount_ = 0;
  preloadInProgress_ = false;

  if (!classificationMode_ || submitInProgress_ || downloadInProgress_ ||
      deleteInProgress_ || files_.isEmpty()) {
    setProgressVisible(false);
    return;
  }

  for (int i = pageStartIndex(); i < pageEndIndex(); ++i) {
    preloadQueue_.push_back(i);
  }
  if (preloadQueue_.isEmpty()) {
    setProgressVisible(false);
    return;
  }

  preloadInProgress_ = true;
  setOperationStatus(QString("正在预热当前页缓存: 0 / %1")
                         .arg(preloadQueue_.size()));
  if (operationProgressBar_) {
    operationProgressBar_->setRange(0, preloadQueue_.size());
    operationProgressBar_->setValue(0);
    operationProgressBar_->show();
  }

  const int generation = preloadGeneration_;
  QTimer::singleShot(0, this, [this, generation]() {
    startPreloadWorkers(generation);
  });
}

void MainWindow::startPreloadWorkers(int generation) {
  if (generation != preloadGeneration_ || !classificationMode_ ||
      submitInProgress_ || downloadInProgress_ || deleteInProgress_) {
    preloadInProgress_ = false;
    setProgressVisible(false);
    return;
  }

  if (preloadCompletedCount_ >= preloadQueue_.size() &&
      preloadActiveCount_ == 0) {
    preloadInProgress_ = false;
    setProgressVisible(false);
    setOperationStatus(QString("当前页缓存已准备: %1-%2 / %3")
                           .arg(pageStartIndex() + 1)
                           .arg(pageEndIndex())
                           .arg(files_.size()));
    return;
  }

  const int concurrency = qBound(1, preloadConcurrency_, 10);
  while (preloadActiveCount_ < concurrency &&
         preloadQueuePosition_ < preloadQueue_.size()) {
    const int position = preloadQueuePosition_;
    ++preloadQueuePosition_;
    ++preloadActiveCount_;
    preloadNextPageItem(generation, position);
  }
}

void MainWindow::finishPreloadItem(int generation, const QString& message) {
  if (generation != preloadGeneration_) {
    return;
  }
  if (preloadActiveCount_ > 0) {
    --preloadActiveCount_;
  }
  ++preloadCompletedCount_;
  if (operationProgressBar_) {
    operationProgressBar_->setRange(0, preloadQueue_.size());
    operationProgressBar_->setValue(preloadCompletedCount_);
    operationProgressBar_->show();
  }
  if (!message.isEmpty()) {
    setOperationStatus(QString("预热当前页缓存: %1 / %2（%3）")
                           .arg(preloadCompletedCount_)
                           .arg(preloadQueue_.size())
                           .arg(message));
  }
  startPreloadWorkers(generation);
}

void MainWindow::preloadNextPageItem(int generation, int position) {
  if (generation != preloadGeneration_ || !classificationMode_ ||
      submitInProgress_ || downloadInProgress_ || deleteInProgress_) {
    return;
  }

  auto complete = [this, generation](const QString& message) {
    QTimer::singleShot(0, this, [this, generation, message]() {
      finishPreloadItem(generation, message);
    });
  };

  const int index = preloadQueue_[position];
  if (index < 0 || index >= files_.size()) {
    complete("跳过无效项");
    return;
  }

  FileItem item = files_[index];
  const int humanPosition = position + 1;
  if (!isImageFileName(item.entry.name)) {
    complete("非图片");
    return;
  }

  auto decodeHeifCache = [this, generation, item, humanPosition, complete](
                             const QByteArray& data) {
    setOperationStatus(QString("正在生成 HEIF JPG 缓存 %1 / %2: %3")
                           .arg(humanPosition)
                           .arg(preloadQueue_.size())
                           .arg(item.entry.name));
    auto* watcher = new QFutureWatcher<ImageDecodeResult>(this);
    QObject::connect(
        watcher, &QFutureWatcher<ImageDecodeResult>::finished, this,
        [this, watcher, generation, item, complete]() {
          const ImageDecodeResult decoded = watcher->result();
          watcher->deleteLater();
          if (generation != preloadGeneration_) {
            return;
          }
          if (decoded.ok) {
            complete("HEIF JPG 缓存");
          } else {
            complete("HEIF JPG 缓存失败: " + decoded.message);
          }
        });
    watcher->setFuture(
        QtConcurrent::run(decodeImageTask, data, item.entry.name, item.entry));
  };

  QImage cachedImage;
  if (PreviewCache::loadImage(item.entry, &cachedImage)) {
    complete("JPG 解码缓存");
    return;
  }

  QByteArray cachedRawData;
  if (PreviewCache::loadRawData(item.entry, &cachedRawData)) {
    if (ImageDecoder::isHeifLikeData(cachedRawData, item.entry.name)) {
      decodeHeifCache(cachedRawData);
      return;
    }
    complete("原始数据缓存");
    return;
  }

  if (item.entry.rawUrl.isEmpty()) {
    setOperationStatus(QString("正在获取预热地址 %1 / %2: %3")
                           .arg(humanPosition)
                           .arg(preloadQueue_.size())
                           .arg(item.entry.name));
    client_.getFileInfo(
        item.entry.path,
        [this, generation, position, index](
            bool ok, const QString&, const RemoteEntry& entry) {
          if (generation != preloadGeneration_) {
            return;
          }
          if (ok && !entry.rawUrl.isEmpty() && index >= 0 &&
              index < files_.size()) {
            files_[index].entry.rawUrl = entry.rawUrl;
            files_[index].entry.thumb = entry.thumb;
            preloadNextPageItem(generation, position);
            return;
          }
          finishPreloadItem(generation, "下载地址获取失败");
        });
    return;
  }

  setOperationStatus(QString("正在预热当前页缓存 %1 / %2: %3")
                         .arg(humanPosition)
                         .arg(preloadQueue_.size())
                         .arg(item.entry.name));
  fetchFileData(
      item,
      [this, generation, item, decodeHeifCache, complete](
          bool ok, const QString& message, const QByteArray& data) {
        if (generation != preloadGeneration_) {
          return;
        }
        if (!ok) {
          complete("预热失败: " + message);
          return;
        }

        if (ImageDecoder::isHeifLikeData(data, item.entry.name)) {
          decodeHeifCache(data);
          return;
        }

        complete("原始数据缓存");
      },
      QString("预热读取 %1 / %2 %3")
          .arg(humanPosition)
          .arg(preloadQueue_.size())
          .arg(item.entry.name));
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
  saveButton_->setEnabled(classificationMode_ && pending > 0 &&
                          !submitInProgress_ && !downloadInProgress_ &&
                          !deleteInProgress_);
  refreshFilePageControls();
}

void MainWindow::refreshTagTable() {
  tagTable_->setRowCount(tags_.size());
  for (int i = 0; i < tags_.size(); ++i) {
    tagTable_->setItem(i, 0, new QTableWidgetItem(tags_[i].name));
    tagTable_->setItem(i, 1, new QTableWidgetItem(tags_[i].targetPath));
  }
  tagTable_->resizeColumnToContents(0);
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
  const bool busy = submitInProgress_ || downloadInProgress_ || deleteInProgress_;
  browseSourceButton_->setEnabled(connected && !classificationMode_ && !busy);
  browseDownloadPathButton_->setEnabled(!busy);
  downloadCurrentButton_->setEnabled(connected && currentFile() && !busy);
  convertCurrentButton_->setEnabled(connected && currentFile() &&
                                    isImageFileName(currentFile()->entry.name) &&
                                    !busy);
  deleteCurrentButton_->setEnabled(connected && currentFile() &&
                                   !currentFile()->submitted && !busy);
  enterModeButton_->setEnabled(connected && !busy);
  enterModeButton_->setText(classificationMode_ ? "退出分类模式" : "进入分类模式");
  sourcePathEdit_->setEnabled(!classificationMode_);
  downloadPathEdit_->setEnabled(!busy);
  convertImageMoveCheck_->setEnabled(!busy);
  addTagButton_->setEnabled(!busy);
  editTagButton_->setEnabled(!busy);
  removeTagButton_->setEnabled(!busy);
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
  int convertedImageMoveCount = 0;
  for (int i = 0; i < files_.size(); ++i) {
    if (!files_[i].submitted && !files_[i].selectedTags.isEmpty()) {
      pendingIndexes.push_back(i);
      hasMultiTag = hasMultiTag || files_[i].selectedTags.size() > 1;
      if (convertImageMoveCheck_->isChecked() &&
          isImageFileName(files_[i].entry.name)) {
        ++convertedImageMoveCount;
      }
    }
  }
  if (pendingIndexes.isEmpty()) {
    QMessageBox::information(this, "没有待保存项", "当前没有已选择但未提交的文件。");
    return;
  }
  if (hasMultiTag &&
      QMessageBox::question(this, "多 tag 复制提示",
                            convertImageMoveCheck_->isChecked()
                                ? "有文件选择了多个 tag。图片会转成 WebP 上传到多个目标目录并删除源文件；非图片仍会复制到多个目标目录并保留源文件。继续吗？"
                                : "有文件选择了多个 tag，保存时会复制到多个目标目录，原文件会保留在源目录。继续吗？") !=
          QMessageBox::Yes) {
    return;
  }
  if (convertedImageMoveCount > 0 &&
      QMessageBox::warning(
          this, "图片转 WebP 后保存",
          QString("已启用“保存时转 WebP”。\n\n"
                  "本次有 %1 个图片不会直接移动或复制原文件；程序会下载原图，"
                  "按原文件 SHA-256 转成 WebP 上传到已选择的目标目录，然后删除 OpenList "
                  "源目录中的原文件。\n\n"
                  "原文件名和原格式不会保留。继续吗？")
              .arg(convertedImageMoveCount),
          QMessageBox::Yes | QMessageBox::No,
          QMessageBox::No) != QMessageBox::Yes) {
    return;
  }

  ++preloadGeneration_;
  preloadInProgress_ = false;
  setProgressVisible(false);
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

  QStringList targetDirs;
  for (const QString& tagName : file.selectedTags) {
    const QString targetPath = targetPathForTag(tagName);
    if (!targetDirs.contains(targetPath)) {
      targetDirs.push_back(targetPath);
    }
  }

  if (convertImageMoveCheck_->isChecked() &&
      isImageFileName(file.entry.name)) {
    submitConvertedImage(index, targetDirs, callback);
    return;
  }

  if (file.selectedTags.size() == 1) {
    client_.moveFile(sourcePath_, file.entry.name,
                     targetDirs.first(),
                     overwriteCheck_->isChecked(), callback);
    return;
  }

  submitCopyTargets(index, file.selectedTags, 0, callback);
}

void MainWindow::submitConvertedImage(
    int index,
    const QStringList& targetDirs,
    OpenListClient::SimpleCallback callback) {
  if (index < 0 || index >= files_.size()) {
    callback(false, "内部索引错误");
    return;
  }
  if (targetDirs.isEmpty()) {
    callback(false, "没有可上传的目标目录");
    return;
  }

  const FileItem file = files_[index];
  setOperationStatus("正在准备待转换图片: " + file.entry.name);
  fetchFileData(
      file,
      [this, index, file, targetDirs, callback](
          bool ok, const QString& message, const QByteArray& data) {
        if (!ok) {
          callback(false, "获取待转换图片失败: " + message);
          return;
        }

        setOperationStatus("正在转 WebP: " + file.entry.name);
        auto* watcher = new QFutureWatcher<ImageConversionResult>(this);
        QObject::connect(
            watcher, &QFutureWatcher<ImageConversionResult>::finished, this,
            [this, watcher, index, targetDirs, callback]() {
              const ImageConversionResult converted = watcher->result();
              watcher->deleteLater();
              if (!converted.ok) {
                callback(false, converted.message);
                return;
              }

              uploadConvertedImageTargets(index, converted, targetDirs, 0,
                                          callback);
            });
        watcher->setFuture(QtConcurrent::run(convertImageToWebpTask, data,
                                             file.entry.name, file.entry));
      });
}

void MainWindow::uploadConvertedImageTargets(
    int index,
    const ImageConversionResult& converted,
    const QStringList& targetDirs,
    int targetPosition,
    OpenListClient::SimpleCallback callback) {
  if (targetPosition >= targetDirs.size()) {
    removeSubmittedSourceFile(
        index,
        QString("已转 WebP 并处理 %1 个目标目录: %2（SHA-256 来源：%3）")
            .arg(targetDirs.size())
            .arg(converted.outputName)
            .arg(converted.hashSource),
        callback);
    return;
  }

  const QString targetDir = targetDirs[targetPosition];
  setOperationStatus(
      QString("正在检查目标目录 %1 / %2: %3")
          .arg(targetPosition + 1)
          .arg(targetDirs.size())
          .arg(targetDir));

  client_.listDirectory(
      targetDir, false,
      [this, index, converted, targetDirs, targetPosition, callback](
          bool ok, const QString& message, const QString&,
          const QVector<RemoteEntry>& entries) {
        if (!ok) {
          callback(false, "检查目标目录失败: " + message);
          return;
        }

        bool duplicateExists = false;
        for (const RemoteEntry& entry : entries) {
          if (entry.isDir || lowerSuffix(entry.name) != "webp") {
            continue;
          }
          if (index >= 0 && index < files_.size() &&
              entry.path == files_[index].entry.path) {
            continue;
          }
          const QString baseName = QFileInfo(entry.name).completeBaseName();
          if (baseName == converted.sha256 || baseName == converted.md5) {
            duplicateExists = true;
            break;
          }
        }

        if (duplicateExists) {
          uploadConvertedImageTargets(index, converted, targetDirs,
                                      targetPosition + 1, callback);
          return;
        }

        const QString uploadPath =
            joinRemotePath(targetDirs[targetPosition], converted.outputName);
        if (index >= 0 && index < files_.size() &&
            normalizeRemotePath(uploadPath) == normalizeRemotePath(files_[index].entry.path)) {
          callback(false, "当前文件已经是目标 WebP 文件，无需转换。");
          return;
        }
        setOperationStatus(
            QString("正在上传 WebP %1 / %2: %3")
                .arg(targetPosition + 1)
                .arg(targetDirs.size())
                .arg(converted.outputName));
        client_.uploadFile(
            uploadPath, converted.data, overwriteCheck_->isChecked(),
            [this, index, converted, targetDirs, targetPosition, callback](
                bool ok, const QString& message) {
              if (!ok) {
                callback(false, "上传 WebP 失败: " + message);
                return;
              }
              uploadConvertedImageTargets(index, converted, targetDirs,
                                          targetPosition + 1, callback);
            });
      });
}

void MainWindow::removeSubmittedSourceFile(
    int index,
    const QString& successMessage,
    OpenListClient::SimpleCallback callback) {
  if (index < 0 || index >= files_.size()) {
    callback(false, "内部索引错误");
    return;
  }

  const RemoteEntry entry = files_[index].entry;
  setOperationStatus("正在删除源文件: " + entry.name);
  client_.removeFile(parentRemotePath(entry.path), entry.name,
                     [callback, successMessage](bool ok,
                                                const QString& message) {
                       if (!ok) {
                         callback(false,
                                  "转换上传已完成，但删除源文件失败: " + message);
                         return;
                       }
                       callback(true, successMessage);
                     });
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
