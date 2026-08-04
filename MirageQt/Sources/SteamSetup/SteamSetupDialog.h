#pragma once

#include "Services/SteamCMDManager.h"

#include <QDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>

namespace Mirage {

class SteamSetupDialog : public QDialog {
    Q_OBJECT

public:
    explicit SteamSetupDialog(SteamCMDManager* steamCMD, QWidget* parent = nullptr);

private:
    void setStep(int step);
    void updateLoginState(SteamLoginState state, const QString& message);
    void appendLog(const QString& line);

    SteamCMDManager* m_steamCMD = nullptr;
    QStackedWidget* m_steps = nullptr;
    QLabel* m_stepIndicator = nullptr;
    QPushButton* m_previous = nullptr;
    QPushButton* m_next = nullptr;
    QProgressBar* m_progress = nullptr;
    QLineEdit* m_username = nullptr;
    QLineEdit* m_password = nullptr;
    QLineEdit* m_guard = nullptr;
    QLabel* m_loginError = nullptr;
    QLabel* m_mobileStatus = nullptr;
    QLabel* m_successAccount = nullptr;
    QStackedWidget* m_loginStates = nullptr;
    QPlainTextEdit* m_log = nullptr;
    int m_step = 0;
};

} // namespace Mirage
