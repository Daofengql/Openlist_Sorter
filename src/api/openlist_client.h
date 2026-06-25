#ifndef OPENLIST_SORTER_OPENLIST_CLIENT_H
#define OPENLIST_SORTER_OPENLIST_CLIENT_H

#include <functional>

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

#include "core/models.h"

struct ConnectionDiagnostics {
  QString endpoint;
  bool connected{};
  bool publicSettingsOk{};
  qint64 latencyMs{-1};
  QString ipAddress;
  QString version;
  QString siteTitle;
  QString message;
};

class OpenListClient : public QObject {
  Q_OBJECT

 public:
  using SimpleCallback = std::function<void(bool ok, const QString& message)>;
  using ListCallback = std::function<void(
      bool ok, const QString& message, const QString& path,
      const QVector<RemoteEntry>& entries)>;
  using EntryCallback =
      std::function<void(bool ok, const QString& message, const RemoteEntry& entry)>;
  using DownloadCallback =
      std::function<void(bool ok, const QString& message, const QByteArray& data)>;
  using DownloadProgressCallback =
      std::function<void(qint64 bytesReceived, qint64 bytesTotal)>;
  using DiagnosticsCallback =
      std::function<void(const ConnectionDiagnostics& diagnostics)>;

  explicit OpenListClient(QObject* parent = nullptr);

  void setBaseUrl(const QString& baseUrl);
  void setToken(const QString& token);
  void clearConnection();

  QString baseUrl() const;
  QString token() const;
  bool hasConnection() const;

  void login(const QString& baseUrl,
             const QString& username,
             const QString& password,
             SimpleCallback callback);
  void validateToken(const QString& baseUrl,
                     const QString& token,
                     SimpleCallback callback);
  void fetchConnectionDiagnostics(DiagnosticsCallback callback);
  void listDirectory(const QString& path,
                     bool refresh,
                     ListCallback callback);
  void getFileInfo(const QString& path, EntryCallback callback);
  void downloadUrl(const QUrl& url,
                   DownloadCallback callback,
                   DownloadProgressCallback progressCallback = {});
  void uploadFile(const QString& remotePath,
                  const QByteArray& data,
                  bool overwrite,
                  SimpleCallback callback);
  void removeFile(const QString& dir,
                  const QString& fileName,
                  SimpleCallback callback);
  void moveFile(const QString& srcDir,
                const QString& fileName,
                const QString& dstDir,
                bool overwrite,
                SimpleCallback callback);
  void copyFile(const QString& srcDir,
                const QString& fileName,
                const QString& dstDir,
                bool overwrite,
                SimpleCallback callback);

 private:
  using JsonCallback = std::function<void(
      bool ok, const QString& message, const QJsonObject& data)>;

  QNetworkRequest makeRequest(const QString& endpoint) const;
  void postJson(const QString& endpoint,
                const QJsonObject& payload,
                JsonCallback callback);
  void getJson(const QString& endpoint, JsonCallback callback);
  void moveOrCopy(const QString& endpoint,
                  const QString& srcDir,
                  const QString& fileName,
                  const QString& dstDir,
                  bool overwrite,
                  SimpleCallback callback);

  static RemoteEntry entryFromJson(const QJsonObject& object,
                                   const QString& parentPath);
  static QDateTime parseDateTime(const QJsonValue& value);

  QNetworkAccessManager network_;
  QString baseUrl_;
  QString token_;
};

#endif
