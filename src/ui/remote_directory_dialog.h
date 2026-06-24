#ifndef OPENLIST_SORTER_REMOTE_DIRECTORY_DIALOG_H
#define OPENLIST_SORTER_REMOTE_DIRECTORY_DIALOG_H

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

#include "api/openlist_client.h"

class RemoteDirectoryDialog : public QDialog {
  Q_OBJECT

 public:
  explicit RemoteDirectoryDialog(OpenListClient* client,
                                 const QString& startPath = "/",
                                 QWidget* parent = nullptr);

  QString selectedPath() const;

 private:
  void buildUi();
  void connectSignals();
  void loadDirectory(const QString& path, bool refresh);
  void setBusy(bool busy);
  void showMessage(const QString& message);

  OpenListClient* client_{};
  QString currentPath_{"/"};
  QString selectedPath_{"/"};

  QLineEdit* pathEdit_{};
  QLabel* statusLabel_{};
  QListWidget* directoryList_{};
  QPushButton* rootButton_{};
  QPushButton* upButton_{};
  QPushButton* refreshButton_{};
  QPushButton* selectButton_{};
  QPushButton* cancelButton_{};
};

#endif
