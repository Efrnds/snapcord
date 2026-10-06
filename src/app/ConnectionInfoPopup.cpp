#include "ConnectionInfoPopup.h"

#include "VoiceController.h"

#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <numeric>

PingGraph::PingGraph(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(260, 90);
}

void PingGraph::setSamples(const QList<int>& samples)
{
    m_samples = samples;
    update();
}

void PingGraph::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x1e, 0x1f, 0x22));
    painter.drawRoundedRect(rect(), 4, 4);
    if (m_samples.size() < 2)
        return;

    // Scale to the largest sample, with a 100 ms minimum so a stable connection looks flat.
    const int maximum = std::max(100, *std::max_element(m_samples.begin(), m_samples.end()));
    const QRectF area = QRectF(rect()).adjusted(6, 8, -6, -8);
    QPainterPath line;
    for (qsizetype i = 0; i < m_samples.size(); ++i) {
        const QPointF point(area.left() + area.width() * i / (m_samples.size() - 1),
                            area.bottom() - area.height() * m_samples[i] / maximum);
        if (i == 0)
            line.moveTo(point);
        else
            line.lineTo(point);
    }
    painter.setPen(QPen(QColor(0x23, 0xa5, 0x59), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(line);
}

ConnectionInfoPopup::ConnectionInfoPopup(VoiceController* voice, QWidget* parent)
    : QFrame(parent, Qt::Popup)
    , m_voice(voice)
    , m_graph(new PingGraph)
    , m_averagePing(new QLabel)
    , m_lastPing(new QLabel)
    , m_packetLoss(new QLabel)
    , m_encryption(new QLabel)
    , m_endToEnd(new QLabel)
    , m_server(new QLabel)
    , m_timer(new QTimer(this))
{
    setObjectName(QStringLiteral("connectionInfo"));
    setAttribute(Qt::WA_DeleteOnClose);

    auto* title = new QLabel(tr("Voice Connection"));
    title->setObjectName(QStringLiteral("connectionInfoTitle"));

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->addRow(tr("Average ping"), m_averagePing);
    form->addRow(tr("Last ping"), m_lastPing);
    form->addRow(tr("Inbound packet loss"), m_packetLoss);
    form->addRow(tr("Transport encryption"), m_encryption);
    form->addRow(tr("End-to-end encryption"), m_endToEnd);
    form->addRow(tr("Server"), m_server);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(m_graph);
    layout->addLayout(form);

    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &ConnectionInfoPopup::refresh);
    m_timer->start();
    refresh();
}

void ConnectionInfoPopup::showAbove(QWidget* anchor)
{
    adjustSize();
    const QPoint topLeft = anchor->mapToGlobal(QPoint(0, 0));
    move(topLeft.x() + 8, topLeft.y() - height() - 8);
    show();
}

void ConnectionInfoPopup::refresh()
{
    const QList<int>& pings = m_voice->pingHistory();
    m_graph->setSamples(pings);
    if (pings.isEmpty()) {
        m_averagePing->setText(QStringLiteral("—"));
        m_lastPing->setText(QStringLiteral("—"));
    } else {
        const int average = std::accumulate(pings.begin(), pings.end(), 0) / static_cast<int>(pings.size());
        m_averagePing->setText(tr("%1 ms").arg(average));
        m_lastPing->setText(tr("%1 ms").arg(pings.last()));
    }

    const VoiceConnection::ConnectionInfo info = m_voice->connection()->connectionInfo();
    m_packetLoss->setText(QStringLiteral("%1%").arg(info.inboundPacketLoss, 0, 'f', 1));
    m_encryption->setText(info.encryptionMode.isEmpty() ? QStringLiteral("—") : info.encryptionMode);
    if (info.endToEndEncrypted)
        m_endToEnd->setText(tr("Active (DAVE v%1)").arg(info.daveProtocolVersion));
    else if (info.daveProtocolVersion > 0)
        m_endToEnd->setText(tr("Waiting for other participants"));
    else
        m_endToEnd->setText(tr("Not used in this call"));
    // The endpoint is "host:port"; the host name tells which region serves the call.
    m_server->setText(info.endpoint.section(u':', 0, 0));
}
