#ifndef OPENLIST_SORTER_CONNECTION_DIALOG_H
#define OPENLIST_SORTER_CONNECTION_DIALOG_H

#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>

#include "api/openlist_client.h"
#include "core/connection_profile.h"

class ConnectionDialog : public QDialog {
  Q_OBJECT

 public:
  explicit ConnectionDialog(OpenListClient* client, QWidget* parent = nullptr);

  ConnectionProfile profile() const;

 private:
  void buildUi();
  void connectSignals();
  void loadCachedProfile();
  void updateAuthMode();
  void updateRememberLogic();
  void startConnection();
  void finishConnection(bool ok, const QString& message);

  OpenListClient* client_{};

  QLineEdit* endpointEdit_{};
  QRadioButton* tokenModeRadio_{};
  QRadioButton* passwordModeRadio_{};
  QStackedWidget* authStack_{};
  QLineEdit* tokenEdit_{};
  QLineEdit* usernameEdit_{};
  QLineEdit* passwordEdit_{};
  QCheckBox* rememberCheck_{};
  QCheckBox* autoLoginCheck_{};
  QLabel* statusLabel_{};
  QPushButton* connectButton_{};
  QPushButton* cancelButton_{};

  ConnectionProfile currentProfile_;
};

#endif

