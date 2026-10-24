#include "run_monitor.h"

#include "attempt_log.h"
#include "run_rules.h"
#include "tra_process.h"

#include <cmath>
// Save a verified completed attempt locally until results are submitted to the
// matchmaking service.
static bool saveCompletedAttempt(
    const QString &level, const QString &category, const QString &subcategory,
    float igt, const QString &buildHash, QString *error
    )
{
    return appendAttempt(
        AttemptRecord{level, category, subcategory, igt, buildHash}, error
        );
}

std::optional<AttemptRecord> RunMonitor::takeSavedAttempt()
{
    std::optional<AttemptRecord> attempt = m_savedAttempt;
    m_savedAttempt.reset();
    return attempt;
}

RunPresence RunMonitor::presence() const
{
    return m_presence;
}

QString RunMonitor::update(const RunSelection &selection)
{
            const auto process = findTraProcess();

            if (!process.has_value()) {
                m_previousGameTime = -1.0f;
                m_previousLevelCount = -1.0f;
                m_unchangedPolls = 0;
                m_levelVerifiedThisRun = false;
                m_finishArmed = false;
                m_finishHandled = false;
                m_finishMessage.clear();
                m_presence = RunPresence{QStringLiteral("waiting"), -1.0f};

                return QStringLiteral("Tomb Raider: Anniversary\nnot running");
            }

            const QString gameTimeStatus =
                process->gameTimeAvailable
                    ? formatIgt(process->gameTime)
                    : QStringLiteral("--:--");

            const QString loadedLevelStatus =
                process->loadedLevelAvailable
                    ? process->loadedLevelCode
                    : QStringLiteral("--");

            const QString currentPositionStatus =
                process->currentPositionAvailable
                    ? process->currentPositionCode
                    : QStringLiteral("--");

            const QString selectedRunKey =
                selection.level + QStringLiteral("\n")
                + selection.category + QStringLiteral("\n")
                + selection.subcategory;

            if (selectedRunKey != m_activeSelectionKey) {
                m_activeSelectionKey = selectedRunKey;
                m_levelVerifiedThisRun = false;
                m_finishArmed = false;
                m_finishHandled = false;
                m_finishMessage.clear();
                m_previousLevelCount = -1.0f;
            }

            const QString selectedLevelCode =
                expectedLevelCode(selection.levelIndex);

            // TRA may preload the next level before Lara has left the old level.
            // A run becomes eligible only when its loaded and current-position
            // codes independently identify the selected level. This deliberately
            // treats an unreadable or transitional state as unverified.
            const bool expectedLevelIsActive =
                !selectedLevelCode.isEmpty()
                && process->loadedLevelAvailable
                && process->currentPositionAvailable
                && process->loadedLevelCode.compare(
                       selectedLevelCode,
                       Qt::CaseInsensitive
                       ) == 0
                && process->currentPositionCode.compare(
                       selectedLevelCode,
                       Qt::CaseInsensitive
                       ) == 0;

            // Retain verification through the legitimate end-trigger/loading
            // transition once the selected level has been observed.
            if (expectedLevelIsActive) {
                m_levelVerifiedThisRun = true;
            }

            const QString levelVerificationStatus =
                selectedLevelCode.isEmpty()
                    ? QStringLiteral("Level verification: mapping needed")
                    : m_levelVerifiedThisRun
                          ? QStringLiteral("Level verification: passed")
                          : QStringLiteral("Level verification: failed");

            const bool gameTimeRewound =
                process->gameTimeAvailable
                && m_previousGameTime > 0.0f
                && process->gameTime + 0.001f < m_previousGameTime;

            // A death or checkpoint reload can place Lara on the other side of
            // a signal face. End the armed attempt before that movement is ever
            // considered a completion.
            if (gameTimeRewound) {
                m_levelVerifiedThisRun = false;
                m_finishArmed = false;
                m_finishHandled = false;
                m_finishMessage.clear();
                m_previousLevelCount = -1.0f;
                // Keep the restart state visible long enough for the opponent
                // heartbeat to report it at least once.
                m_restartPresencePolls = 50;
            }

            const bool levelCountAdvanced =
                process->levelCountAvailable
                && std::isfinite(process->levelCount)
                && m_previousLevelCount >= 0.0f
                && process->levelCount > m_previousLevelCount + 0.001f;

            const bool endscreenVisible =
                process->menuStateAvailable && process->menuState >= 1;

            QString runStatus;

            if (!process->gameTimeAvailable) {
                runStatus = QStringLiteral("IGT unavailable");
                m_previousGameTime = -1.0f;
                m_previousLevelCount = -1.0f;
                m_unchangedPolls = 0;
            } else if (process->gameTime <= 0.0f) {
                runStatus = QStringLiteral("Waiting for level");
                m_previousGameTime = 0.0f;
                m_unchangedPolls = 0;
            } else if (process->gameTime > m_previousGameTime + 0.001f) {
                runStatus = QStringLiteral("Running");
                m_unchangedPolls = 0;
            } else {
                // Twenty 100 ms polls gives a two-second grace period. This stops
                // a normal IGT update gap from being mistaken for a pause/finish.
                ++m_unchangedPolls;

                runStatus = m_unchangedPolls <= 20
                                ? QStringLiteral("Running")
                                : QStringLiteral("Finish / pause candidate: ")
                                      + formatIgt(process->gameTime);
            }

            m_previousGameTime = process->gameTime;

            // TRAE-Menu-Hook.asi blocks attempt confirmation.
            const bool integrityPassed = process->blockedModule.isEmpty();

            const QString endTriggerCode =
                expectedEndTriggerCode(selection.levelIndex);

            const bool onEndPortal =
                !endTriggerCode.isEmpty()
                && process->currentPositionAvailable
                && process->currentPositionCode.compare(
                       endTriggerCode,
                       Qt::CaseInsensitive
                       ) == 0;

            const bool onEndSignal =
                process->signalCrossingAvailable
                && isExpectedEndSignal(
                       selection.levelIndex,
                       process->crossedSignalId
                       );

            // TRA commits its level counter on the result screen. Unlike a
            // portal, this catches cutscene and QTE endings without guessing
            // their intermediate position codes.
            const bool onEndscreenCompletion =
                levelCountAdvanced && endscreenVisible;

            const bool onEndTrigger =
                onEndPortal || onEndSignal || onEndscreenCompletion;

            // Signal IDs are reused by unrelated trigger meshes. They may
            // assist diagnostics, but never authorise a finish. The optional
            // screen verifier independently identifies the actual result
            // screen and writes one matching completion event.
            const bool screenCompletionConfirmed =
                m_screenCompletionReader.takeCompletionFor(selection.level);

            // Loading a save can also move game state forward. Never carry an
            // armed attempt across that transition; it must be observed again
            // from stable in-level state.
            if (m_finishArmed && process->loadingAvailable && process->loading) {
                m_finishArmed = false;
                m_finishMessage = QStringLiteral("Run invalidated: loading observed");
            }

            // Arm after a valid run starts away from its completion portal.
            // This prevents stale portal data from recording a duplicate attempt.
            if (!m_finishHandled
                && m_levelVerifiedThisRun
                && expectedLevelIsActive
                && process->gameTimeAvailable
                && process->gameTime > 0.0f
                && !gameTimeRewound
                && (!process->loadingAvailable || !process->loading)
                && !onEndTrigger) {
                m_finishArmed = true;
                m_finishMessage.clear();
            }

            if (!m_finishHandled
                && m_finishArmed
                && screenCompletionConfirmed
                && process->gameTimeAvailable
                && process->gameTime > 0.0f
                && !gameTimeRewound
                && (!process->loadingAvailable || !process->loading)) {
                if (!integrityPassed) {
                    m_finishMessage = QStringLiteral("Finish rejected: ")
                        + process->blockedModule;
                } else {
                    QString saveError;

                    if (saveCompletedAttempt(
                            selection.level,
                            selection.category,
                            selection.subcategory,
                            process->gameTime,
                            process->buildHash,
                            &saveError)) {
                        m_savedAttempt = AttemptRecord{
                            selection.level,
                            selection.category,
                            selection.subcategory,
                            process->gameTime,
                            process->buildHash
                        };
                        m_finishMessage = QStringLiteral("Finish saved: ")
                            + formatIgt(process->gameTime);
                    } else {
                        m_finishMessage = QStringLiteral("Finish not saved: ")
                            + saveError;
                    }
                }

                // Consume this completion even when integrity fails: a persistent
                // portal code must never be accepted after a Menu Hook is removed.
                m_finishArmed = false;
                m_finishHandled = true;
            }
            m_previousLevelCount =
                process->levelCountAvailable && std::isfinite(process->levelCount)
                    ? process->levelCount
                    : -1.0f;

            if (m_finishHandled) {
                m_presence = RunPresence{QStringLiteral("finished"), process->gameTime};
            } else if (m_restartPresencePolls > 0) {
                --m_restartPresencePolls;
                m_presence = RunPresence{QStringLiteral("restarted"), process->gameTime};
            } else if (m_levelVerifiedThisRun && process->gameTimeAvailable
                       && process->gameTime > 0.0f) {
                m_presence = RunPresence{QStringLiteral("running"), process->gameTime};
            } else {
                m_presence = RunPresence{QStringLiteral("waiting"), process->gameTime};
            }

            const QString integrityStatus =
                integrityPassed
                    ? QStringLiteral("Integrity: passed")
                    : QStringLiteral("Integrity failure: ")
                          + process->blockedModule;

            const bool isInMainMenu =
                loadedLevelStatus.compare(QStringLiteral("Pu104"), Qt::CaseInsensitive) == 0
                || loadedLevelStatus.compare(QStringLiteral("Gr104"), Qt::CaseInsensitive) == 0
                || loadedLevelStatus.compare(QStringLiteral("Eg104"), Qt::CaseInsensitive) == 0
                || loadedLevelStatus.compare(QStringLiteral("Lc104"), Qt::CaseInsensitive) == 0;

            // Keep one fixed layout. The central line changes with the game state,
            // so the window does not resize when leaving the main menu.
            const QString activityStatus = !m_finishMessage.isEmpty()
                ? m_finishMessage
                : isInMainMenu ? QStringLiteral("Main Menu") : runStatus;

            return QStringLiteral(
                    "Selected speedrun:\n%1\n%2\n%3"
                    "\n\n%4\n\nIGT\n%5\n\n%6"
                    )
                    .arg(
                        selection.level,
                        rulesetName(selection.category, selection.subcategory),
                        levelVerificationStatus,
                        activityStatus,
                        gameTimeStatus,
                        integrityStatus
                        );

}
