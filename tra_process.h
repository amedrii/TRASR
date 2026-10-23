#pragma once

#include <QString>
#include <QtGlobal>

#include <optional>

// A read-only snapshot of the running Tomb Raider: Anniversary process.
struct TraProcess
{
    quint32 processId;
    QString executablePath;
    QString buildHash;
    QString blockedModule;

    float laraHealth;
    bool healthAvailable;

    float gameTime;
    bool gameTimeAvailable;

    float levelCount;
    bool levelCountAvailable;

    quint32 menuState;
    bool menuStateAvailable;

    bool loading;
    bool loadingAvailable;

    QString loadedLevelCode;
    bool loadedLevelAvailable;

    QString currentPositionCode;
    bool currentPositionAvailable;

    int crossedSignalId;
    bool signalCrossingAvailable;
};

std::optional<TraProcess> findTraProcess();