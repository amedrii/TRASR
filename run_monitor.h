#pragma once

#include <QString>

struct RunSelection
{
    int levelIndex;
    QString level;
    QString category;
    QString subcategory;
};

class RunMonitor
{
public:
    QString update(const RunSelection &selection);

private:
    float m_previousGameTime = -1.0f;
    float m_previousLevelCount = -1.0f;
    int m_unchangedPolls = 0;
    QString m_activeSelectionKey;
    bool m_levelVerifiedThisRun = false;
    bool m_finishArmed = false;
    bool m_finishHandled = false;
    QString m_finishMessage;
};