#include "LoginWindow.h"

#include "Motion.h"

#include "core/RemoteAuth.h"
#include "core/RestClient.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <qrcodegen.hpp>

namespace {

constexpr int QrSize = 176;

QPixmap renderQrCode(const QString& text, qreal devicePixelRatio)
{
    const auto qr = qrcodegen::QrCode::encodeText(text.toUtf8().constData(), qrcodegen::QrCode::Ecc::LOW);
    const int quietZone = 2;
    const int modules = qr.getSize() + 2 * quietZone;

    QPixmap pixmap(QSize(QrSize, QrSize) * devicePixelRatio);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(QRectF(0, 0, QrSize, QrSize), 6, 6);

    const qreal moduleSize = qreal(QrSize) / modules;
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setBrush(Qt::black);
    for (int y = 0; y < qr.getSize(); ++y) {
        for (int x = 0; x < qr.getSize(); ++x) {
            if (qr.getModule(x, y))
                painter.drawRect(QRectF((x + quietZone) * moduleSize, (y + quietZone) * moduleSize, moduleSize + 0.5,
                                        moduleSize + 0.5));
        }
    }
    return pixmap;
}

} // namespace

LoginWindow::LoginWindow(QWidget* parent)
    : QWidget(parent)
    , m_auth(new RemoteAuth(this))
    , m_qrStack(new QStackedWidget)
    , m_qrImage(new QLabel)
    , m_qrTitle(new QLabel)
    , m_qrHint(new QLabel)
    , m_errorLabel(new QLabel)
    , m_retryButton(new QPushButton(tr("Try Again")))
    , m_leftStack(new QStackedWidget)
    , m_tokenInput(new QLineEdit)
    , m_tokenError(new QLabel)
    , m_tokenButton(new QPushButton(tr("Log In")))
    , m_rest(new RestClient(this))
{
    setObjectName(QStringLiteral("loginWindow"));
    setAttribute(Qt::WA_StyledBackground);
    setWindowTitle(QStringLiteral("Snapcord"));
    resize(1100, 680);
    setMinimumSize(800, 520);
    m_rest->setReferer(QStringLiteral("https://discord.com/login"));

    // Left column: welcome text and the Terms of Service warning.
    auto* title = new QLabel(tr("Welcome to Snapcord"));
    title->setObjectName(QStringLiteral("loginTitle"));
    auto* subtitle = new QLabel(tr("A lightweight Discord client focused on voice."));
    subtitle->setObjectName(QStringLiteral("loginSubtitle"));
    auto* steps = new QLabel(tr("1. Open the Discord app on your phone.\n"
                                "2. Go to Settings and tap \"Scan QR Code\".\n"
                                "3. Point your camera at the code on the right."));
    steps->setObjectName(QStringLiteral("loginSteps"));
    auto* warning = new QLabel(tr("Snapcord is not affiliated with Discord. Using third-party clients goes "
                                  "against Discord's Terms of Service and may get your account suspended. "
                                  "Use it at your own risk."));
    warning->setObjectName(QStringLiteral("loginWarning"));
    warning->setWordWrap(true);

    // Alternative to the QR code (e.g. when Discord asks for a captcha): paste the account token.
    auto* useToken = new QPushButton(tr("Log in with a token instead"));
    useToken->setObjectName(QStringLiteral("linkButton"));
    useToken->setCursor(Qt::PointingHandCursor);

    auto* qrInstructions = new QWidget;
    auto* qrInstructionsLayout = new QVBoxLayout(qrInstructions);
    qrInstructionsLayout->setContentsMargins(0, 0, 0, 0);
    qrInstructionsLayout->setSpacing(8);
    qrInstructionsLayout->addStretch();
    qrInstructionsLayout->addWidget(title);
    qrInstructionsLayout->addWidget(subtitle);
    qrInstructionsLayout->addSpacing(20);
    qrInstructionsLayout->addWidget(steps);
    qrInstructionsLayout->addSpacing(12);
    qrInstructionsLayout->addWidget(useToken, 0, Qt::AlignLeft);
    qrInstructionsLayout->addStretch();

    auto* tokenTitle = new QLabel(tr("Log in with a token"));
    tokenTitle->setObjectName(QStringLiteral("loginTitle"));
    auto* tokenLabel = new QLabel(tr("TOKEN"));
    tokenLabel->setObjectName(QStringLiteral("settingsSection"));
    m_tokenInput->setObjectName(QStringLiteral("tokenInput"));
    m_tokenInput->setEchoMode(QLineEdit::Password);
    m_tokenInput->setPlaceholderText(tr("Paste your Discord token"));
    m_tokenError->setObjectName(QStringLiteral("tokenError"));
    m_tokenError->setWordWrap(true);
    m_tokenError->hide();
    m_tokenButton->setObjectName(QStringLiteral("brandButton"));
    m_tokenButton->setCursor(Qt::PointingHandCursor);
    auto* tokenHint = new QLabel(tr("Never share your token with anyone: it gives full access to your account. "
                                    "Snapcord stores it only in the Windows Credential Manager."));
    tokenHint->setObjectName(QStringLiteral("loginWarning"));
    tokenHint->setWordWrap(true);
    auto* backToQr = new QPushButton(tr("Back to QR code login"));
    backToQr->setObjectName(QStringLiteral("linkButton"));
    backToQr->setCursor(Qt::PointingHandCursor);

    auto* tokenPage = new QWidget;
    auto* tokenLayout = new QVBoxLayout(tokenPage);
    tokenLayout->setContentsMargins(0, 0, 0, 0);
    tokenLayout->setSpacing(8);
    tokenLayout->addStretch();
    tokenLayout->addWidget(tokenTitle);
    tokenLayout->addSpacing(12);
    tokenLayout->addWidget(tokenLabel);
    tokenLayout->addWidget(m_tokenInput);
    tokenLayout->addWidget(m_tokenError);
    tokenLayout->addSpacing(4);
    tokenLayout->addWidget(m_tokenButton);
    tokenLayout->addWidget(tokenHint);
    tokenLayout->addSpacing(4);
    tokenLayout->addWidget(backToQr, 0, Qt::AlignLeft);
    tokenLayout->addStretch();

    m_leftStack->addWidget(qrInstructions);
    m_leftStack->addWidget(tokenPage);

    connect(useToken, &QPushButton::clicked, this, [this] {
        Motion::crossFade(m_leftStack);
        m_leftStack->setCurrentIndex(1);
        m_tokenInput->setFocus();
    });
    connect(backToQr, &QPushButton::clicked, this, [this] {
        Motion::crossFade(m_leftStack);
        m_leftStack->setCurrentIndex(0);
    });
    connect(m_tokenButton, &QPushButton::clicked, this, &LoginWindow::submitToken);
    connect(m_tokenInput, &QLineEdit::returnPressed, this, &LoginWindow::submitToken);

    auto* left = new QVBoxLayout;
    left->setSpacing(8);
    left->addWidget(m_leftStack, 1);
    left->addWidget(warning);

    // Right column: the QR code, or the error state.
    m_qrImage->setFixedSize(QrSize, QrSize);
    m_qrImage->setAlignment(Qt::AlignCenter);
    m_qrTitle->setObjectName(QStringLiteral("qrTitle"));
    m_qrTitle->setAlignment(Qt::AlignCenter);
    m_qrHint->setObjectName(QStringLiteral("qrHint"));
    m_qrHint->setAlignment(Qt::AlignCenter);
    m_qrHint->setWordWrap(true);

    auto* qrPage = new QWidget;
    auto* qrLayout = new QVBoxLayout(qrPage);
    qrLayout->setContentsMargins(0, 0, 0, 0);
    qrLayout->addStretch();
    qrLayout->addWidget(m_qrImage, 0, Qt::AlignHCenter);
    qrLayout->addSpacing(24);
    qrLayout->addWidget(m_qrTitle);
    qrLayout->addWidget(m_qrHint);
    qrLayout->addStretch();

    m_errorLabel->setObjectName(QStringLiteral("qrHint"));
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->setWordWrap(true);
    m_retryButton->setObjectName(QStringLiteral("primaryButton"));
    m_retryButton->setCursor(Qt::PointingHandCursor);
    connect(m_retryButton, &QPushButton::clicked, this, &LoginWindow::restart);
    auto* errorPage = new QWidget;
    auto* errorLayout = new QVBoxLayout(errorPage);
    errorLayout->addStretch();
    errorLayout->addWidget(m_errorLabel);
    errorLayout->addSpacing(16);
    errorLayout->addWidget(m_retryButton, 0, Qt::AlignHCenter);
    errorLayout->addStretch();

    m_qrStack->addWidget(qrPage);
    m_qrStack->addWidget(errorPage);
    m_qrStack->setFixedWidth(260);

    auto* card = new QWidget;
    card->setObjectName(QStringLiteral("loginCard"));
    card->setAttribute(Qt::WA_StyledBackground);
    card->setFixedSize(784, 420);
    auto* cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(32, 32, 32, 32);
    cardLayout->setSpacing(48);
    cardLayout->addLayout(left, 1);
    cardLayout->addWidget(m_qrStack);

    auto* outer = new QVBoxLayout(this);
    outer->addStretch();
    outer->addWidget(card, 0, Qt::AlignCenter);
    outer->addStretch();

    connect(m_auth, &RemoteAuth::qrCodeReady, this, &LoginWindow::showQrCode);
    connect(m_auth, &RemoteAuth::userScanned, this,
            [this](const RemoteAuth::UserPreview& user) { showScanned(user.username); });
    connect(m_auth, &RemoteAuth::failed, this, &LoginWindow::showError);
    connect(m_auth, &RemoteAuth::loggedIn, this, &LoginWindow::loggedIn);

    restart();
}

void LoginWindow::restart()
{
    Motion::crossFade(m_qrStack->parentWidget());
    m_qrStack->setCurrentIndex(0);
    m_qrImage->setPixmap({});
    m_qrImage->setText(tr("Loading…"));
    m_qrTitle->setText(tr("Log in with QR Code"));
    m_qrHint->setText(tr("Scan this with the Discord mobile app to log in instantly."));
    m_auth->start();
}

void LoginWindow::showQrCode(const QString& url)
{
    Motion::crossFade(m_qrStack->parentWidget());
    m_qrStack->setCurrentIndex(0);
    m_qrImage->setPixmap(renderQrCode(url, devicePixelRatioF()));
    m_qrTitle->setText(tr("Log in with QR Code"));
    m_qrHint->setText(tr("Scan this with the Discord mobile app to log in instantly."));
}

void LoginWindow::showScanned(const QString& username)
{
    Motion::crossFade(m_qrStack->parentWidget());
    m_qrTitle->setText(tr("Check your phone!"));
    m_qrHint->setText(tr("Logging in as %1. Confirm the login in the Discord app.").arg(username));
}

void LoginWindow::showError(const QString& message)
{
    Motion::crossFade(m_qrStack->parentWidget());
    m_errorLabel->setText(message);
    m_qrStack->setCurrentIndex(1);
}

void LoginWindow::submitToken()
{
    // Tokens are often copied with surrounding quotes or spaces.
    QString token = m_tokenInput->text().trimmed();
    if (token.size() >= 2 && token.startsWith(u'"') && token.endsWith(u'"'))
        token = token.mid(1, token.size() - 2).trimmed();
    if (token.isEmpty()) {
        showTokenError(tr("Paste your token first."));
        return;
    }

    m_tokenButton->setEnabled(false);
    m_tokenButton->setText(tr("Checking…"));
    m_tokenError->hide();

    // Validate the token before using it, so a typo shows a clear message instead of a failed connection.
    m_rest->setToken(token);
    m_rest->get(QStringLiteral("/users/@me"), [this, token](const RestClient::Response& response) {
        m_tokenButton->setEnabled(true);
        m_tokenButton->setText(tr("Log In"));
        if (response.ok()) {
            m_auth->stop();
            emit loggedIn(token);
        } else if (response.status == 401 || response.status == 403) {
            showTokenError(tr("This token is invalid or has expired."));
        } else if (!response.networkError.isEmpty()) {
            showTokenError(tr("Could not connect to Discord. Check your internet connection and try again."));
        } else {
            showTokenError(tr("Discord rejected the login (HTTP %1).").arg(response.status));
        }
    });
}

void LoginWindow::showTokenError(const QString& message)
{
    m_tokenError->setText(message);
    m_tokenError->show();
}
