#ifndef OPENLIST_SORTER_CONNECTION_INFO_DIALOG_H
#define OPENLIST_SORTER_CONNECTION_INFO_DIALOG_H

#include <QDialog>
#include <QString>

class ConnectionInfoDialog : public QDialog {
  Q_OBJECT

 public:
  explicit ConnectionInfoDialog(const QString& endpoint,
                                bool connected,
                                QWidget* parent = nullptr);

  bool reconnectRequested() const;

 private:
  bool reconnectRequested_{};
};

#endif

