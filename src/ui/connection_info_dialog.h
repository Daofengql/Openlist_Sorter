#ifndef OPENLIST_SORTER_CONNECTION_INFO_DIALOG_H
#define OPENLIST_SORTER_CONNECTION_INFO_DIALOG_H

#include <QDialog>
#include <QLabel>
#include <QString>

#include "api/openlist_client.h"

class ConnectionInfoDialog : public QDialog {
  Q_OBJECT

 public:
  explicit ConnectionInfoDialog(const QString& endpoint,
                                bool connected,
                                QWidget* parent = nullptr);

  void setDiagnostics(const ConnectionDiagnostics& diagnostics);

 private:
  QLabel* stateLabel_{};
  QLabel* latencyLabel_{};
  QLabel* ipLabel_{};
  QLabel* versionLabel_{};
  QLabel* siteTitleLabel_{};
  QLabel* messageLabel_{};
};

#endif
