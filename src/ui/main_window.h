#ifndef OPENLIST_SORTER_MAIN_WINDOW_H
#define OPENLIST_SORTER_MAIN_WINDOW_H

#include <QCheckBox>
#include <QCloseEvent>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMediaPlayer>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVideoWidget>
#include <QVBoxLayout>
#include <QVector>

#include "api/openlist_client.h"
#include "core/models.h"
#include "ui/zoomable_image_label.h"

struct ImageConversionResult;

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  bool ensureInitialConnection();

 protected:
  void closeEvent(QCloseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  void buildUi();
  void buildMenuBar();
  void buildTaskPanel(QVBoxLayout* rootLayout);
  QWidget* buildStatsPanel();
  QWidget* buildFilePanel();
  QWidget* buildPreviewPanel();
  QWidget* buildTagPanel();
  void connectSignals();
  void applyModernStyle();

  void loadSettings();
  void saveSettings() const;

  bool showConnectionDialog(bool initialLaunch = false);
  void showConnectionInfoDialog();
  void showSettingsDialog();
  void showAboutDialog();
  void openProjectHomepage();
  void openAuthorBlog();
  void logout();
  void refreshConnectionDiagnostics();
  void clearPreviewCache();
  void clearPreviewCacheSilently(const QString& reason);
  void clearCurrentSelection();
  void clearAllPendingSelections();
  void setOperationStatus(const QString& message);
  void updateEndpointStatus();
  void chooseSourceDirectory();
  void chooseDownloadDirectory();
  void downloadCurrentFile();
  void downloadCurrentFileFromUrl(const FileItem& item, const QUrl& url);
  void deleteCurrentFile();
  void fetchFileData(const FileItem& item,
                     OpenListClient::DownloadCallback callback,
                     const QString& progressLabel = QString());
  void fetchFileDataFromUrl(const RemoteEntry& entry,
                            const QUrl& url,
                            OpenListClient::DownloadCallback callback,
                            const QString& progressLabel = QString());
  void convertCurrentFileOnly();
  void enterClassificationMode();
  void exitClassificationMode();
  void loadSourceDirectory();

  void setCurrentIndex(int index);
  void renderCurrentFile();
  void renderImage(const FileItem& item, const QString& rawUrl);
  void renderVideo(const FileItem& item, const QString& rawUrl);
  void showInfoPage(const QString& title, const QString& body);
  QString formatEntryInfo(const FileItem& item) const;
  bool imageLargeEnoughForPreview(const QImage& image) const;
  void updateImagePreviewScale();

  void rebuildFileList();
  void updateFileListRow(int index);
  int pageStartIndex() const;
  int pageEndIndex() const;
  void setCurrentPage(int page);
  void ensurePageForIndex(int index);
  void refreshFilePageControls();
  void preloadCurrentPage();
  void preloadNextPageItem(int generation, int index);
  void startPreloadWorkers(int generation);
  void finishPreloadItem(int generation, const QString& message);
  void setProgressVisible(bool visible);
  void setProgressValue(qint64 bytesReceived,
                        qint64 bytesTotal,
                        const QString& label);
  void refreshStats();
  void refreshTagTable();
  void rebuildTagChecks();
  void onTagSelectionChanged();
  void updateModeControls();

  void addTag();
  void editSelectedTag();
  void removeSelectedTag();
  bool editTagDialog(TagMapping* tag, const QString& title);
  QString targetPathForTag(const QString& tagName) const;

  void savePending();
  void submitNextFile(const QVector<int>& pendingIndexes, int position);
  void submitFile(int index, OpenListClient::SimpleCallback callback);
  void submitConvertedImage(int index,
                            const QStringList& targetDirs,
                            OpenListClient::SimpleCallback callback);
  void uploadConvertedImageTargets(int index,
                                   const ImageConversionResult& converted,
                                   const QStringList& targetDirs,
                                   int targetPosition,
                                   OpenListClient::SimpleCallback callback);
  void removeSubmittedSourceFile(int index,
                                 const QString& successMessage,
                                 OpenListClient::SimpleCallback callback);
  void submitCopyTargets(int fileIndex,
                         const QStringList& selectedTags,
                         int tagPosition,
                         OpenListClient::SimpleCallback callback);

  QString currentFilePath() const;
  FileItem* currentFile();
  const FileItem* currentFile() const;

  OpenListClient client_;

  QLineEdit* sourcePathEdit_{};
  QPushButton* browseSourceButton_{};
  QLineEdit* downloadPathEdit_{};
  QPushButton* browseDownloadPathButton_{};
  QPushButton* downloadCurrentButton_{};
  QPushButton* convertCurrentButton_{};
  QPushButton* deleteCurrentButton_{};
  QPushButton* enterModeButton_{};
  QPushButton* saveButton_{};
  QCheckBox* overwriteCheck_{};
  QCheckBox* convertImageMoveCheck_{};

  QLabel* totalLabel_{};
  QLabel* submittedLabel_{};
  QLabel* pendingLabel_{};
  QLabel* unprocessedLabel_{};
  QLabel* currentIndexLabel_{};

  QListWidget* fileList_{};
  QPushButton* prevPageButton_{};
  QPushButton* nextPageButton_{};
  QLabel* pageInfoLabel_{};

  QLabel* currentPathLabel_{};
  QLabel* previewTitleLabel_{};
  QStackedWidget* previewStack_{};
  QWidget* emptyPage_{};
  QWidget* imagePage_{};
  QWidget* videoPage_{};
  QWidget* infoPage_{};
  QLabel* emptyLabel_{};
  ZoomableImageLabel* imageLabel_{};
  QLabel* infoLabel_{};
  QVideoWidget* videoWidget_{};
  QMediaPlayer* mediaPlayer_{};
  QPushButton* playPauseButton_{};
  QPushButton* prevButton_{};
  QPushButton* nextButton_{};

  QLabel* endpointStatusLabel_{};
  QLabel* operationStatusLabel_{};
  QProgressBar* operationProgressBar_{};
  QString connectionLatencyText_;
  QString connectionIpText_;
  QString connectionVersionText_;

  QTableWidget* tagTable_{};
  QPushButton* addTagButton_{};
  QPushButton* editTagButton_{};
  QPushButton* removeTagButton_{};
  QScrollArea* tagScrollArea_{};
  QWidget* tagCheckContainer_{};
  QVBoxLayout* tagCheckLayout_{};
  QLabel* multiTagNotice_{};
  QVector<QCheckBox*> tagCheckBoxes_;

  QString sourcePath_{"/"};
  QVector<TagMapping> tags_;
  QVector<FileItem> files_;
  int currentIndex_{-1};
  int currentPage_{};
  int pageSize_{20};
  int preloadConcurrency_{2};
  int preloadGeneration_{};
  bool preloadInProgress_{};
  QVector<int> preloadQueue_;
  int preloadQueuePosition_{};
  int preloadActiveCount_{};
  int preloadCompletedCount_{};
  bool classificationMode_{};
  bool submitInProgress_{};
  bool downloadInProgress_{};
  bool deleteInProgress_{};
  bool suppressTagChange_{};
  QPixmap currentPixmap_;
};

#endif
