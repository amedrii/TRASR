#include <QApplication>
#include "attempt_log.h"
#include "tra_process.h"
#include "run_rules.h"
#include <QComboBox>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

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



int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("TRASR"));
    QCoreApplication::setApplicationName(QStringLiteral("TRASR"));

    // QSettings stores login and UI selections between application launches.
    QSettings settings;

    QString playerId =
        settings.value(QStringLiteral("matchmaking/playerId")).toString();

    if (playerId.isEmpty()) {
        playerId = QUuid::createUuid().toString(QUuid::WithoutBraces);

        settings.setValue(
            QStringLiteral("matchmaking/playerId"),
            playerId
            );
    }

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("TRASR"));

    QDialog loginDialog(&window);
    loginDialog.setWindowTitle(QStringLiteral("TRSR login"));
    loginDialog.setModal(true);

    auto *loginLayout = new QVBoxLayout(&loginDialog);

    auto *loginIntro = new QLabel(
        QStringLiteral("Link your Speedrun.com account to use TRSR matchmaking.")
        );
    loginIntro->setWordWrap(true);

    auto *speedrunApiKeyLabel = new QLabel(
        QStringLiteral("Speedrun.com API key")
        );

    auto *speedrunApiKeyInput = new QLineEdit;
    speedrunApiKeyInput->setEchoMode(QLineEdit::Password);
    speedrunApiKeyInput->setPlaceholderText(
        QStringLiteral("Paste from speedrun.com/api/auth")
        );

    auto *linkSpeedrunButton = new QPushButton(
        QStringLiteral("Link Speedrun.com")
        );

    auto *speedrunStatus = new QLabel(
        QStringLiteral("Speedrun.com: checking saved login...")
        );
    speedrunStatus->setAlignment(Qt::AlignCenter);

    loginLayout->addWidget(loginIntro);
    loginLayout->addWidget(speedrunApiKeyLabel);
    loginLayout->addWidget(speedrunApiKeyInput);
    loginLayout->addWidget(linkSpeedrunButton);
    loginLayout->addWidget(speedrunStatus);

    auto *accountStatus = new QLabel(
        QStringLiteral("Speedrun.com: not linked")
        );
    accountStatus->setAlignment(Qt::AlignCenter);

    auto *logoutButton = new QPushButton(
        QStringLiteral("Log out")
        );

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);

    auto *levelLabel = new QLabel(QStringLiteral("Level"));
    auto *levelSelector = new QComboBox;

    levelSelector->addItems(QStringList{
        QStringLiteral("Mountain Caves"),
        QStringLiteral("City of Vilcabamba"),
        QStringLiteral("The Lost Valley"),
        QStringLiteral("Tomb of Qualopec"),
        QStringLiteral("St. Francis Folly"),
        QStringLiteral("The Coliseum"),
        QStringLiteral("Midas Palace"),
        QStringLiteral("Tomb of Tihocan"),
        QStringLiteral("Temple of Khamoon"),
        QStringLiteral("Obelisk of Khamoon"),
        QStringLiteral("Sanctuary of the Scion"),
        QStringLiteral("Natla's Mines"),
        QStringLiteral("The Great Pyramid"),
        QStringLiteral("Final Conflict"),
        QStringLiteral("Croft Manor")
    });

    auto *categoryLabel = new QLabel(QStringLiteral("Category"));
    auto *categorySelector = new QComboBox;

    categorySelector->addItems(QStringList{
        QStringLiteral("Any%"),
        QStringLiteral("100%")
    });

    auto *subcategoryLabel = new QLabel(QStringLiteral("Subcategory"));
    auto *subcategorySelector = new QComboBox;

    subcategorySelector->addItems(QStringList{
        QStringLiteral("No Bug Jump"),
        QStringLiteral("Bug Jump"),
        QStringLiteral("Better Than Glitchless"),
        QStringLiteral("Glitchless")
    });

    // Restore the user's previous local selection.
    const int savedLevelIndex = levelSelector->findText(
        settings.value(
                    QStringLiteral("selectedLevel"),
                    QStringLiteral("Mountain Caves")
                    ).toString()
        );

    if (savedLevelIndex >= 0) {
        levelSelector->setCurrentIndex(savedLevelIndex);
    }

    const int savedCategoryIndex = categorySelector->findText(
        settings.value(
                    QStringLiteral("selectedCategory"),
                    QStringLiteral("Any%")
                    ).toString()
        );

    if (savedCategoryIndex >= 0) {
        categorySelector->setCurrentIndex(savedCategoryIndex);
    }

    const int savedSubcategoryIndex = subcategorySelector->findText(
        settings.value(
                    QStringLiteral("selectedSubcategory"),
                    QStringLiteral("No Bug Jump")
                    ).toString()
        );

    if (savedSubcategoryIndex >= 0) {
        subcategorySelector->setCurrentIndex(savedSubcategoryIndex);
    }

    auto *status = new QLabel;
    status->setAlignment(Qt::AlignCenter);
    status->setWordWrap(true);

    auto *serverStatus = new QLabel(
        QStringLiteral("Matchmaking server: checking...")
        );
    serverStatus->setAlignment(Qt::AlignCenter);

    // UI only for now; queue joining is the next matchmaking step.
    auto *randomQueueButton = new QPushButton(
        QStringLiteral("Join random queue")
        );

    auto *rulesetQueueButton = new QPushButton(
        QStringLiteral("Join selected ruleset queue")
        );

    auto *cancelQueueButton = new QPushButton(
        QStringLiteral("Cancel queue")
        );

    auto *queueStatus = new QLabel(
        QStringLiteral("Matchmaking: not queued")
        );
    queueStatus->setAlignment(Qt::AlignCenter);

    QFont statusFont;
    statusFont.setPointSize(16);
    statusFont.setBold(true);
    status->setFont(statusFont);

    // Save a changed ruleset between launches.
    auto refreshSelection =
        [levelSelector, categorySelector, subcategorySelector]() {
            QSettings settings;

            const QString level = levelSelector->currentText();
            const QString category = categorySelector->currentText();
            const QString subcategory = subcategorySelector->currentText();

            settings.setValue(QStringLiteral("selectedLevel"), level);
            settings.setValue(QStringLiteral("selectedCategory"), category);
            settings.setValue(
                QStringLiteral("selectedSubcategory"),
                subcategory
                );

        };

    refreshSelection();

    layout->addWidget(accountStatus);
    layout->addWidget(logoutButton);
    layout->addWidget(levelLabel);
    layout->addWidget(levelSelector);
    layout->addWidget(categoryLabel);
    layout->addWidget(categorySelector);
    layout->addWidget(subcategoryLabel);
    layout->addWidget(subcategorySelector);
    layout->addWidget(serverStatus);
    layout->addWidget(randomQueueButton);
    layout->addWidget(rulesetQueueButton);
    layout->addWidget(cancelQueueButton);
    layout->addWidget(queueStatus);
    layout->addWidget(status);

    window.setCentralWidget(content);

    // Local development health check for the Node matchmaking server.
    auto *networkManager = new QNetworkAccessManager(&window);

    auto *matchPollTimer = new QTimer(&window);
    matchPollTimer->setInterval(1000);

    auto *healthReply = networkManager->get(
        QNetworkRequest(
            QUrl(QStringLiteral("https://api.trsr.app/health"))
            )
        );

    QObject::connect(
        logoutButton,
        &QPushButton::clicked,
        &window,
        [&window]() {
            QSettings settings;
            settings.remove(QStringLiteral("matchmaking/sessionToken"));
            settings.remove(QStringLiteral("matchmaking/speedrunUserId"));
            settings.sync();
            window.close();
        }
        );

    const QString savedSessionToken =
        settings.value(QStringLiteral("matchmaking/sessionToken")).toString();

    if (savedSessionToken.isEmpty()) {
        speedrunStatus->setText(
            QStringLiteral("Link your Speedrun.com account to continue.")
            );
    } else {
        QJsonObject sessionRequestBody;
        sessionRequestBody.insert(
            QStringLiteral("sessionToken"),
            savedSessionToken
            );

        QNetworkRequest sessionRequest{
            QUrl(QStringLiteral("https://api.trsr.app/auth/session"))
        };
        sessionRequest.setHeader(
            QNetworkRequest::ContentTypeHeader,
            QStringLiteral("application/json")
            );

        auto *sessionReply = networkManager->post(
            sessionRequest,
            QJsonDocument(sessionRequestBody).toJson(QJsonDocument::Compact)
            );

        QObject::connect(
            sessionReply,
            &QNetworkReply::finished,
            [sessionReply, speedrunStatus, accountStatus, &loginDialog]() {
                const QJsonObject response =
                    QJsonDocument::fromJson(sessionReply->readAll()).object();

                if (sessionReply->error() != QNetworkReply::NoError
                    || response.value(QStringLiteral("status")).toString()
                           != QStringLiteral("valid")) {
                    QSettings settings;
                    settings.remove(QStringLiteral("matchmaking/sessionToken"));
                    settings.remove(QStringLiteral("matchmaking/speedrunUserId"));

                    speedrunStatus->setText(
                        QStringLiteral("Link your Speedrun.com account to continue.")
                        );
                    sessionReply->deleteLater();
                    return;
                }

                const QJsonObject profile =
                    response.value(QStringLiteral("profile")).toObject();
                const QString displayName =
                    profile.value(QStringLiteral("displayName")).toString();

                accountStatus->setText(
                    QStringLiteral("Speedrun.com: linked as %1")
                        .arg(displayName)
                    );
                speedrunStatus->setText(
                    QStringLiteral("Speedrun.com: linked as %1")
                        .arg(displayName)
                    );

                loginDialog.accept();
                sessionReply->deleteLater();
            }
            );
    }

    QObject::connect(
        linkSpeedrunButton,
        &QPushButton::clicked,
        &loginDialog,
        [networkManager, speedrunApiKeyInput, linkSpeedrunButton,
         speedrunStatus, accountStatus, &loginDialog]() {
            const QString apiKey = speedrunApiKeyInput->text().trimmed();

            if (apiKey.isEmpty()) {
                speedrunStatus->setText(
                    QStringLiteral("Speedrun.com: enter an API key first")
                    );
                return;
            }

            linkSpeedrunButton->setEnabled(false);
            speedrunStatus->setText(
                QStringLiteral("Speedrun.com: linking...")
                );

            QJsonObject requestBody;
            requestBody.insert(QStringLiteral("apiKey"), apiKey);

            QNetworkRequest request{
                QUrl(QStringLiteral("https://api.trsr.app/auth/speedrun"))
            };
            request.setHeader(
                QNetworkRequest::ContentTypeHeader,
                QStringLiteral("application/json")
                );

            auto *reply = networkManager->post(
                request,
                QJsonDocument(requestBody).toJson(QJsonDocument::Compact)
                );

            QObject::connect(
                reply,
                &QNetworkReply::finished,
                [reply, speedrunApiKeyInput, linkSpeedrunButton,
                 speedrunStatus, accountStatus, &loginDialog]() {
                    const QJsonObject response =
                        QJsonDocument::fromJson(reply->readAll()).object();

                    if (reply->error() != QNetworkReply::NoError) {
                        const QString serverError =
                            response.value(QStringLiteral("error")).toString();

                        speedrunStatus->setText(
                            serverError.isEmpty()
                                ? QStringLiteral("Speedrun.com: %1").arg(reply->errorString())
                                : QStringLiteral("Speedrun.com: %1").arg(serverError)
                            );

                        linkSpeedrunButton->setEnabled(true);
                        reply->deleteLater();
                        return;
                    }

                    if (response.value(QStringLiteral("status")).toString()
                        != QStringLiteral("linked")) {
                        speedrunStatus->setText(
                            QStringLiteral("Speedrun.com: invalid API key")
                            );
                        linkSpeedrunButton->setEnabled(true);
                        reply->deleteLater();
                        return;
                    }

                    const QJsonObject profile =
                        response.value(QStringLiteral("profile")).toObject();

                    const QString displayName =
                        profile.value(QStringLiteral("displayName")).toString();

                    QSettings settings;
                    settings.setValue(
                        QStringLiteral("matchmaking/sessionToken"),
                        response.value(QStringLiteral("sessionToken")).toString()
                        );
                    settings.setValue(
                        QStringLiteral("matchmaking/speedrunUserId"),
                        profile.value(QStringLiteral("id")).toString()
                        );
                    speedrunApiKeyInput->clear();

                    speedrunStatus->setText(
                        QStringLiteral("Speedrun.com: linked as %1")
                            .arg(displayName)
                        );

                    accountStatus->setText(
                        QStringLiteral("Speedrun.com: linked as %1")
                            .arg(displayName)
                        );

                    loginDialog.accept();

                    reply->deleteLater();
                }
                );
        }
        );

    QObject::connect(
        healthReply,
        &QNetworkReply::finished,
        &window,
        [healthReply, serverStatus]() {
            if (healthReply->error() == QNetworkReply::NoError) {
                serverStatus->setText(
                    QStringLiteral("Matchmaking server: connected")
                    );
            } else {
                serverStatus->setText(
                    QStringLiteral("Matchmaking server: offline")
                    );
            }

            healthReply->deleteLater();
        }
        );

    QObject::connect(
        matchPollTimer,
        &QTimer::timeout,
        &window,
        [networkManager, matchPollTimer, randomQueueButton, rulesetQueueButton, queueStatus,
         levelSelector, categorySelector, subcategorySelector, playerId]() {
            auto *reply = networkManager->get(
                QNetworkRequest(
                    QUrl(
                        QStringLiteral("https://api.trsr.app/matches/")
                        + playerId
                        )
                    )
                );

            QObject::connect(
                reply,
                &QNetworkReply::finished,
                [reply, matchPollTimer, randomQueueButton, rulesetQueueButton,
                 queueStatus, levelSelector, categorySelector, subcategorySelector]() {
                    if (reply->error() != QNetworkReply::NoError) {
                        reply->deleteLater();
                        return;
                    }

                    const QJsonObject response =
                        QJsonDocument::fromJson(reply->readAll()).object();

                    if (response.value(
                                    QStringLiteral("status")
                                    ).toString() != QStringLiteral("matched")) {
                        reply->deleteLater();
                        return;
                    }

                    const QJsonObject match =
                        response.value(QStringLiteral("match")).toObject();

                    levelSelector->setCurrentText(
                        match.value(QStringLiteral("level")).toString()
                        );

                    categorySelector->setCurrentText(
                        match.value(QStringLiteral("category")).toString()
                        );

                    subcategorySelector->setCurrentText(
                        match.value(QStringLiteral("subcategory")).toString()
                        );

                    queueStatus->setText(
                        QStringLiteral("Match found: %1 — %2 — %3")
                            .arg(match.value(
                                          QStringLiteral("level")
                                          ).toString())
                            .arg(match.value(
                                          QStringLiteral("category")
                                          ).toString())
                            .arg(match.value(
                                          QStringLiteral("subcategory")
                                          ).toString())
                        );

                    matchPollTimer->stop();
                    randomQueueButton->setEnabled(false);
                    rulesetQueueButton->setEnabled(false);
                    reply->deleteLater();
                }
                );
        }
        );

    QObject::connect(
        cancelQueueButton,
        &QPushButton::clicked,
        &window,
        [networkManager, randomQueueButton, rulesetQueueButton,
         queueStatus, playerId]() {
            QSettings settings;
            const QString sessionToken =
                settings.value(QStringLiteral("matchmaking/sessionToken")).toString();

            if (sessionToken.isEmpty()) {
                queueStatus->setText(
                    QStringLiteral("Matchmaking: link Speedrun.com first")
                    );
                return;
            }

            QJsonObject requestBody{
                {QStringLiteral("playerId"), playerId},
                {QStringLiteral("sessionToken"), sessionToken}
            };

            QNetworkRequest request(
                QUrl(QStringLiteral("https://api.trsr.app/queue/cancel"))
                );
            request.setHeader(
                QNetworkRequest::ContentTypeHeader,
                QStringLiteral("application/json")
                );

            auto *reply = networkManager->post(
                request,
                QJsonDocument(requestBody).toJson(QJsonDocument::Compact)
                );

            QObject::connect(
                reply,
                &QNetworkReply::finished,
                [reply, randomQueueButton, rulesetQueueButton, queueStatus]() {
                    if (reply->error() != QNetworkReply::NoError) {
                        queueStatus->setText(
                            QStringLiteral("Matchmaking: could not cancel queue")
                            );
                        reply->deleteLater();
                        return;
                    }

                    queueStatus->setText(
                        QStringLiteral("Matchmaking: queue cancelled")
                        );
                    randomQueueButton->setEnabled(true);
                    rulesetQueueButton->setEnabled(true);
                    reply->deleteLater();
                }
                );
        }
        );

    QObject::connect(
        randomQueueButton,
        &QPushButton::clicked,
        &window,
        [networkManager, randomQueueButton, queueStatus,
         levelSelector, categorySelector, subcategorySelector, matchPollTimer,
         playerId]() {
            QSettings settings;

            const QString sessionToken =
                settings.value(QStringLiteral("matchmaking/sessionToken")).toString();

            if (sessionToken.isEmpty()) {
                queueStatus->setText(
                    QStringLiteral("Matchmaking: link Speedrun.com first")
                    );
                return;
            }

            randomQueueButton->setEnabled(false);

            queueStatus->setText(
                QStringLiteral("Matchmaking: joining random queue...")
                );

            const QJsonObject requestBody{
                {QStringLiteral("playerId"), playerId},
                {QStringLiteral("sessionToken"), sessionToken},
                {QStringLiteral("queueType"), QStringLiteral("random")}
            };

            QNetworkRequest request(
                QUrl(QStringLiteral("https://api.trsr.app/queue"))
                );

            request.setHeader(
                QNetworkRequest::ContentTypeHeader,
                QStringLiteral("application/json")
                );

            auto *reply = networkManager->post(
                request,
                QJsonDocument(requestBody).toJson(QJsonDocument::Compact)
                );

            QObject::connect(
                reply,
                &QNetworkReply::finished,
                [reply, randomQueueButton, queueStatus, matchPollTimer,
                levelSelector, categorySelector, subcategorySelector]() {
                    if (reply->error() != QNetworkReply::NoError) {
                        queueStatus->setText(
                            QStringLiteral("Matchmaking: server request failed")
                            );

                        randomQueueButton->setEnabled(true);
                        reply->deleteLater();
                        return;
                    }

                    const QJsonObject response =
                        QJsonDocument::fromJson(reply->readAll()).object();

                    const QString responseStatus =
                        response.value(QStringLiteral("status")).toString();

                    if (responseStatus == QStringLiteral("waiting")) {
                        queueStatus->setText(
                            QStringLiteral("Matchmaking: waiting for opponent")
                            );

                        matchPollTimer->start();

                    } else if (responseStatus == QStringLiteral("matched")) {
                        const QJsonObject match =
                            response.value(QStringLiteral("match")).toObject();

                        levelSelector->setCurrentText(
                            match.value(QStringLiteral("level")).toString()
                            );

                        categorySelector->setCurrentText(
                            match.value(QStringLiteral("category")).toString()
                            );

                        subcategorySelector->setCurrentText(
                            match.value(QStringLiteral("subcategory")).toString()
                            );

                        queueStatus->setText(
                            QStringLiteral("Match found: %1 — %2 — %3")
                                .arg(match.value(
                                              QStringLiteral("level")
                                              ).toString())
                                .arg(match.value(
                                              QStringLiteral("category")
                                              ).toString())
                                .arg(match.value(
                                              QStringLiteral("subcategory")
                                              ).toString())
                            );
                    } else {
                        queueStatus->setText(
                            QStringLiteral("Matchmaking: ")
                            + response.value(
                                          QStringLiteral("error")
                                          ).toString()
                            );

                        randomQueueButton->setEnabled(true);
                    }

                    reply->deleteLater();
                }
                );
        }
        );

    QObject::connect(
        rulesetQueueButton,
        &QPushButton::clicked,
        &window,
        [networkManager, randomQueueButton,
         rulesetQueueButton, queueStatus, levelSelector,
         categorySelector, subcategorySelector, matchPollTimer,
         playerId]() {
            QSettings settings;

            const QString sessionToken =
                settings.value(QStringLiteral("matchmaking/sessionToken")).toString();

            if (sessionToken.isEmpty()) {
                queueStatus->setText(
                    QStringLiteral("Matchmaking: link Speedrun.com first")
                    );
                return;
            }

            randomQueueButton->setEnabled(false);
            rulesetQueueButton->setEnabled(false);

            queueStatus->setText(
                QStringLiteral("Matchmaking: joining selected ruleset queue...")
                );

            const QJsonObject requestBody{
                {QStringLiteral("playerId"), playerId},
                {QStringLiteral("sessionToken"), sessionToken},
                {QStringLiteral("queueType"), QStringLiteral("ruleset")},
                {QStringLiteral("category"), categorySelector->currentText()},
                {QStringLiteral(
                     "subcategory"
                     ), subcategorySelector->currentText()}
            };

            QNetworkRequest request(
                QUrl(QStringLiteral("https://api.trsr.app/queue"))
                );

            request.setHeader(
                QNetworkRequest::ContentTypeHeader,
                QStringLiteral("application/json")
                );

            auto *reply = networkManager->post(
                request,
                QJsonDocument(requestBody).toJson(QJsonDocument::Compact)
                );

            QObject::connect(
                reply,
                &QNetworkReply::finished,
                [reply, randomQueueButton, rulesetQueueButton,
                 queueStatus, levelSelector, categorySelector,
                 subcategorySelector, matchPollTimer]() {
                    if (reply->error() != QNetworkReply::NoError) {
                        queueStatus->setText(
                            QStringLiteral("Matchmaking: server request failed")
                            );

                        randomQueueButton->setEnabled(true);
                        rulesetQueueButton->setEnabled(true);
                        reply->deleteLater();
                        return;
                    }

                    const QJsonObject response =
                        QJsonDocument::fromJson(reply->readAll()).object();

                    const QString responseStatus =
                        response.value(QStringLiteral("status")).toString();

                    if (responseStatus == QStringLiteral("waiting")) {
                        queueStatus->setText(
                            QStringLiteral(
                                "Matchmaking: waiting for ruleset opponent"
                                )
                            );

                        matchPollTimer->start();
                    } else if (responseStatus == QStringLiteral("matched")) {
                        const QJsonObject match =
                            response.value(QStringLiteral("match")).toObject();

                        levelSelector->setCurrentText(
                            match.value(QStringLiteral("level")).toString()
                            );

                        categorySelector->setCurrentText(
                            match.value(QStringLiteral("category")).toString()
                            );

                        subcategorySelector->setCurrentText(
                            match.value(QStringLiteral("subcategory")).toString()
                            );

                        queueStatus->setText(
                            QStringLiteral("Match found: %1 — %2 — %3")
                                .arg(match.value(
                                              QStringLiteral("level")
                                              ).toString())
                                .arg(match.value(
                                              QStringLiteral("category")
                                              ).toString())
                                .arg(match.value(
                                              QStringLiteral("subcategory")
                                              ).toString())
                            );

                        matchPollTimer->stop();
                    } else {
                        queueStatus->setText(
                            QStringLiteral("Matchmaking: ")
                            + response.value(
                                          QStringLiteral("error")
                                          ).toString()
                            );

                        randomQueueButton->setEnabled(true);
                        rulesetQueueButton->setEnabled(true);
                    }

                    reply->deleteLater();
                }
                );
        }
        );

    QObject::connect(
        levelSelector,
        &QComboBox::currentTextChanged,
        &window,
        [refreshSelection](const QString &) {
            refreshSelection();
        }
        );

    QObject::connect(
        categorySelector,
        &QComboBox::currentTextChanged,
        &window,
        [refreshSelection](const QString &) {
            refreshSelection();
        }
        );

    QObject::connect(
        subcategorySelector,
        &QComboBox::currentTextChanged,
        &window,
        [refreshSelection](const QString &) {
            refreshSelection();
        }
        );

    // Poll ten times per second. IGT updates in whole seconds, but faster polling
    // helps distinguish normal play from pauses and loading transitions.
    auto updateStatus =
        [status, levelSelector, categorySelector, subcategorySelector,
         previousGameTime = -1.0f, previousLevelCount = -1.0f,
         unchangedPolls = 0, activeSelectionKey = QString(),
         levelVerifiedThisRun = false,
         finishArmed = false, finishHandled = false,
         finishMessage = QString()]() mutable {
            const auto process = findTraProcess();

            if (!process.has_value()) {
                previousGameTime = -1.0f;
                previousLevelCount = -1.0f;
                unchangedPolls = 0;
                levelVerifiedThisRun = false;
                finishArmed = false;
                finishHandled = false;
                finishMessage.clear();

                status->setText(
                    QStringLiteral("Tomb Raider: Anniversary\nnot running")
                    );
                return;
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
                levelSelector->currentText() + QStringLiteral("\n")
                + categorySelector->currentText() + QStringLiteral("\n")
                + subcategorySelector->currentText();

            if (selectedRunKey != activeSelectionKey) {
                activeSelectionKey = selectedRunKey;
                levelVerifiedThisRun = false;
                finishArmed = false;
                finishHandled = false;
                finishMessage.clear();
                previousLevelCount = -1.0f;
            }

            const QString selectedLevelCode =
                expectedLevelCode(levelSelector->currentIndex());

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
                levelVerifiedThisRun = true;
            }

            const QString levelVerificationStatus =
                selectedLevelCode.isEmpty()
                    ? QStringLiteral("Level verification: mapping needed")
                    : levelVerifiedThisRun
                          ? QStringLiteral("Level verification: passed")
                          : QStringLiteral("Level verification: failed");

            const bool gameTimeRewound =
                process->gameTimeAvailable
                && previousGameTime > 0.0f
                && process->gameTime + 0.001f < previousGameTime;

            // A death or checkpoint reload can place Lara on the other side of
            // a signal face. End the armed attempt before that movement is ever
            // considered a completion.
            if (gameTimeRewound) {
                levelVerifiedThisRun = false;
                finishArmed = false;
                finishHandled = false;
                finishMessage.clear();
                previousLevelCount = -1.0f;
            }

            const bool levelCountAdvanced =
                process->levelCountAvailable
                && std::isfinite(process->levelCount)
                && previousLevelCount >= 0.0f
                && process->levelCount > previousLevelCount + 0.001f;

            const bool endscreenVisible =
                process->menuStateAvailable && process->menuState >= 1;

            QString runStatus;

            if (!process->gameTimeAvailable) {
                runStatus = QStringLiteral("IGT unavailable");
                previousGameTime = -1.0f;
                previousLevelCount = -1.0f;
                unchangedPolls = 0;
            } else if (process->gameTime <= 0.0f) {
                runStatus = QStringLiteral("Waiting for level");
                previousGameTime = 0.0f;
                unchangedPolls = 0;
            } else if (process->gameTime > previousGameTime + 0.001f) {
                runStatus = QStringLiteral("Running");
                unchangedPolls = 0;
            } else {
                // Twenty 100 ms polls gives a two-second grace period. This stops
                // a normal IGT update gap from being mistaken for a pause/finish.
                ++unchangedPolls;

                runStatus = unchangedPolls <= 20
                                ? QStringLiteral("Running")
                                : QStringLiteral("Finish / pause candidate: ")
                                      + formatIgt(process->gameTime);
            }

            previousGameTime = process->gameTime;

            // TRAE-Menu-Hook.asi blocks attempt confirmation.
            const bool integrityPassed = process->blockedModule.isEmpty();

            const QString endTriggerCode =
                expectedEndTriggerCode(levelSelector->currentIndex());

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
                       levelSelector->currentIndex(),
                       process->crossedSignalId
                       );

            // TRA commits its level counter on the result screen. Unlike a
            // portal, this catches cutscene and QTE endings without guessing
            // their intermediate position codes.
            const bool onEndscreenCompletion =
                levelCountAdvanced && endscreenVisible;

            const bool onEndTrigger =
                onEndPortal || onEndSignal || onEndscreenCompletion;

            // Loading a save can also move game state forward. Never carry an
            // armed attempt across that transition; it must be observed again
            // from stable in-level state.
            if (finishArmed && process->loadingAvailable && process->loading) {
                finishArmed = false;
                finishMessage = QStringLiteral("Run invalidated: loading observed");
            }

            // Arm after a valid run starts away from its completion portal.
            // This prevents stale portal data from recording a duplicate attempt.
            if (!finishHandled
                && levelVerifiedThisRun
                && expectedLevelIsActive
                && process->gameTimeAvailable
                && process->gameTime > 0.0f
                && !gameTimeRewound
                && process->healthAvailable
                && process->laraHealth > 0.0f
                && (!process->loadingAvailable || !process->loading)
                && !onEndTrigger) {
                finishArmed = true;
                finishMessage.clear();
            }

            if (!finishHandled
                && finishArmed
                && onEndTrigger
                && process->gameTimeAvailable
                && process->gameTime > 0.0f
                && !gameTimeRewound
                && process->healthAvailable
                && process->laraHealth > 0.0f
                && (!process->loadingAvailable || !process->loading)) {
                if (!integrityPassed) {
                    finishMessage = QStringLiteral("Finish rejected: ")
                        + process->blockedModule;
                } else {
                    QString saveError;

                    if (saveCompletedAttempt(
                            levelSelector->currentText(),
                            categorySelector->currentText(),
                            subcategorySelector->currentText(),
                            process->gameTime,
                            process->buildHash,
                            &saveError)) {
                        finishMessage = QStringLiteral("Finish saved: ")
                            + formatIgt(process->gameTime);
                    } else {
                        finishMessage = QStringLiteral("Finish not saved: ")
                            + saveError;
                    }
                }

                // Consume this completion even when integrity fails: a persistent
                // portal code must never be accepted after a Menu Hook is removed.
                finishArmed = false;
                finishHandled = true;
            }
            previousLevelCount =
                process->levelCountAvailable && std::isfinite(process->levelCount)
                    ? process->levelCount
                    : -1.0f;

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
            const QString activityStatus = !finishMessage.isEmpty()
                ? finishMessage
                : isInMainMenu ? QStringLiteral("Main Menu") : runStatus;

            status->setText(
                QStringLiteral(
                    "Selected speedrun:\n%1\n%2\n%3"
                    "\n\n%4\n\nIGT\n%5\n\n%6"
                    )
                    .arg(
                        levelSelector->currentText(),
                        rulesetName(categorySelector->currentText(), subcategorySelector->currentText()),
                        levelVerificationStatus,
                        activityStatus,
                        gameTimeStatus,
                        integrityStatus
                        )
                );
        };

    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &window, updateStatus);

    loginDialog.resize(380, 180);

    if (loginDialog.exec() != QDialog::Accepted) {
        return 0;
    }

    timer.start(100);
    updateStatus();

    window.resize(440, 460);
    window.show();

    return app.exec();
}