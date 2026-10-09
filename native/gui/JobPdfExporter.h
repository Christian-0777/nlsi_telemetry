#pragma once

#include <QString>
#include <QVector>

#include "session/HistoryStore.h"

namespace nlsi::gui {

bool ExportJobsToPdf(
    const QString& path,
    const QVector<nlsi::session::JobRecord>& jobs,
    QString* error = nullptr);

} // namespace nlsi::gui
