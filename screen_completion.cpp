#include "screen_completion.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

QString screenCompletionEventPath()
{
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    const QString directory = localAppData.isEmpty()
        ? QDir::tempPath() + QStringLiteral("/TRASR")
        : localAppData + QStringLiteral("/TRASR");

    QDir().mkpath(directory);
    return QDir(directory).filePath(QStringLiteral("level-end-events.jsonl"));
}

ScreenCompletionReader::ScreenCompletionReader()
    : m_startedAtUtc(QDateTime::currentDateTimeUtc())
{
    QFile file(screenCompletionEventPath());
    if (file.exists()) {
        m_readOffset = file.size();
    }
}

bool ScreenCompletionReader::takeCompletionFor(const QString &level)
{
    QFile file(screenCompletionEventPath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const qint64 size = file.size();
    if (size < m_readOffset) {
        // The file was replaced or cleared; begin at its new start.
        m_readOffset = 0;
    }

    if (!file.seek(m_readOffset)) {
        return false;
    }

    const QByteArray newEvents = file.readAll();
    m_readOffset = size;
    bool matchingCompletion = false;

    for (const QByteArray &line : newEvents.split('\n')) {
        const QJsonObject event = QJsonDocument::fromJson(line).object();
        if (event.value(QStringLiteral("event")).toString()
            != QStringLiteral("level_complete_detected")) {
            continue;
        }

        const QDateTime observedAt = QDateTime::fromString(
            event.value(QStringLiteral("observed_at_utc")).toString(),
            Qt::ISODateWithMs
            );

        if (!observedAt.isValid() || observedAt < m_startedAtUtc) {
            continue;
        }

        if (event.value(QStringLiteral("level")).toString() == level) {
            matchingCompletion = true;
        }
    }

    return matchingCompletion;
}
