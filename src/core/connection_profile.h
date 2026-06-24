#ifndef OPENLIST_SORTER_CONNECTION_PROFILE_H
#define OPENLIST_SORTER_CONNECTION_PROFILE_H

#include <QString>

enum class AuthMode {
  Token,
  Password,
};

struct ConnectionProfile {
  QString endpoint;
  QString token;
  QString username;
  QString password;
  AuthMode authMode{AuthMode::Token};
  bool rememberMe{};
  bool autoLogin{};

  bool hasCredentials() const {
    if (endpoint.trimmed().isEmpty()) {
      return false;
    }
    if (authMode == AuthMode::Token) {
      return !token.trimmed().isEmpty();
    }
    return !username.trimmed().isEmpty() && !password.isEmpty();
  }
};

class ConnectionSettingsStore {
 public:
  static ConnectionProfile load();
  static void save(const ConnectionProfile& profile);
  static void clearRememberedConnection();
};

#endif

