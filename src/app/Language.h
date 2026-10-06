#pragma once

#include <QList>
#include <QString>

#include <utility>

class QCoreApplication;

// Interface language. English is the default; other languages are loaded from Qt translation files.
namespace Language {

// Language code and its name written in that language, e.g. {"pt_BR", "Português (Brasil)"}.
QList<std::pair<QString, QString>> available();

QString current();
void setCurrent(const QString& code);

// Loads the saved language. Must be called before any window is created.
void install(QCoreApplication& app);

} // namespace Language
