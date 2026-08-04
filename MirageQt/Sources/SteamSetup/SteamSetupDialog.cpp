#include "SteamSetup/SteamSetupDialog.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFont>
#include <QFrame>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace Mirage {
namespace {

QLabel* iconLabel(const QString& iconName, int size, QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setAlignment(Qt::AlignCenter);
    label->setFixedSize(size, size);
    const QIcon icon = QIcon::fromTheme(iconName);
    if (icon.isNull()) {
        label->setText(QStringLiteral("●"));
        QFont font = label->font();
        font.setPointSize(qMax(18, size / 2));
        label->setFont(font);
    } else {
        label->setPixmap(icon.pixmap(size, size));
    }
    return label;
}

QLabel* textLabel(const QString& text, const QString& objectName, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(objectName);
    label->setAlignment(Qt::AlignHCenter);
    label->setWordWrap(true);
    return label;
}

QFrame* separator(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

} // namespace

SteamSetupDialog::SteamSetupDialog(SteamCMDManager* steamCMD, QWidget* parent)
    : QDialog(parent)
    , m_steamCMD(steamCMD) {
    setWindowTitle(QStringLiteral("Steam 创意工坊设置"));
    setModal(true);
    resize(520, 560);
    setMinimumSize(480, 520);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 16);
    root->setSpacing(12);

    m_stepIndicator = new QLabel(this);
    m_stepIndicator->setObjectName(QStringLiteral("steamSetupSteps"));
    m_stepIndicator->setAlignment(Qt::AlignCenter);
    root->addWidget(m_stepIndicator);
    root->addWidget(separator(this));

    m_steps = new QStackedWidget(this);
    root->addWidget(m_steps, 1);

    auto* welcome = new QWidget(m_steps);
    auto* welcomeLayout = new QVBoxLayout(welcome);
    welcomeLayout->setContentsMargins(28, 12, 28, 12);
    welcomeLayout->setSpacing(12);
    welcomeLayout->addStretch();
    auto* welcomeIcon = iconLabel(QStringLiteral("cloud-download"), 52, welcome);
    welcomeLayout->addWidget(welcomeIcon, 0, Qt::AlignHCenter);
    welcomeLayout->addWidget(textLabel(QStringLiteral("设置 Steam 创意工坊"), QStringLiteral("steamSetupTitle"), welcome));
    welcomeLayout->addWidget(textLabel(QStringLiteral("连接 Steam 创意工坊，浏览并下载 Wallpaper Engine 壁纸。"),
                                       QStringLiteral("steamSetupSubtitle"), welcome));
    auto* ownership = new QFrame(welcome);
    ownership->setObjectName(QStringLiteral("steamSetupNotice"));
    auto* ownershipLayout = new QHBoxLayout(ownership);
    ownershipLayout->setContentsMargins(10, 8, 10, 8);
    ownershipLayout->addWidget(iconLabel(QStringLiteral("dialog-warning"), 20, ownership), 0, Qt::AlignTop);
    auto* ownershipText = new QLabel(QStringLiteral("需要在 Steam 上拥有 Wallpaper Engine 才能下载创意工坊壁纸。"), ownership);
    ownershipText->setWordWrap(true);
    ownershipText->setProperty("secondary", true);
    ownershipLayout->addWidget(ownershipText, 1);
    welcomeLayout->addWidget(ownership);
    auto* network = new QLabel(
        QStringLiteral("中国大陆网络说明：此功能依赖全球 Steam 的登录服务和内容 CDN。蒸汽平台兼容性不保证，Mirage 不承诺任何网络线路能解决登录或下载问题。"),
        welcome);
    network->setObjectName(QStringLiteral("steamSetupInfo"));
    network->setWordWrap(true);
    welcomeLayout->addWidget(network);
    welcomeLayout->addStretch();
    m_steps->addWidget(welcome);

    auto* commandPage = new QWidget(m_steps);
    auto* commandLayout = new QVBoxLayout(commandPage);
    commandLayout->setContentsMargins(40, 18, 40, 18);
    commandLayout->setSpacing(12);
    commandLayout->addStretch();
    commandLayout->addWidget(iconLabel(QStringLiteral("utilities-terminal"), 48, commandPage), 0, Qt::AlignHCenter);
    commandLayout->addWidget(textLabel(QStringLiteral("准备 SteamCMD"), QStringLiteral("steamSetupTitle"), commandPage));
    commandLayout->addWidget(textLabel(QStringLiteral("SteamCMD 是 Valve 提供的官方命令行工具，用于安全登录并下载创意工坊内容。"),
                                       QStringLiteral("steamSetupSubtitle"), commandPage));
    auto* commandActions = new QHBoxLayout;
    auto* detect = new QPushButton(QIcon::fromTheme(QStringLiteral("system-search")), QStringLiteral("检测 SteamCMD"), commandPage);
    auto* install = new QPushButton(QIcon::fromTheme(QStringLiteral("go-down")), QStringLiteral("安装 SteamCMD"), commandPage);
    install->setProperty("accent", true);
    auto* cancelInstall = new QToolButton(commandPage);
    cancelInstall->setIcon(QIcon::fromTheme(QStringLiteral("process-stop")));
    cancelInstall->setToolTip(QStringLiteral("取消 SteamCMD 安装"));
    commandActions->addWidget(detect);
    commandActions->addWidget(install);
    commandActions->addWidget(cancelInstall);
    commandLayout->addLayout(commandActions);
    m_progress = new QProgressBar(commandPage);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    commandLayout->addWidget(m_progress);
    auto* commandStatus = new QLabel(commandPage);
    commandStatus->setObjectName(QStringLiteral("steamSetupInfo"));
    commandStatus->setAlignment(Qt::AlignCenter);
    commandStatus->setWordWrap(true);
    commandLayout->addWidget(commandStatus);
    commandLayout->addStretch();
    m_steps->addWidget(commandPage);

    auto* loginPage = new QWidget(m_steps);
    auto* loginLayout = new QVBoxLayout(loginPage);
    loginLayout->setContentsMargins(40, 12, 40, 12);
    loginLayout->setSpacing(10);
    loginLayout->addWidget(iconLabel(QStringLiteral("user-identity"), 48, loginPage), 0, Qt::AlignHCenter);
    loginLayout->addWidget(textLabel(QStringLiteral("登录 Steam 账号"), QStringLiteral("steamSetupTitle"), loginPage));
    loginLayout->addWidget(textLabel(QStringLiteral("需要一个拥有 Wallpaper Engine 的全球 Steam 账号来下载创意工坊内容。"),
                                     QStringLiteral("steamSetupSubtitle"), loginPage));

    auto* loginNotice = new QFrame(loginPage);
    loginNotice->setObjectName(QStringLiteral("steamSetupNotice"));
    auto* loginNoticeLayout = new QVBoxLayout(loginNotice);
    loginNoticeLayout->setContentsMargins(10, 8, 10, 8);
    auto* noticeTitle = new QLabel(QStringLiteral("Mirage 并非 Steam 官方客户端。"), loginNotice);
    noticeTitle->setProperty("warning", true);
    loginNoticeLayout->addWidget(noticeTitle);
    auto* noticeText = new QLabel(
        QStringLiteral("密码仅通过本机 Valve SteamCMD 的标准输入提交，不会写入命令行或 Mirage 日志。SteamCMD 会在本机保存会话，您可随时退出登录并清除它。"),
        loginNotice);
    noticeText->setProperty("secondary", true);
    noticeText->setWordWrap(true);
    loginNoticeLayout->addWidget(noticeText);
    loginLayout->addWidget(loginNotice);

    m_loginStates = new QStackedWidget(loginPage);
    loginLayout->addWidget(m_loginStates);

    auto* formState = new QWidget(m_loginStates);
    auto* formLayout = new QVBoxLayout(formState);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(8);
    if (m_steamCMD->isLoggedIn() && !m_steamCMD->savedUsername().isEmpty()) {
        auto* reuse = new QPushButton(QIcon::fromTheme(QStringLiteral("emblem-default")),
                                      QStringLiteral("使用已保存会话：%1").arg(m_steamCMD->savedUsername()), formState);
        reuse->setProperty("accent", true);
        reuse->setMaximumWidth(300);
        formLayout->addWidget(reuse, 0, Qt::AlignHCenter);
        connect(reuse, &QPushButton::clicked, this, [this] {
            m_username->setText(m_steamCMD->savedUsername());
            m_password->clear();
            updateLoginState(SteamLoginState::Success, QStringLiteral("已使用验证有效的本机 SteamCMD 会话"));
        });
        auto* divider = new QLabel(QStringLiteral("或使用密码登录"), formState);
        divider->setObjectName(QStringLiteral("steamSetupDivider"));
        divider->setAlignment(Qt::AlignCenter);
        divider->setMaximumWidth(300);
        formLayout->addWidget(divider);
    }
    m_username = new QLineEdit(formState);
    m_username->setText(m_steamCMD->savedUsername());
    m_username->setPlaceholderText(QStringLiteral("全球 Steam 登录账户名（非昵称）"));
    m_username->setMinimumWidth(260);
    m_username->setMaximumWidth(300);
    m_password = new QLineEdit(formState);
    m_password->setPlaceholderText(QStringLiteral("密码"));
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setFixedWidth(300);
    auto* usernameRow = new QHBoxLayout;
    usernameRow->setContentsMargins(0, 0, 0, 0);
    usernameRow->setSpacing(7);
    usernameRow->addWidget(iconLabel(QStringLiteral("user-identity"), 20, formState));
    usernameRow->addWidget(m_username);
    auto* passwordRow = new QHBoxLayout;
    passwordRow->setContentsMargins(0, 0, 0, 0);
    passwordRow->setSpacing(7);
    passwordRow->addWidget(iconLabel(QStringLiteral("object-unlocked"), 20, formState));
    passwordRow->addWidget(m_password);
    auto* revealPassword = m_password->addAction(QIcon::fromTheme(QStringLiteral("view-visible")), QLineEdit::TrailingPosition);
    revealPassword->setCheckable(true);
    revealPassword->setToolTip(QStringLiteral("显示或隐藏密码"));
    auto* login = new QPushButton(QIcon::fromTheme(QStringLiteral("go-next")), QStringLiteral("登录"), formState);
    login->setProperty("accent", true);
    login->setMaximumWidth(300);
    formLayout->addLayout(usernameRow);
    formLayout->addLayout(passwordRow);
    formLayout->addWidget(login, 0, Qt::AlignHCenter);
    formLayout->setAlignment(Qt::AlignHCenter);
    m_loginStates->addWidget(formState);

    auto* loggingState = new QWidget(m_loginStates);
    auto* loggingLayout = new QVBoxLayout(loggingState);
    loggingLayout->setContentsMargins(0, 8, 0, 8);
    loggingLayout->setSpacing(10);
    auto* loginSpinner = new QProgressBar(loggingState);
    loginSpinner->setObjectName(QStringLiteral("steamLoginSpinner"));
    loginSpinner->setRange(0, 0);
    loginSpinner->setFixedSize(96, 8);
    loginSpinner->setTextVisible(false);
    loggingLayout->addWidget(loginSpinner, 0, Qt::AlignHCenter);
    loggingLayout->addWidget(textLabel(QStringLiteral("正在登录..."), QStringLiteral("steamSetupSubtitle"), loggingState));
    auto* cancelLogging = new QPushButton(QStringLiteral("取消登录"), loggingState);
    cancelLogging->setMaximumWidth(140);
    loggingLayout->addWidget(cancelLogging, 0, Qt::AlignHCenter);
    loggingLayout->setAlignment(Qt::AlignHCenter);
    m_loginStates->addWidget(loggingState);

    auto* mobileState = new QWidget(m_loginStates);
    auto* mobileLayout = new QVBoxLayout(mobileState);
    mobileLayout->setContentsMargins(0, 8, 0, 8);
    mobileLayout->setSpacing(9);
    mobileLayout->addWidget(iconLabel(QStringLiteral("smartphone"), 42, mobileState), 0, Qt::AlignHCenter);
    mobileLayout->addWidget(textLabel(QStringLiteral("请在手机上确认登录"), QStringLiteral("steamSetupStateTitle"), mobileState));
    m_mobileStatus = textLabel(QStringLiteral("打开 Steam 手机应用，在通知中确认此次登录请求。"), QStringLiteral("steamSetupSubtitle"), mobileState);
    mobileLayout->addWidget(m_mobileStatus);
    auto* cancelMobile = new QPushButton(QStringLiteral("取消登录"), mobileState);
    mobileLayout->addWidget(cancelMobile, 0, Qt::AlignHCenter);
    m_loginStates->addWidget(mobileState);

    auto* guardState = new QWidget(m_loginStates);
    auto* guardLayout = new QVBoxLayout(guardState);
    guardLayout->setContentsMargins(0, 8, 0, 8);
    guardLayout->setSpacing(9);
    guardLayout->addWidget(iconLabel(QStringLiteral("security-high"), 38, guardState), 0, Qt::AlignHCenter);
    guardLayout->addWidget(textLabel(QStringLiteral("请输入 Steam Guard 验证码"), QStringLiteral("steamSetupStateTitle"), guardState));
    auto* guardDescription = textLabel(QStringLiteral("请查看 Steam 手机令牌或邮箱中的验证码。"), QStringLiteral("steamSetupSubtitle"), guardState);
    guardLayout->addWidget(guardDescription);
    m_guard = new QLineEdit(guardState);
    m_guard->setPlaceholderText(QStringLiteral("验证码"));
    m_guard->setMaxLength(12);
    m_guard->setMaximumWidth(180);
    guardLayout->addWidget(m_guard, 0, Qt::AlignHCenter);
    auto* verify = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-ok")), QStringLiteral("验证"), guardState);
    verify->setProperty("accent", true);
    guardLayout->addWidget(verify, 0, Qt::AlignHCenter);
    auto* cancelGuard = new QPushButton(QStringLiteral("取消登录"), guardState);
    guardLayout->addWidget(cancelGuard, 0, Qt::AlignHCenter);
    m_loginStates->addWidget(guardState);

    auto* successState = new QWidget(m_loginStates);
    auto* successLayout = new QVBoxLayout(successState);
    successLayout->setContentsMargins(0, 10, 0, 10);
    successLayout->setSpacing(8);
    successLayout->addWidget(iconLabel(QStringLiteral("emblem-default"), 46, successState), 0, Qt::AlignHCenter);
    successLayout->addWidget(textLabel(QStringLiteral("登录成功"), QStringLiteral("steamSetupStateTitle"), successState));
    m_successAccount = textLabel(QString(), QStringLiteral("steamSetupSuccess"), successState);
    successLayout->addWidget(m_successAccount);
    m_loginStates->addWidget(successState);

    m_loginError = new QLabel(loginPage);
    m_loginError->setProperty("error", true);
    m_loginError->setAlignment(Qt::AlignHCenter);
    m_loginError->setWordWrap(true);
    m_loginError->hide();
    loginLayout->addWidget(m_loginError);

    auto* logActions = new QHBoxLayout;
    logActions->addStretch();
    auto* showLog = new QToolButton(loginPage);
    showLog->setIcon(QIcon::fromTheme(QStringLiteral("utilities-terminal")));
    showLog->setText(QStringLiteral("显示 SteamCMD 日志"));
    showLog->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    showLog->setProperty("flatButton", true);
    showLog->setCheckable(true);
    showLog->setToolTip(QStringLiteral("显示或隐藏脱敏 SteamCMD 日志"));
    auto* copyLog = new QToolButton(loginPage);
    copyLog->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
    copyLog->setText(QStringLiteral("复制脱敏日志"));
    copyLog->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    copyLog->setProperty("flatButton", true);
    copyLog->setToolTip(QStringLiteral("复制脱敏 SteamCMD 日志"));
    logActions->addWidget(showLog);
    logActions->addWidget(copyLog);
    logActions->addStretch();
    loginLayout->addLayout(logActions);
    m_log = new QPlainTextEdit(loginPage);
    m_log->setObjectName(QStringLiteral("steamSetupLog"));
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);
    m_log->setFixedHeight(108);
    m_log->hide();
    loginLayout->addWidget(m_log);
    m_steps->addWidget(loginPage);

    auto* complete = new QWidget(m_steps);
    auto* completeLayout = new QVBoxLayout(complete);
    completeLayout->setContentsMargins(40, 20, 40, 20);
    completeLayout->setSpacing(12);
    completeLayout->addStretch();
    completeLayout->addWidget(iconLabel(QStringLiteral("emblem-default"), 58, complete), 0, Qt::AlignHCenter);
    completeLayout->addWidget(textLabel(QStringLiteral("设置完成！"), QStringLiteral("steamSetupTitle"), complete));
    completeLayout->addWidget(textLabel(QStringLiteral("Steam 登录已完成。Wallpaper Engine 所有权与项目访问权限将在首次下载时由 Steam 验证。"),
                                        QStringLiteral("steamSetupSubtitle"), complete));
    auto* completeInfo = new QLabel(complete);
    completeInfo->setObjectName(QStringLiteral("steamSetupInfo"));
    completeInfo->setAlignment(Qt::AlignHCenter);
    completeInfo->setWordWrap(true);
    completeLayout->addWidget(completeInfo);
    completeLayout->addStretch();
    m_steps->addWidget(complete);

    root->addWidget(separator(this));
    auto* navigation = new QHBoxLayout;
    m_previous = new QPushButton(QIcon::fromTheme(QStringLiteral("go-previous")), QStringLiteral("上一步"), this);
    m_next = new QPushButton(QIcon::fromTheme(QStringLiteral("go-next")), QStringLiteral("下一步"), this);
    m_next->setProperty("accent", true);
    navigation->addWidget(m_previous);
    navigation->addStretch();
    navigation->addWidget(m_next);
    root->addLayout(navigation);

    connect(detect, &QPushButton::clicked, this, [this, commandStatus] {
        const QString path = m_steamCMD->detectSteamCMD();
        commandStatus->setText(path.isEmpty() ? QStringLiteral("未找到 SteamCMD") : QStringLiteral("已找到：%1").arg(path));
        appendLog(commandStatus->text());
    });
    connect(install, &QPushButton::clicked, m_steamCMD, &SteamCMDManager::installSteamCMD);
    connect(cancelInstall, &QToolButton::clicked, m_steamCMD, &SteamCMDManager::cancelInstallation);
    connect(revealPassword, &QAction::toggled, this, [this, revealPassword](bool visible) {
        m_password->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
        revealPassword->setIcon(QIcon::fromTheme(visible ? QStringLiteral("view-hidden") : QStringLiteral("view-visible")));
    });
    connect(login, &QPushButton::clicked, this, [this] {
        m_loginError->hide();
        m_steamCMD->login(m_username->text(), m_password->text());
    });
    connect(verify, &QPushButton::clicked, this, [this] {
        const QString code = m_guard->text().trimmed();
        if (code.isEmpty()) {
            m_loginError->setText(QStringLiteral("请输入 Steam Guard 验证码"));
            m_loginError->show();
            return;
        }
        m_loginError->hide();
        m_steamCMD->submitGuardCode(code);
        m_guard->clear();
    });
    connect(m_guard, &QLineEdit::returnPressed, verify, &QPushButton::click);
    connect(cancelLogging, &QPushButton::clicked, m_steamCMD, &SteamCMDManager::cancelLogin);
    connect(cancelMobile, &QPushButton::clicked, m_steamCMD, &SteamCMDManager::cancelLogin);
    connect(cancelGuard, &QPushButton::clicked, m_steamCMD, &SteamCMDManager::cancelLogin);
    connect(showLog, &QToolButton::toggled, this, [this, showLog](bool visible) {
        m_log->setVisible(visible);
        showLog->setText(visible ? QStringLiteral("隐藏日志") : QStringLiteral("显示 SteamCMD 日志"));
    });
    connect(copyLog, &QToolButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_log->toPlainText());
    });
    connect(m_previous, &QPushButton::clicked, this, [this] { setStep(m_step - 1); });
    connect(m_next, &QPushButton::clicked, this, [this] {
        if (m_step == 1 && m_steamCMD->steamCMDPath().isEmpty()) {
            appendLog(QStringLiteral("请先检测或安装 SteamCMD"));
            return;
        }
        if (m_step == 2 && !m_steamCMD->isLoggedIn()) {
            m_loginError->setText(QStringLiteral("请先完成 Steam 登录"));
            m_loginError->show();
            return;
        }
        if (m_step == 3) {
            accept();
            return;
        }
        setStep(m_step + 1);
    });
    connect(m_steamCMD, &SteamCMDManager::installStateChanged, this,
            [this, commandStatus](SteamCMDInstallState state, double progress, const QString& message) {
                m_progress->setValue(qRound(progress * 100.0));
                commandStatus->setText(message);
                appendLog(message);
                if (state == SteamCMDInstallState::Installed) setStep(2);
            });
    connect(m_steamCMD, &SteamCMDManager::loginStateChanged, this, &SteamSetupDialog::updateLoginState);
    connect(m_steamCMD, &SteamCMDManager::diagnosticEvent, this, &SteamSetupDialog::appendLog);
    connect(m_steamCMD, &SteamCMDManager::authenticationChanged, this,
            [this, completeInfo](bool loggedIn, const QString&) {
                if (loggedIn) {
                    completeInfo->setText(QStringLiteral("SteamCMD：%1\n账号：%2")
                                              .arg(QFileInfo(m_steamCMD->steamCMDPath()).fileName(), m_steamCMD->savedUsername()));
                }
            });

    if (m_steamCMD->isLoggedIn()) {
        const QString account = m_steamCMD->savedUsername();
        m_successAccount->setText(QStringLiteral("●  %1").arg(account));
        completeInfo->setText(QStringLiteral("SteamCMD：%1\n账号：%2")
                                  .arg(QFileInfo(m_steamCMD->steamCMDPath()).fileName(), account));
    }
    setStep(m_steamCMD->steamCMDPath().isEmpty() ? 0 : (m_steamCMD->isLoggedIn() ? 3 : 2));
}

void SteamSetupDialog::setStep(int step) {
    m_step = qBound(0, step, 3);
    m_steps->setCurrentIndex(m_step);

    const QStringList titles = {QStringLiteral("欢迎"), QStringLiteral("SteamCMD"), QStringLiteral("登录"), QStringLiteral("完成")};
    QStringList labels;
    for (int index = 0; index < 4; ++index) {
        const QString marker = index < m_step ? QStringLiteral("✓") : QString::number(index + 1);
        labels << QStringLiteral("%1  %2").arg(marker, titles.at(index));
    }
    m_stepIndicator->setText(labels.join(QStringLiteral("    ─    ")));
    m_previous->setVisible(m_step > 0);
    m_next->setText(m_step == 3 ? QStringLiteral("完成") : QStringLiteral("下一步"));
    m_next->setIcon(QIcon::fromTheme(m_step == 3 ? QStringLiteral("dialog-ok") : QStringLiteral("go-next")));
}

void SteamSetupDialog::updateLoginState(SteamLoginState state, const QString& message) {
    appendLog(message);
    switch (state) {
    case SteamLoginState::LoggingIn:
        m_loginError->hide();
        m_loginStates->setCurrentIndex(1);
        break;
    case SteamLoginState::WaitingForGuard:
        m_loginError->hide();
        if (message.contains(QStringLiteral("手机"))) {
            m_mobileStatus->setText(QStringLiteral("打开 Steam 手机应用，在通知中确认此次登录请求。"));
            m_loginStates->setCurrentIndex(2);
        } else {
            m_loginStates->setCurrentIndex(3);
            m_guard->setFocus();
        }
        break;
    case SteamLoginState::Success:
        m_password->clear();
        m_guard->clear();
        m_loginError->hide();
        m_successAccount->setText(QStringLiteral("●  %1").arg(m_username->text().trimmed()));
        m_loginStates->setCurrentIndex(4);
        break;
    case SteamLoginState::Failed:
        m_loginStates->setCurrentIndex(0);
        m_loginError->setText(message);
        m_loginError->show();
        break;
    case SteamLoginState::Idle:
        m_loginStates->setCurrentIndex(0);
        break;
    }
}

void SteamSetupDialog::appendLog(const QString& line) {
    if (!line.trimmed().isEmpty()) m_log->appendPlainText(line.trimmed());
}

} // namespace Mirage
