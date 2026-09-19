#pragma once

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

struct AttemptRecord
{
    QString level;
    QString category;
    QString subcategory;
    float igtSeconds;
    QString buildHash;
};

inline QString attemptLogPath()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(directory);
    return QDir(directory).filePath(QStringLiteral("attempts.json"));
}

inline bool appendAttempt(const AttemptRecord &record, QString *errorMessage)
{
    const QString filePath = attemptLogPath();
    QJsonArray attempts;
    QFile existingFile(filePath);

    if (existingFile.exists()) {
        if (!existingFile.open(QIODevice::ReadOnly)) {
            if (errorMessage) *errorMessage = QStringLiteral("Could not open the attempt log.");
            return false;
        }
        const QJsonDocument document = QJsonDocument::fromJson(existingFile.readAll());
        if (!document.isArray()) {
            if (errorMessage) *errorMessage = QStringLiteral("The attempt log has an invalid format.");
            return false;
        }
        attempts = document.array();
        existingFile.close();
    }

    attempts.append(QJsonObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("level"), record.level},
        {QStringLiteral("category"), record.category},
        {QStringLiteral("subcategory"), record.subcategory},
        {QStringLiteral("difficulty"), QStringLiteral("Easy")},
        {QStringLiteral("igtSeconds"), record.igtSeconds},
        {QStringLiteral("buildHash"), record.buildHash},
        {QStringLiteral("appVersion"), QStringLiteral("0.1.0")},
        {QStringLiteral("confirmedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}
    });

    QSaveFile outputFile(filePath);
    if (!outputFile.open(QIODevice::WriteOnly)
        || outputFile.write(QJsonDocument(attempts).toJson(QJsonDocument::Indented)) < 0
        || !outputFile.commit()) {
        if (errorMessage) *errorMessage = QStringLiteral("Could not save the attempt log.");
        return false;
    }
    return true;
}
