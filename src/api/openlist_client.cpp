#include "api/openlist_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>

namespace {

QString sanitizeBaseUrl(QString baseUrl) {
  baseUrl = baseUrl.trimmed();
  while (baseUrl.endsWith('/')) {
    baseUrl.chop(1);
  }
  return baseUrl;
}

QJsonObject parseApiResponse(const QByteArray& payload,
                             bool* ok,
                             QString* message,
                             QJsonObject* data) {
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    *ok = false;
    *message = "无法解析 OpenList 返回内容: " + parseError.errorString();
    return {};
  }

  const QJsonObject object = document.object();
  const int code = object.value("code").toInt(-1);
  *message = object.value("message").toString();
  if (message->isEmpty()) {
    *message = object.value("msg").toString();
  }

  if (code != 200) {
    *ok = false;
    if (message->isEmpty()) {
      *message = "OpenList API 返回错误码 " + QString::number(code);
    }
    return object;
  }

  const QJsonValue dataValue = object.value("data");
  if (dataValue.isObject()) {
    *data = dataValue.toObject();
  } else {
    QJsonObject wrapped;
    wrapped.insert("value", dataValue);
    *data = wrapped;
  }
  *ok = true;
  if (message->isEmpty()) {
    *message = "success";
  }
  return object;
}

}  // namespace

OpenListClient::OpenListClient(QObject* parent) : QObject(parent) {}

void OpenListClient::setBaseUrl(const QString& baseUrl) {
  baseUrl_ = sanitizeBaseUrl(baseUrl);
}

void OpenListClient::setToken(const QString& token) {
  token_ = token.trimmed();
}

QString OpenListClient::baseUrl() const {
  return baseUrl_;
}

QString OpenListClient::token() const {
  return token_;
}

bool OpenListClient::hasConnection() const {
  return !baseUrl_.isEmpty() && !token_.isEmpty();
}

void OpenListClient::login(const QString& baseUrl,
                           const QString& username,
                           const QString& password,
                           SimpleCallback callback) {
  setBaseUrl(baseUrl);
  token_.clear();

  QJsonObject payload;
  payload.insert("username", username);
  payload.insert("password", password);
  postJson("/api/auth/login", payload,
           [this, callback](bool ok, const QString& message,
                            const QJsonObject& data) {
             if (ok) {
               const QString token = data.value("token").toString();
               if (token.isEmpty()) {
                 callback(false, "登录成功但没有返回 token");
                 return;
               }
               token_ = token;
             }
             callback(ok, message);
           });
}

void OpenListClient::validateToken(const QString& baseUrl,
                                   const QString& token,
                                   SimpleCallback callback) {
  setBaseUrl(baseUrl);
  setToken(token);
  listDirectory("/", false,
                [callback](bool ok, const QString& message, const QString&,
                           const QVector<RemoteEntry>&) {
                  callback(ok, ok ? "token 可用" : message);
                });
}

void OpenListClient::listDirectory(const QString& path,
                                   bool refresh,
                                   ListCallback callback) {
  const QString normalizedPath = normalizeRemotePath(path);
  QJsonObject payload;
  payload.insert("path", normalizedPath);
  payload.insert("password", "");
  payload.insert("page", 1);
  payload.insert("per_page", 0);
  payload.insert("refresh", refresh);

  postJson("/api/fs/list", payload,
           [callback, normalizedPath](bool ok, const QString& message,
                                      const QJsonObject& data) {
             QVector<RemoteEntry> entries;
             if (ok) {
               const QJsonArray content = data.value("content").toArray();
               entries.reserve(content.size());
               for (const QJsonValue& value : content) {
                 if (value.isObject()) {
                   entries.push_back(entryFromJson(value.toObject(), normalizedPath));
                 }
               }
             }
             callback(ok, message, normalizedPath, entries);
           });
}

void OpenListClient::getFileInfo(const QString& path, EntryCallback callback) {
  const QString normalizedPath = normalizeRemotePath(path);
  QJsonObject payload;
  payload.insert("path", normalizedPath);
  payload.insert("password", "");

  postJson("/api/fs/get", payload,
           [callback, normalizedPath](bool ok, const QString& message,
                                      const QJsonObject& data) {
             RemoteEntry entry;
             if (ok) {
               entry = entryFromJson(data, parentRemotePath(normalizedPath));
               entry.path = normalizedPath;
               entry.rawUrl = data.value("raw_url").toString();
             }
             callback(ok, message, entry);
           });
}

void OpenListClient::downloadUrl(const QUrl& url, DownloadCallback callback) {
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader, "OpenListSorter/1.0");
  const QUrl apiBase(baseUrl_);
  if (!token_.isEmpty() && url.host() == apiBase.host() &&
      url.scheme() == apiBase.scheme()) {
    request.setRawHeader("Authorization", token_.toUtf8());
  }

  QNetworkReply* reply = network_.get(request);
  QObject::connect(reply, &QNetworkReply::finished, this,
                   [reply, callback]() {
                     const QByteArray payload = reply->readAll();
                     const bool ok =
                         reply->error() == QNetworkReply::NoError;
                     const QString message =
                         ok ? "success" : reply->errorString();
                     reply->deleteLater();
                     callback(ok, message, payload);
                   });
}

void OpenListClient::uploadFile(const QString& remotePath,
                                const QByteArray& data,
                                bool overwrite,
                                SimpleCallback callback) {
  const QString normalizedPath = normalizeRemotePath(remotePath);
  QNetworkRequest request = makeRequest("/api/fs/put");
  request.setHeader(QNetworkRequest::ContentTypeHeader,
                    "application/octet-stream");
  request.setRawHeader("File-Path",
                       QUrl::toPercentEncoding(normalizedPath));
  request.setRawHeader("As-Task", "false");
  request.setRawHeader("Overwrite", overwrite ? "true" : "false");

  QNetworkReply* reply = network_.put(request, data);
  QObject::connect(reply, &QNetworkReply::finished, this,
                   [reply, callback]() {
                     const QByteArray payload = reply->readAll();
                     if (reply->error() != QNetworkReply::NoError) {
                       const QString message = reply->errorString();
                       reply->deleteLater();
                       callback(false, message);
                       return;
                     }

                     bool ok = false;
                     QString message;
                     QJsonObject dataObject;
                     parseApiResponse(payload, &ok, &message, &dataObject);
                     reply->deleteLater();
                     callback(ok, message);
                   });
}

void OpenListClient::removeFile(const QString& dir,
                                const QString& fileName,
                                SimpleCallback callback) {
  QJsonObject payload;
  payload.insert("dir", normalizeRemotePath(dir));

  QJsonArray names;
  names.append(fileName);
  payload.insert("names", names);

  postJson("/api/fs/remove", payload,
           [callback](bool ok, const QString& message, const QJsonObject& data) {
             QString finalMessage = message;
             const QString dataMessage = data.value("message").toString();
             if (!dataMessage.isEmpty()) {
               finalMessage = dataMessage;
             }
             callback(ok, finalMessage);
           });
}

void OpenListClient::moveFile(const QString& srcDir,
                              const QString& fileName,
                              const QString& dstDir,
                              bool overwrite,
                              SimpleCallback callback) {
  moveOrCopy("/api/fs/move", srcDir, fileName, dstDir, overwrite, callback);
}

void OpenListClient::copyFile(const QString& srcDir,
                              const QString& fileName,
                              const QString& dstDir,
                              bool overwrite,
                              SimpleCallback callback) {
  moveOrCopy("/api/fs/copy", srcDir, fileName, dstDir, overwrite, callback);
}

QNetworkRequest OpenListClient::makeRequest(const QString& endpoint) const {
  QNetworkRequest request(QUrl(baseUrl_ + endpoint));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setHeader(QNetworkRequest::UserAgentHeader, "OpenListSorter/1.0");
  if (!token_.isEmpty()) {
    request.setRawHeader("Authorization", token_.toUtf8());
  }
  return request;
}

void OpenListClient::postJson(const QString& endpoint,
                              const QJsonObject& payload,
                              JsonCallback callback) {
  if (baseUrl_.isEmpty()) {
    callback(false, "OpenList 地址为空", {});
    return;
  }

  QNetworkReply* reply =
      network_.post(makeRequest(endpoint),
                    QJsonDocument(payload).toJson(QJsonDocument::Compact));
  QObject::connect(reply, &QNetworkReply::finished, this,
                   [reply, callback]() {
                     const QByteArray payload = reply->readAll();
                     if (reply->error() != QNetworkReply::NoError) {
                       const QString message = reply->errorString();
                       reply->deleteLater();
                       callback(false, message, {});
                       return;
                     }

                     bool ok = false;
                     QString message;
                     QJsonObject data;
                     parseApiResponse(payload, &ok, &message, &data);
                     reply->deleteLater();
                     callback(ok, message, data);
                   });
}

void OpenListClient::moveOrCopy(const QString& endpoint,
                                const QString& srcDir,
                                const QString& fileName,
                                const QString& dstDir,
                                bool overwrite,
                                SimpleCallback callback) {
  QJsonObject payload;
  payload.insert("src_dir", normalizeRemotePath(srcDir));
  payload.insert("dst_dir", normalizeRemotePath(dstDir));
  payload.insert("overwrite", overwrite);
  payload.insert("skip_existing", false);
  payload.insert("merge", false);

  QJsonArray names;
  names.append(fileName);
  payload.insert("names", names);

  postJson(endpoint, payload,
           [callback](bool ok, const QString& message, const QJsonObject& data) {
             QString finalMessage = message;
             const QString dataMessage = data.value("message").toString();
             if (!dataMessage.isEmpty()) {
               finalMessage = dataMessage;
             }
             callback(ok, finalMessage);
           });
}

RemoteEntry OpenListClient::entryFromJson(const QJsonObject& object,
                                          const QString& parentPath) {
  RemoteEntry entry;
  entry.name = object.value("name").toString();
  entry.path = joinRemotePath(parentPath, entry.name);
  entry.size = static_cast<qint64>(object.value("size").toDouble());
  entry.isDir = object.value("is_dir").toBool();
  entry.type = object.value("type").toInt();
  entry.modified = parseDateTime(object.value("modified"));
  entry.created = parseDateTime(object.value("created"));
  entry.thumb = object.value("thumb").toString();
  entry.rawUrl = object.value("raw_url").toString();
  return entry;
}

QDateTime OpenListClient::parseDateTime(const QJsonValue& value) {
  if (!value.isString()) {
    return {};
  }
  QString text = value.toString();
  QDateTime parsed = QDateTime::fromString(text, Qt::ISODateWithMs);
  if (!parsed.isValid()) {
    parsed = QDateTime::fromString(text, Qt::ISODate);
  }
  return parsed;
}
