#pragma once

#include <QDateTime>
#include <QString>

// Reads the append-only event file produced by the optional Python screen
// verifier. Events recorded before this client session are ignored.
class ScreenCompletionReader
{
public:
    ScreenCompletionReader();

    // Consumes newly appended events and returns true only once for a matching
    // selected level. Malformed events are ignored safely.
    bool takeCompletionFor(const QString &level);

private:
    QDateTime m_startedAtUtc;
    qint64 m_readOffset = 0;
};

QString screenCompletionEventPath();
