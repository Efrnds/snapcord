#pragma once

#include <QFrame>
#include <QList>

class QLabel;
class QTimer;
class VoiceController;

// Line chart of recent voice server pings.
class PingGraph : public QWidget
{
    Q_OBJECT

public:
    explicit PingGraph(QWidget* parent = nullptr);
    void setSamples(const QList<int>& samples);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QList<int> m_samples;
};

// Popup opened from the "Voice Connected" panel: ping graph, packet loss and encryption details.
class ConnectionInfoPopup : public QFrame
{
    Q_OBJECT

public:
    explicit ConnectionInfoPopup(VoiceController* voice, QWidget* parent = nullptr);

    // Opens the popup above `anchor`.
    void showAbove(QWidget* anchor);

private:
    void refresh();

    VoiceController* m_voice;
    PingGraph* m_graph;
    QLabel* m_averagePing;
    QLabel* m_lastPing;
    QLabel* m_packetLoss;
    QLabel* m_encryption;
    QLabel* m_endToEnd;
    QLabel* m_server;
    QTimer* m_timer;
};
