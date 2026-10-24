#pragma once

#include "attempt_log.h"
#include "screen_completion.h"

#include <QString>

#include <optional>

struct RunSelection
{
    int levelIndex;
    QString level;
    QString category;
    QString subcategory;
};

struct RunPresence
{
    QString state = QStringLiteral("waiting");
    float igtSeconds = -1.0f;
};

class RunMonitor
{
public:
    QString update(const RunSelection &selection);
    std::optional<AttemptRecord> takeSavedAttempt();
    RunPresence presence() const;

private:
    float m_previousGameTime = -1.0f;
    float m_previousLevelCount = -1.0f;
    int m_unchangedPolls = 0;
    QString m_activeSelectionKey;
    bool m_levelVerifiedThisRun = false;
    bool m_finishArmed = false;
    bool m_finishHandled = false;
    QString m_finishMessage;
    ScreenCompletionReader m_screenCompletionReader;
    std::optional<AttemptRecord> m_savedAttempt;
    RunPresence m_presence;
    int m_restartPresencePolls = 0;
};