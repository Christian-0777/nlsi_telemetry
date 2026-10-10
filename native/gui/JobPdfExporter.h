#pragma once

#include <QString>
#include <QVector>

#include "session/HistoryStore.h"

namespace nlsi::gui {

bool ExportJobsToPdf(
    const QString& path,
    const QVector<nlsi::session::JobRecord>& jobs,
    QString* error = nullptr);

bool ExportJobToPdf(
    const QString& path,
    const nlsi::session::JobRecord& job,
    QString* error = nullptr);

} // namespace nlsi::gui
