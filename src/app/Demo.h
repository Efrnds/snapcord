#pragma once

#include <QObject>
#include <QPointer>

class MainWindow;
class Session;
class VoiceController;

// Demo mode (`Snapcord --demo`): the real interface filled with made-up servers, people and messages, without
// logging in or touching the network. Used for screenshots; `--screenshots <folder>` saves them and quits.
class DemoController : public QObject
{
    Q_OBJECT

public:
    explicit DemoController(QObject* parent = nullptr);
    ~DemoController() override;

    void start(const QString& screenshotFolder);

private:
    void takeScreenshots(const QString& folder);

    Session* m_session = nullptr;
    VoiceController* m_voice = nullptr;
    QPointer<MainWindow> m_main;
};
