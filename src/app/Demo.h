#pragma once

#include <QDialog>
#include <QList>
#include <QObject>
#include <QPointer>

#include <functional>

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
    struct Step
    {
        int delayMs = 0;
        std::function<void()> action;
    };

    void takeScreenshots(const QString& folder);
    void runNextStep();

    Session* m_session = nullptr;
    VoiceController* m_voice = nullptr;
    QPointer<MainWindow> m_main;
    QPointer<QDialog> m_editor;
    QPointer<QDialog> m_settings;
    QList<Step> m_steps;
};
