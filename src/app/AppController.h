#pragma once

#include <QObject>
#include <QPointer>

class LoginWindow;
class MainWindow;
class Session;
class VoiceController;

// Switches between the login screen and the main window, and owns the logged-in session.
class AppController : public QObject
{
    Q_OBJECT

public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    void start();

private:
    void showLogin();
    void showMain(const QString& token);
    void logout();
    void closeSession();

    QPointer<LoginWindow> m_login;
    QPointer<MainWindow> m_main;
    Session* m_session = nullptr;
    VoiceController* m_voice = nullptr;
};
