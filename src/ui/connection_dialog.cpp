#include "ui/connection_dialog.h"

#include <QButtonGroup>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSslSocket>
#include <QTimer>
#include <QVBoxLayout>

ConnectionDialog::ConnectionDialog(OpenListClient* client, QWidget* parent)
    : QDialog(parent), client_(client) {
  buildUi();
  if (!parent) {
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                   Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint);
  }
  connectSignals();
  loadCachedProfile();
  const ConnectionProfile cached = profile();
  if (cached.autoLogin && cached.hasCredentials()) {
    QTimer::singleShot(0, this, &ConnectionDialog::startConnection);
  }
}

ConnectionProfile ConnectionDialog::profile() const {
  ConnectionProfile profile;
  profile.endpoint = endpointEdit_->text().trimmed();
  profile.authMode =
      passwordModeRadio_->isChecked() ? AuthMode::Password : AuthMode::Token;
  profile.token = tokenEdit_->text().trimmed();
  profile.username = usernameEdit_->text().trimmed();
  profile.password = passwordEdit_->text();
  profile.rememberMe = rememberCheck_->isChecked();
  profile.autoLogin = autoLoginCheck_->isChecked() && profile.rememberMe;
  return profile;
}

void ConnectionDialog::buildUi() {
  setWindowTitle("连接 OpenList");
  setMinimumSize(480, 420);
  setModal(true);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(24, 24, 24, 24);
  root->setSpacing(16);

  auto* title = new QLabel("连接到 OpenList");
  title->setObjectName("dialogTitle");
  auto* subtitle = new QLabel("使用 endpoint + token key，或使用账号密码登录。");
  subtitle->setObjectName("subtleLabel");
  subtitle->setWordWrap(true);
  root->addWidget(title);
  root->addWidget(subtitle);

  auto* form = new QFormLayout();
  endpointEdit_ = new QLineEdit();
  endpointEdit_->setPlaceholderText("https://openlist.example.com");
  form->addRow("Endpoint", endpointEdit_);
  root->addLayout(form);

  tokenModeRadio_ = new QRadioButton("Token key");
  passwordModeRadio_ = new QRadioButton("账号密码");
  auto* modeGroup = new QButtonGroup(this);
  modeGroup->addButton(tokenModeRadio_);
  modeGroup->addButton(passwordModeRadio_);
  tokenModeRadio_->setChecked(true);

  auto* modeRow = new QHBoxLayout();
  modeRow->addWidget(tokenModeRadio_);
  modeRow->addWidget(passwordModeRadio_);
  modeRow->addStretch(1);
  root->addLayout(modeRow);

  authStack_ = new QStackedWidget();
  auto* tokenPage = new QWidget();
  auto* tokenForm = new QFormLayout(tokenPage);
  tokenEdit_ = new QLineEdit();
  tokenEdit_->setEchoMode(QLineEdit::Password);
  tokenEdit_->setPlaceholderText("OpenList token");
  tokenForm->addRow("Token key", tokenEdit_);

  auto* passwordPage = new QWidget();
  auto* passwordForm = new QFormLayout(passwordPage);
  usernameEdit_ = new QLineEdit();
  usernameEdit_->setPlaceholderText("用户名");
  passwordEdit_ = new QLineEdit();
  passwordEdit_->setEchoMode(QLineEdit::Password);
  passwordEdit_->setPlaceholderText("密码");
  passwordForm->addRow("用户名", usernameEdit_);
  passwordForm->addRow("密码", passwordEdit_);

  authStack_->addWidget(tokenPage);
  authStack_->addWidget(passwordPage);
  root->addWidget(authStack_);

  rememberCheck_ = new QCheckBox("记住我");
  autoLoginCheck_ = new QCheckBox("自动登录");
  auto* checkRow = new QHBoxLayout();
  checkRow->addWidget(rememberCheck_);
  checkRow->addWidget(autoLoginCheck_);
  checkRow->addStretch(1);
  root->addLayout(checkRow);

  statusLabel_ = new QLabel(" ");
  statusLabel_->setObjectName("subtleLabel");
  statusLabel_->setWordWrap(true);
  root->addWidget(statusLabel_);

  auto* buttonRow = new QHBoxLayout();
  buttonRow->addStretch(1);
  cancelButton_ = new QPushButton("取消");
  connectButton_ = new QPushButton("连接");
  connectButton_->setObjectName("primaryButton");
  buttonRow->addWidget(cancelButton_);
  buttonRow->addWidget(connectButton_);
  root->addLayout(buttonRow);
}

void ConnectionDialog::connectSignals() {
  QObject::connect(tokenModeRadio_, &QRadioButton::toggled, this,
                   &ConnectionDialog::updateAuthMode);
  QObject::connect(passwordModeRadio_, &QRadioButton::toggled, this,
                   &ConnectionDialog::updateAuthMode);
  QObject::connect(rememberCheck_, &QCheckBox::toggled, this,
                   &ConnectionDialog::updateRememberLogic);
  QObject::connect(autoLoginCheck_, &QCheckBox::toggled, this,
                   &ConnectionDialog::updateRememberLogic);
  QObject::connect(connectButton_, &QPushButton::clicked, this,
                   &ConnectionDialog::startConnection);
  QObject::connect(cancelButton_, &QPushButton::clicked, this,
                   &QDialog::reject);
}

void ConnectionDialog::loadCachedProfile() {
  const ConnectionProfile cached = ConnectionSettingsStore::load();
  endpointEdit_->setText(cached.endpoint);
  tokenEdit_->setText(cached.token);
  usernameEdit_->setText(cached.username);
  passwordEdit_->setText(cached.password);
  passwordModeRadio_->setChecked(cached.authMode == AuthMode::Password);
  tokenModeRadio_->setChecked(cached.authMode != AuthMode::Password);
  rememberCheck_->setChecked(cached.rememberMe);
  autoLoginCheck_->setChecked(cached.autoLogin && cached.rememberMe);
  updateAuthMode();
  updateRememberLogic();
}

void ConnectionDialog::updateAuthMode() {
  authStack_->setCurrentIndex(passwordModeRadio_->isChecked() ? 1 : 0);
}

void ConnectionDialog::updateRememberLogic() {
  QObject* changedWidget = sender();
  if (changedWidget == rememberCheck_ && !rememberCheck_->isChecked()) {
    autoLoginCheck_->setChecked(false);
    autoLoginCheck_->setEnabled(false);
    return;
  }
  if (changedWidget == autoLoginCheck_ && autoLoginCheck_->isChecked() &&
      !rememberCheck_->isChecked()) {
    rememberCheck_->setChecked(true);
  }
  autoLoginCheck_->setEnabled(rememberCheck_->isChecked());
}

void ConnectionDialog::startConnection() {
  if (!client_) {
    finishConnection(false, "内部错误: OpenList 客户端不存在。");
    return;
  }

  currentProfile_ = profile();
  if (currentProfile_.endpoint.isEmpty()) {
    finishConnection(false, "请输入 endpoint。");
    return;
  }
  if (!currentProfile_.hasCredentials()) {
    finishConnection(false, "请输入 token key，或完整的用户名和密码。");
    return;
  }

  connectButton_->setEnabled(false);
  cancelButton_->setEnabled(false);
  statusLabel_->setText("正在连接...");

  auto callback = [this](bool ok, const QString& message) {
    finishConnection(ok, message);
  };

  if (currentProfile_.authMode == AuthMode::Token) {
    client_->validateToken(currentProfile_.endpoint, currentProfile_.token,
                           callback);
  } else {
    client_->login(currentProfile_.endpoint, currentProfile_.username,
                   currentProfile_.password, callback);
  }
}

void ConnectionDialog::finishConnection(bool ok, const QString& message) {
  connectButton_->setEnabled(true);
  cancelButton_->setEnabled(true);
  if (!ok) {
    QString detail = "连接失败: " + message;
    if (message.contains("TLS", Qt::CaseInsensitive) ||
        message.contains("SSL", Qt::CaseInsensitive)) {
      detail += "\nQt SSL: ";
      detail += QSslSocket::supportsSsl() ? "可用" : "不可用";
      detail += "\nQt 编译时 SSL: " +
                QSslSocket::sslLibraryBuildVersionString();
      detail += "\n当前加载 SSL: " + QSslSocket::sslLibraryVersionString();
    }
    statusLabel_->setText(detail);
    return;
  }

  if (currentProfile_.rememberMe) {
    ConnectionSettingsStore::save(currentProfile_);
  } else {
    ConnectionSettingsStore::clearRememberedConnection();
  }
  statusLabel_->setText("连接成功");
  accept();
}
