#include "Language.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTranslator>

namespace {

constexpr auto SettingsKey = "language";
constexpr auto DefaultLanguage = "en";

} // namespace

namespace Language {

QList<std::pair<QString, QString>> available()
{
    return {
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("pt_BR"), QStringLiteral("Português (Brasil)")},
    };
}

QString current()
{
    const QString code = QSettings().value(SettingsKey, DefaultLanguage).toString();
    for (const auto& [known, name] : available()) {
        if (known == code)
            return code;
    }
    return DefaultLanguage;
}

void setCurrent(const QString& code)
{
    QSettings().setValue(SettingsKey, code);
}

void install(QCoreApplication& app)
{
    const QString code = current();
    if (code == QLatin1String(DefaultLanguage))
        return;

    auto* translator = new QTranslator(&app);
    if (translator->load(QStringLiteral(":/i18n/snapcord_%1.qm").arg(code)))
        QCoreApplication::installTranslator(translator);
    else
        delete translator;
}

} // namespace Language
