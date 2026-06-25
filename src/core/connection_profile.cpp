#include "core/connection_profile.h"

#include <QSettings>

namespace {

QString authModeToString(AuthMode mode) {
  return mode == AuthMode::Password ? "password" : "token";
}

AuthMode authModeFromString(const QString& value) {
  return value == "password" ? AuthMode::Password : AuthMode::Token;
}

}  // namespace

ConnectionProfile ConnectionSettingsStore::load() {
  QSettings settings;
  ConnectionProfile profile;
  profile.endpoint = settings.value("connection/endpoint").toString();
  profile.token = settings.value("connection/token").toString();
  profile.username = settings.value("connection/username").toString();
  profile.password = settings.value("connection/password").toString();
  profile.authMode =
      authModeFromString(settings.value("connection/authMode").toString());
  profile.rememberMe = settings.value("connection/rememberMe", false).toBool();
  profile.autoLogin = settings.value("connection/autoLogin", false).toBool();
  if (!profile.rememberMe) {
    profile.autoLogin = false;
    profile.token.clear();
    profile.username.clear();
    profile.password.clear();
  }
  if (profile.autoLogin && !profile.hasCredentials()) {
    profile.autoLogin = false;
  }
  return profile;
}

void ConnectionSettingsStore::save(const ConnectionProfile& profile) {
  QSettings settings;
  if (!profile.rememberMe) {
    clearRememberedConnection();
    settings.setValue("connection/rememberMe", false);
    settings.setValue("connection/autoLogin", false);
    return;
  }

  settings.setValue("connection/endpoint", profile.endpoint.trimmed());
  settings.setValue("connection/authMode", authModeToString(profile.authMode));
  settings.setValue("connection/token", profile.token.trimmed());
  settings.setValue("connection/username", profile.username.trimmed());
  settings.setValue("connection/password", profile.password);
  settings.setValue("connection/rememberMe", true);
  settings.setValue("connection/autoLogin",
                    profile.autoLogin && profile.hasCredentials());
}

void ConnectionSettingsStore::disableAutoLogin() {
  QSettings settings;
  settings.setValue("connection/autoLogin", false);
}

void ConnectionSettingsStore::clearRememberedConnection() {
  QSettings settings;
  settings.remove("connection/endpoint");
  settings.remove("connection/authMode");
  settings.remove("connection/token");
  settings.remove("connection/username");
  settings.remove("connection/password");
  settings.remove("connection/rememberMe");
  settings.remove("connection/autoLogin");
}
