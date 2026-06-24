#ifndef OPENLIST_SORTER_MAIN_WINDOW_H
#define OPENLIST_SORTER_MAIN_WINDOW_H

#include <QCheckBox>
#include <QCloseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMediaPlayer>
#include <QPixmap>
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
  void showAboutDialog();
  void clearCurrentSelection();
  void clearAllPendingSelections();
  void setOperationStatus(const QString& message);
  void updateEndpointStatus();
  void chooseSourceDirectory();
  void enterClassificationMode();
  void exitClassificationMode();
  void loadSourceDirectory();

  void setCurrentIndex(int index);
  void renderCurrentFile();
  void renderImage(const FileItem& item, const QString& rawUrl);
  void renderVideo(const FileItem& item, const QString& rawUrl);
  void showInfoPage(const QString& title, const QString& body);
  QString formatEntryInfo(const FileItem& item) const;
  void updateImagePreviewScale();

  void rebuildFileList();
  void updateFileListRow(int index);
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
  QPushButton* enterModeButton_{};
  QPushButton* saveButton_{};
  QCheckBox* overwriteCheck_{};

  QLabel* totalLabel_{};
  QLabel* submittedLabel_{};
  QLabel* pendingLabel_{};
  QLabel* unprocessedLabel_{};
  QLabel* currentIndexLabel_{};

  QListWidget* fileList_{};

  QLabel* currentPathLabel_{};
  QLabel* previewTitleLabel_{};
  QStackedWidget* previewStack_{};
  QWidget* emptyPage_{};
  QWidget* imagePage_{};
  QWidget* videoPage_{};
  QWidget* infoPage_{};
  QLabel* emptyLabel_{};
  QLabel* imageLabel_{};
  QLabel* infoLabel_{};
  QVideoWidget* videoWidget_{};
  QMediaPlayer* mediaPlayer_{};
  QPushButton* playPauseButton_{};
  QPushButton* prevButton_{};
  QPushButton* nextButton_{};

  QLabel* endpointStatusLabel_{};
  QLabel* operationStatusLabel_{};

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
  bool classificationMode_{};
  bool submitInProgress_{};
  bool suppressTagChange_{};
  QPixmap currentPixmap_;
};

#endif
