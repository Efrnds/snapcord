#pragma once

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcGateway)
Q_DECLARE_LOGGING_CATEGORY(lcVoice)

namespace Log {

// Writes all log output to snapcord.log in the app data folder (the previous run is kept as snapcord.old.log),
// so problems can be diagnosed after the fact. Tokens and keys are never logged.
void installFileHandler();
QString filePath();

} // namespace Log
