#include <QApplication>
#include "attempt_log.h"
#include "tra_process.h"
#include "run_rules.h"
#include "run_monitor.h"
#include "screen_completion.h"
#include "trasr_window.h"
#include <QComboBox>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QProcess>
#include <QSettings>
#include <QStringList>

#include <memory>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>

int runTrasrApplication(QApplication &app)
{

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

    auto *screenVerifierStatus = new QLabel(
        QStringLiteral("Screen verifier: starting after login")
        );
    screenVerifierStatus->setAlignment(Qt::AlignCenter);

    auto *opponentStatus = new QLabel(
        QStringLiteral("Opponent: waiting for match")
        );
    opponentStatus->setAlignment(Qt::AlignCenter);

    auto *readyButton = new QPushButton(
        QStringLiteral("Ready for automatic start")
        );
    readyButton->setEnabled(false);

    auto *autoStartStatus = new QLabel(
        QStringLiteral("Automatic start: join a match first")
        );
    autoStartStatus->setAlignment(Qt::AlignCenter);
    autoStartStatus->setWordWrap(true);

    auto *forfeitButton = new QPushButton(
        QStringLiteral("Forfeit match")
        );
    forfeitButton->setEnabled(false);

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

    auto setQueueUiState = [randomQueueButton, rulesetQueueButton,
                             cancelQueueButton, levelSelector,
                             categorySelector, subcategorySelector](
        bool canJoin, bool canCancel, bool canChangeRuleset
        ) {
            randomQueueButton->setEnabled(canJoin);
            rulesetQueueButton->setEnabled(canJoin);
            cancelQueueButton->setEnabled(canCancel);
            levelSelector->setEnabled(canChangeRuleset);
            categorySelector->setEnabled(canChangeRuleset);
            subcategorySelector->setEnabled(canChangeRuleset);
        };

    setQueueUiState(true, false, true);

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
    layout->addWidget(screenVerifierStatus);
    layout->addWidget(opponentStatus);
    layout->addWidget(readyButton);
    layout->addWidget(autoStartStatus);
    layout->addWidget(forfeitButton);
    layout->addWidget(randomQueueButton);
    layout->addWidget(rulesetQueueButton);
    layout->addWidget(cancelQueueButton);
    layout->addWidget(queueStatus);
    layout->addWidget(status);

    window.setCentralWidget(content);

    // Local development health check for the Node matchmaking server.
    auto *networkManager = new QNetworkAccessManager(&window);

    auto *screenDetector = new QProcess(&window);
    screenDetector->setProcessChannelMode(QProcess::MergedChannels);

    auto launchScreenDetector = [screenDetector, screenVerifierStatus]() {
        const QString level = screenDetector->property("selectedLevel").toString();
        const QString scriptPath = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("tools/level_end_detector.py"));

        if (level.isEmpty() || !QFileInfo::exists(scriptPath)) {
            screenVerifierStatus->setText(
                QStringLiteral("Screen verifier: unavailable")
                );
            return;
        }

        QStringList arguments{
            QStringLiteral("-3.13"),
            scriptPath,
            QStringLiteral("--level"), level,
            QStringLiteral("--event-file"), screenCompletionEventPath()
        };

        const QString tesseractPath =
            QStringLiteral("C:/Program Files/Tesseract-OCR/tesseract.exe");
        if (QFileInfo::exists(tesseractPath)) {
            arguments << QStringLiteral("--tesseract") << tesseractPath;
        }

        screenDetector->start(QStringLiteral("py"), arguments);
        screenVerifierStatus->setText(
            QStringLiteral("Screen verifier: watching %1").arg(level)
            );
    };

    auto restartScreenDetector = [screenDetector, launchScreenDetector](
        const QString &level
        ) {
            screenDetector->setProperty("selectedLevel", level);

            if (screenDetector->state() == QProcess::NotRunning) {
                launchScreenDetector();
                return;
            }

            screenDetector->setProperty("restartWhenStopped", true);
            screenDetector->terminate();
        };

    QObject::connect(
        screenDetector,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        &window,
        [screenDetector, screenVerifierStatus, launchScreenDetector](
            int, QProcess::ExitStatus
            ) {
                if (!screenDetector->property("restartWhenStopped").toBool()) {
                    screenVerifierStatus->setText(
                        QStringLiteral("Screen verifier: stopped")
                        );
                    return;
                }

                screenDetector->setProperty("restartWhenStopped", false);
                QTimer::singleShot(50, screenDetector, launchScreenDetector);
            }
        );

    QObject::connect(
        screenDetector,
        &QProcess::errorOccurred,
        &window,
        [screenVerifierStatus](QProcess::ProcessError) {
            screenVerifierStatus->setText(
                QStringLiteral("Screen verifier: unavailable")
                );
        }
        );

    auto submitOnlineResult = [networkManager, queueStatus, playerId](
        const AttemptRecord &attempt
        ) {
            QSettings settings;
            const QString sessionToken = settings.value(
                QStringLiteral("matchmaking/sessionToken")
                ).toString();

            if (sessionToken.isEmpty()) {
                queueStatus->setText(
                    QStringLiteral("Finish saved locally: login required for submission")
                    );
                return;
            }

            const QJsonObject body{
                {QStringLiteral("sessionToken"), sessionToken},
                {QStringLiteral("level"), attempt.level},
                {QStringLiteral("category"), attempt.category},
                {QStringLiteral("subcategory"), attempt.subcategory},
                {QStringLiteral("igtMilliseconds"),
                 static_cast<double>(std::round(attempt.igtSeconds * 1000.0f))},
                {QStringLiteral("buildHash"), attempt.buildHash}
            };

            QNetworkRequest request(
                QUrl(QStringLiteral("https://api.trsr.app/matches/")
                    + playerId + QStringLiteral("/result"))
                );
            request.setHeader(
                QNetworkRequest::ContentTypeHeader,
                QStringLiteral("application/json")
                );

            auto *reply = networkManager->post(
                request,
                QJsonDocument(body).toJson(QJsonDocument::Compact)
                );

            QObject::connect(
                reply,
                &QNetworkReply::finished,
                [reply, queueStatus, playerId]() {
                    const QJsonObject response =
                        QJsonDocument::fromJson(reply->readAll()).object();

                    if (reply->error() != QNetworkReply::NoError) {
                        const QString error =
                            response.value(QStringLiteral("error")).toString();
                        queueStatus->setText(
                            error.isEmpty()
                                ? QStringLiteral("Finish saved locally: online submission failed")
                                : QStringLiteral("Finish saved locally: %1").arg(error)
                            );
                    } else {
                        const QString winnerPlayerId =
                            response.value(QStringLiteral("winnerPlayerId")).toString();
                        if (!winnerPlayerId.isEmpty()) {
                            queueStatus->setText(
                                winnerPlayerId == playerId
                                    ? QStringLiteral("Finish submitted — You win!")
                                    : QStringLiteral("Finish submitted — Opponent wins")
                                );
                        } else {
                            queueStatus->setText(
                                response.value(QStringLiteral("status")).toString()
                                    == QStringLiteral("already_submitted")
                                    ? QStringLiteral("Finish was already submitted")
                                    : QStringLiteral("Finish submitted")
                                );
                        }
                    }

                    reply->deleteLater();
                }
                );
        };
    auto *matchPollTimer = new QTimer(&window);
    auto *matchPresenceTimer = new QTimer(&window);
    matchPresenceTimer->setInterval(2000);
    auto hasActiveMatch = std::make_shared<bool>(false);
    auto latestPresence = std::make_shared<RunPresence>();
    auto autoStartPresence = std::make_shared<QString>();
    auto autoStartNavigationScheduled = std::make_shared<bool>(false);
    auto autoStartLaunchScheduled = std::make_shared<bool>(false);
    auto autoStartAwaitingRun = std::make_shared<bool>(false);
    auto *menuNavigator = new QProcess(&window);
    menuNavigator->setProcessChannelMode(QProcess::SeparateChannels);
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

    auto navigatorOutput = std::make_shared<QByteArray>();
    QObject::connect(
        menuNavigator,
        &QProcess::readyReadStandardOutput,
        &window,
        [menuNavigator, navigatorOutput, autoStartPresence, autoStartStatus,
         readyButton, autoStartLaunchScheduled]() {
            *navigatorOutput += menuNavigator->readAllStandardOutput();
            while (true) {
                const int newline = navigatorOutput->indexOf('\n');
                if (newline < 0) {
                    break;
                }
                const QByteArray line = navigatorOutput->left(newline);
                navigatorOutput->remove(0, newline + 1);
                const QJsonObject event = QJsonDocument::fromJson(line).object();
                const QString name = event.value(QStringLiteral("event")).toString();
                if (name == QStringLiteral("menu_armed")) {
                    *autoStartPresence = QStringLiteral("armed");
                    *autoStartLaunchScheduled = false;
                    autoStartStatus->setText(
                        QStringLiteral("Automatic start: Final Conflict armed; waiting for opponent")
                        );
                } else if (name == QStringLiteral("menu_waiting_for_launch")) {
                    autoStartStatus->setText(
                        QStringLiteral("Automatic start: waiting for shared GO")
                        );
                } else if (name == QStringLiteral("menu_launch_sent")) {
                    autoStartPresence->clear();
                    autoStartStatus->setText(
                        QStringLiteral("Automatic start: launched; verifying IGT")
                        );
                } else if (name == QStringLiteral("menu_navigation_failed")) {
                    autoStartPresence->clear();
                    *autoStartLaunchScheduled = false;
                    readyButton->setEnabled(true);
                    autoStartStatus->setText(
                        QStringLiteral("Automatic start stopped: %1")
                            .arg(event.value(QStringLiteral("error")).toString())
                        );
                }
            }
        }
        );

    QObject::connect(
        menuNavigator,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        &window,
        [autoStartPresence, autoStartStatus, readyButton]() {
            if (*autoStartPresence == QStringLiteral("arming")) {
                autoStartPresence->clear();
                readyButton->setEnabled(true);
                autoStartStatus->setText(
                    QStringLiteral("Automatic start stopped before arming")
                    );
            }
        }
        );

    QObject::connect(
        readyButton,
        &QPushButton::clicked,
        &window,
        [menuNavigator, hasActiveMatch, levelSelector, autoStartPresence,
         autoStartNavigationScheduled, autoStartLaunchScheduled, autoStartStatus, readyButton]() {
            if (!*hasActiveMatch || menuNavigator->state() != QProcess::NotRunning) {
                return;
            }
            if (levelSelector->currentText() != QStringLiteral("Final Conflict")) {
                autoStartStatus->setText(
                    QStringLiteral("Automatic start currently supports Final Conflict only")
                    );
                return;
            }
            *autoStartPresence = QStringLiteral("ready");
            *autoStartNavigationScheduled = false;
            *autoStartLaunchScheduled = false;
            readyButton->setEnabled(false);
            autoStartStatus->setText(
                QStringLiteral("Ready: waiting for opponent. Keep Tomb Raider on Play Game.")
                );
        }
        );
    QObject::connect(
        forfeitButton,
        &QPushButton::clicked,
        &window,
        [networkManager, playerId, hasActiveMatch, autoStartPresence, menuNavigator,
         forfeitButton, readyButton, queueStatus, matchPresenceTimer, setQueueUiState, &window]() {
            if (!*hasActiveMatch) {
                return;
            }
            if (QMessageBox::question(
                    &window,
                    QStringLiteral("Forfeit match?"),
                    QStringLiteral("This immediately awards the match to your opponent."),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No
                    ) != QMessageBox::Yes) {
                return;
            }
            QSettings settings;
            const QString sessionToken = settings.value(
                QStringLiteral("matchmaking/sessionToken")).toString();
            if (sessionToken.isEmpty()) {
                queueStatus->setText(QStringLiteral("Forfeit failed: login required"));
                return;
            }
            forfeitButton->setEnabled(false);
            const QJsonObject body{{QStringLiteral("sessionToken"), sessionToken}};
            QNetworkRequest request(
                QUrl(QStringLiteral("https://api.trsr.app/matches/")
                    + playerId + QStringLiteral("/forfeit"))
                );
            request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
            auto *reply = networkManager->post(
                request, QJsonDocument(body).toJson(QJsonDocument::Compact));
            QObject::connect(
                reply, &QNetworkReply::finished,
                [reply, hasActiveMatch, autoStartPresence, menuNavigator, forfeitButton,
                 readyButton, queueStatus, matchPresenceTimer, setQueueUiState]() {
                    const QJsonObject response =
                        QJsonDocument::fromJson(reply->readAll()).object();
                    if (reply->error() != QNetworkReply::NoError) {
                        queueStatus->setText(QStringLiteral("Forfeit failed: server request failed"));
                        forfeitButton->setEnabled(true);
                        reply->deleteLater();
                        return;
                    }
                    *hasActiveMatch = false;
                    autoStartPresence->clear();
                    if (menuNavigator->state() != QProcess::NotRunning) {
                        menuNavigator->terminate();
                    }
                    matchPresenceTimer->stop();
                    readyButton->setEnabled(false);
                    forfeitButton->setEnabled(false);
                    setQueueUiState(true, false, true);
                    queueStatus->setText(
                        response.value(QStringLiteral("status")).toString() == QStringLiteral("forfeited")
                            ? QStringLiteral("Match forfeited — opponent wins")
                            : QStringLiteral("Match was already decided")
                        );
                    reply->deleteLater();
                }
                );
        }
        );

    QObject::connect(
        matchPresenceTimer,
        &QTimer::timeout,
        &window,
        [networkManager, playerId, opponentStatus, readyButton, forfeitButton, autoStartStatus, menuNavigator, hasActiveMatch, latestPresence, autoStartPresence, autoStartNavigationScheduled, autoStartLaunchScheduled, autoStartAwaitingRun]() {
            if (!*hasActiveMatch) {
                return;
            }
            readyButton->setEnabled(autoStartPresence->isEmpty()
                                    && menuNavigator->state() == QProcess::NotRunning);
            forfeitButton->setEnabled(true);

            QSettings settings;
            const QString sessionToken = settings.value(
                QStringLiteral("matchmaking/sessionToken")).toString();
            if (sessionToken.isEmpty()) {
                return;
            }

            const QJsonObject body{
                {QStringLiteral("sessionToken"), sessionToken},
                {QStringLiteral("state"), autoStartPresence->isEmpty() ? latestPresence->state : *autoStartPresence},
                {QStringLiteral("igtMilliseconds"),
                 latestPresence->igtSeconds < 0.0f
                     ? QJsonValue()
                     : QJsonValue(static_cast<double>(std::round(
                           latestPresence->igtSeconds * 1000.0f)))}
            };
            QNetworkRequest request(
                QUrl(QStringLiteral("https://api.trsr.app/matches/")
                    + playerId + QStringLiteral("/presence"))
                );
            request.setHeader(QNetworkRequest::ContentTypeHeader,
                              QStringLiteral("application/json"));
            auto *reply = networkManager->post(
                request, QJsonDocument(body).toJson(QJsonDocument::Compact));

            QObject::connect(
                reply,
                &QNetworkReply::finished,
                [reply, playerId, opponentStatus, autoStartStatus, menuNavigator, autoStartPresence, autoStartNavigationScheduled, autoStartLaunchScheduled, latestPresence, autoStartAwaitingRun]() {
                    if (reply->error() != QNetworkReply::NoError) {
                        opponentStatus->setText(
                            QStringLiteral("Opponent: status unavailable"));
                        reply->deleteLater();
                        return;
                    }

                    const QJsonObject response =
                        QJsonDocument::fromJson(reply->readAll()).object();
                    const QString winnerPlayerId =
                        response.value(QStringLiteral("winnerPlayerId")).toString();
                    if (!winnerPlayerId.isEmpty()) {
                        opponentStatus->setText(
                            winnerPlayerId == playerId
                                ? QStringLiteral("Result: You win")
                                : QStringLiteral("Result: Opponent wins")
                            );
                        reply->deleteLater();
                        return;
                    }
                    const QJsonArray players =
                        response.value(QStringLiteral("players")).toArray();
                    QString opponentState;
                    QJsonObject opponent;
                    for (const QJsonValue &player : players) {
                        const QJsonObject candidate = player.toObject();
                        if (candidate.value(QStringLiteral("playerId")).toString()
                            != playerId) {
                            opponent = candidate;
                            break;
                        }
                    }

                    if (opponent.isEmpty()) {
                        opponentStatus->setText(
                            QStringLiteral("Opponent: waiting for opponent"));
                    } else {
                        const QString state =
                            opponent.value(QStringLiteral("state")).toString();
                        opponentState = state;
                        const double milliseconds =
                            opponent.value(QStringLiteral("igtMilliseconds")).toDouble(-1.0);
                        QString detail;
                        if (state == QStringLiteral("ready")) {
                            detail = QStringLiteral("Ready");
                        } else if (state == QStringLiteral("arming")) {
                            detail = QStringLiteral("Navigating menus");
                        } else if (state == QStringLiteral("armed")) {
                            detail = QStringLiteral("Armed");
                        } else if (state == QStringLiteral("running")) {
                            detail = QStringLiteral("Running");
                        } else if (state == QStringLiteral("restarted")) {
                            detail = QStringLiteral("Restarted");
                        } else if (state == QStringLiteral("finished")) {
                            detail = QStringLiteral("Finished");
                        } else {
                            detail = QStringLiteral("Waiting to start");
                        }
                        if (milliseconds >= 0.0) {
                            detail += QStringLiteral(" — ")
                                + formatIgt(static_cast<float>(milliseconds / 1000.0));
                        }
                        opponentStatus->setText(
                            QStringLiteral("Opponent: %1").arg(detail));
                    }
                    if (*autoStartPresence == QStringLiteral("armed")
                        && opponentState == QStringLiteral("waiting")) {
                        menuNavigator->terminate();
                        autoStartPresence->clear();
                        *autoStartLaunchScheduled = false;
                        autoStartStatus->setText(
                            QStringLiteral("Run start cancelled: opponent did not finish preparing")
                            );
                    }

                    if (*autoStartPresence == QStringLiteral("ready")
                        && !*autoStartNavigationScheduled) {
                        const int delayMilliseconds = response.value(
                            QStringLiteral("navigationDelayMilliseconds")
                            ).toInt(-1);
                        if (delayMilliseconds >= 0) {
                            *autoStartNavigationScheduled = true;
                            autoStartStatus->setText(
                                QStringLiteral("Both players ready — focus Tomb Raider / Play Game in %1 seconds")
                                    .arg((delayMilliseconds + 999) / 1000)
                                );
                            QTimer::singleShot(
                                delayMilliseconds,
                                menuNavigator,
                                [menuNavigator, autoStartPresence, autoStartStatus]() {
                                    if (menuNavigator->state() != QProcess::NotRunning) {
                                        return;
                                    }
                                    const QString scriptPath = QDir(QCoreApplication::applicationDirPath())
                                        .filePath(QStringLiteral("tools/menu_navigator.py"));
                                    if (!QFileInfo::exists(scriptPath)) {
                                        autoStartPresence->clear();
                                        autoStartStatus->setText(
                                            QStringLiteral("Run failed to start: navigator file is missing")
                                            );
                                        return;
                                    }
                                    *autoStartPresence = QStringLiteral("arming");
                                    autoStartStatus->setText(
                                        QStringLiteral("Automatic start: verifying focus and navigating menus…")
                                        );
                                    QStringList arguments{
                                        QStringLiteral("-3.13"), scriptPath,
                                        QStringLiteral("--level"), QStringLiteral("Final Conflict"),
                                        QStringLiteral("--wait-for-launch")
                                    };
                                    const QString tesseractPath =
                                        QStringLiteral("C:/Program Files/Tesseract-OCR/tesseract.exe");
                                    if (QFileInfo::exists(tesseractPath)) {
                                        arguments << QStringLiteral("--tesseract") << tesseractPath;
                                    }
                                    menuNavigator->start(QStringLiteral("py"), arguments);
                                }
                                );
                        }
                    }

                    if (*autoStartPresence == QStringLiteral("armed")
                        && !*autoStartLaunchScheduled) {
                        const int delayMilliseconds = response.value(
                            QStringLiteral("startDelayMilliseconds")
                            ).toInt(-1);
                        if (delayMilliseconds >= 0) {
                            *autoStartLaunchScheduled = true;
                            autoStartStatus->setText(
                                QStringLiteral("Automatic start: both players armed — GO in %1 seconds")
                                    .arg((delayMilliseconds + 999) / 1000)
                                );
                            QTimer::singleShot(
                                delayMilliseconds,
                                menuNavigator,
                                [menuNavigator, autoStartStatus, latestPresence, autoStartAwaitingRun]() {
                                    if (menuNavigator->state() != QProcess::NotRunning) {
                                        autoStartStatus->setText(
                                            QStringLiteral("Automatic start: GO!")
                                            );
                                        menuNavigator->write("{\"command\":\"launch\"}\n");
                                        *autoStartAwaitingRun = true;
                                        QTimer::singleShot(
                                            20000,
                                            menuNavigator,
                                            [autoStartStatus, latestPresence, autoStartAwaitingRun]() {
                                                if (!*autoStartAwaitingRun) {
                                                    return;
                                                }
                                                *autoStartAwaitingRun = false;
                                                if (latestPresence->state == QStringLiteral("running")) {
                                                    autoStartStatus->setText(
                                                        QStringLiteral("Automatic start: run started")
                                                        );
                                                } else {
                                                    autoStartStatus->setText(
                                                        QStringLiteral("Run failed to start: assigned level did not load")
                                                        );
                                                }
                                            }
                                            );
                                    }
                                }
                                );
                        }
                    }
                    reply->deleteLater();
                }
                );
        }
        );
    QObject::connect(
        matchPollTimer,
        &QTimer::timeout,
        &window,
        [networkManager, matchPollTimer, matchPresenceTimer, queueStatus, setQueueUiState,
         levelSelector, categorySelector, subcategorySelector, playerId, hasActiveMatch]() {
            auto *reply = networkManager->get(QNetworkRequest(QUrl(
                QStringLiteral("https://api.trsr.app/matches/") + playerId)));
            QObject::connect(
                reply, &QNetworkReply::finished,
                [reply, matchPollTimer, matchPresenceTimer, queueStatus, setQueueUiState,
                 levelSelector, categorySelector, subcategorySelector, hasActiveMatch]() {
                    if (reply->error() != QNetworkReply::NoError) {
                        reply->deleteLater();
                        return;
                    }
                    const QJsonObject response =
                        QJsonDocument::fromJson(reply->readAll()).object();
                    if (response.value(QStringLiteral("status")).toString()
                        != QStringLiteral("matched")) {
                        reply->deleteLater();
                        return;
                    }
                    const QJsonObject match =
                        response.value(QStringLiteral("match")).toObject();
                    levelSelector->setCurrentText(match.value(QStringLiteral("level")).toString());
                    categorySelector->setCurrentText(match.value(QStringLiteral("category")).toString());
                    subcategorySelector->setCurrentText(match.value(QStringLiteral("subcategory")).toString());
                    queueStatus->setText(
                        QStringLiteral("Match found: %1 — %2 — %3")
                            .arg(match.value(QStringLiteral("level")).toString())
                            .arg(match.value(QStringLiteral("category")).toString())
                            .arg(match.value(QStringLiteral("subcategory")).toString()));
                    matchPollTimer->stop();
                    *hasActiveMatch = true;
                    matchPresenceTimer->start();
                    setQueueUiState(false, false, false);
                    reply->deleteLater();
                });
        });

    QObject::connect(
        cancelQueueButton, &QPushButton::clicked, &window,
        [networkManager, matchPollTimer, queueStatus, setQueueUiState, playerId]() {
            QSettings settings;
            const QString sessionToken = settings.value(
                QStringLiteral("matchmaking/sessionToken")).toString();
            if (sessionToken.isEmpty()) {
                queueStatus->setText(QStringLiteral("Matchmaking: link Speedrun.com first"));
                return;
            }
            setQueueUiState(false, false, false);
            const QJsonObject requestBody{
                {QStringLiteral("playerId"), playerId},
                {QStringLiteral("sessionToken"), sessionToken}
            };
            QNetworkRequest request(QUrl(QStringLiteral("https://api.trsr.app/queue/cancel")));
            request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
            auto *reply = networkManager->post(
                request, QJsonDocument(requestBody).toJson(QJsonDocument::Compact));
            QObject::connect(
                reply, &QNetworkReply::finished,
                [reply, matchPollTimer, queueStatus, setQueueUiState]() {
                    if (reply->error() != QNetworkReply::NoError) {
                        queueStatus->setText(QStringLiteral("Matchmaking: could not cancel queue"));
                        setQueueUiState(false, true, false);
                        reply->deleteLater();
                        return;
                    }
                    matchPollTimer->stop();
                    queueStatus->setText(QStringLiteral("Matchmaking: queue cancelled"));
                    setQueueUiState(true, false, true);
                    reply->deleteLater();
                });
        });

    QObject::connect(
        randomQueueButton, &QPushButton::clicked, &window,
        [networkManager, queueStatus, setQueueUiState, levelSelector,
         categorySelector, subcategorySelector, matchPollTimer, matchPresenceTimer,
         playerId, hasActiveMatch]() {
            QSettings settings;
            const QString sessionToken = settings.value(
                QStringLiteral("matchmaking/sessionToken")).toString();
            if (sessionToken.isEmpty()) {
                queueStatus->setText(QStringLiteral("Matchmaking: link Speedrun.com first"));
                return;
            }
            setQueueUiState(false, false, false);
            queueStatus->setText(QStringLiteral("Matchmaking: joining random queue..."));
            const QJsonObject requestBody{
                {QStringLiteral("playerId"), playerId},
                {QStringLiteral("sessionToken"), sessionToken},
                {QStringLiteral("queueType"), QStringLiteral("random")}
            };
            QNetworkRequest request(QUrl(QStringLiteral("https://api.trsr.app/queue")));
            request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
            auto *reply = networkManager->post(
                request, QJsonDocument(requestBody).toJson(QJsonDocument::Compact));
            QObject::connect(
                reply, &QNetworkReply::finished,
                [reply, queueStatus, setQueueUiState, levelSelector,
                 categorySelector, subcategorySelector, matchPollTimer, matchPresenceTimer,
                 hasActiveMatch]() {
                    if (reply->error() != QNetworkReply::NoError) {
                        queueStatus->setText(QStringLiteral("Matchmaking: server request failed"));
                        setQueueUiState(true, false, true);
                        reply->deleteLater();
                        return;
                    }
                    const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
                    const QString responseStatus = response.value(QStringLiteral("status")).toString();
                    if (responseStatus == QStringLiteral("waiting")) {
                        queueStatus->setText(QStringLiteral("Matchmaking: waiting for opponent"));
                        setQueueUiState(false, true, false);
                        matchPollTimer->start();
                    } else if (responseStatus == QStringLiteral("matched")) {
                        const QJsonObject match = response.value(QStringLiteral("match")).toObject();
                        levelSelector->setCurrentText(match.value(QStringLiteral("level")).toString());
                        categorySelector->setCurrentText(match.value(QStringLiteral("category")).toString());
                        subcategorySelector->setCurrentText(match.value(QStringLiteral("subcategory")).toString());
                        queueStatus->setText(
                            QStringLiteral("Match found: %1 — %2 — %3")
                                .arg(match.value(QStringLiteral("level")).toString())
                                .arg(match.value(QStringLiteral("category")).toString())
                                .arg(match.value(QStringLiteral("subcategory")).toString()));
                        matchPollTimer->stop();
                        *hasActiveMatch = true;
                        matchPresenceTimer->start();
                        setQueueUiState(false, false, false);
                    } else {
                        queueStatus->setText(QStringLiteral("Matchmaking: ")
                            + response.value(QStringLiteral("error")).toString());
                        setQueueUiState(true, false, true);
                    }
                    reply->deleteLater();
                });
        });

    QObject::connect(
        rulesetQueueButton, &QPushButton::clicked, &window,
        [networkManager, queueStatus, setQueueUiState, levelSelector,
         categorySelector, subcategorySelector, matchPollTimer, matchPresenceTimer,
         playerId, hasActiveMatch]() {
            QSettings settings;
            const QString sessionToken = settings.value(
                QStringLiteral("matchmaking/sessionToken")).toString();
            if (sessionToken.isEmpty()) {
                queueStatus->setText(QStringLiteral("Matchmaking: link Speedrun.com first"));
                return;
            }
            setQueueUiState(false, false, false);
            queueStatus->setText(
                QStringLiteral("Matchmaking: joining selected ruleset queue..."));
            const QJsonObject requestBody{
                {QStringLiteral("playerId"), playerId},
                {QStringLiteral("sessionToken"), sessionToken},
                {QStringLiteral("queueType"), QStringLiteral("ruleset")},
                {QStringLiteral("category"), categorySelector->currentText()},
                {QStringLiteral("subcategory"), subcategorySelector->currentText()}
            };
            QNetworkRequest request(QUrl(QStringLiteral("https://api.trsr.app/queue")));
            request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
            auto *reply = networkManager->post(
                request, QJsonDocument(requestBody).toJson(QJsonDocument::Compact));
            QObject::connect(
                reply, &QNetworkReply::finished,
                [reply, queueStatus, setQueueUiState, levelSelector,
                 categorySelector, subcategorySelector, matchPollTimer, matchPresenceTimer,
                 hasActiveMatch]() {
                    if (reply->error() != QNetworkReply::NoError) {
                        queueStatus->setText(QStringLiteral("Matchmaking: server request failed"));
                        setQueueUiState(true, false, true);
                        reply->deleteLater();
                        return;
                    }
                    const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
                    const QString responseStatus = response.value(QStringLiteral("status")).toString();
                    if (responseStatus == QStringLiteral("waiting")) {
                        queueStatus->setText(
                            QStringLiteral("Matchmaking: waiting for ruleset opponent"));
                        setQueueUiState(false, true, false);
                        matchPollTimer->start();
                    } else if (responseStatus == QStringLiteral("matched")) {
                        const QJsonObject match = response.value(QStringLiteral("match")).toObject();
                        levelSelector->setCurrentText(match.value(QStringLiteral("level")).toString());
                        categorySelector->setCurrentText(match.value(QStringLiteral("category")).toString());
                        subcategorySelector->setCurrentText(match.value(QStringLiteral("subcategory")).toString());
                        queueStatus->setText(
                            QStringLiteral("Match found: %1 — %2 — %3")
                                .arg(match.value(QStringLiteral("level")).toString())
                                .arg(match.value(QStringLiteral("category")).toString())
                                .arg(match.value(QStringLiteral("subcategory")).toString()));
                        matchPollTimer->stop();
                        *hasActiveMatch = true;
                        matchPresenceTimer->start();
                        setQueueUiState(false, false, false);
                    } else {
                        queueStatus->setText(QStringLiteral("Matchmaking: ")
                            + response.value(QStringLiteral("error")).toString());
                        setQueueUiState(true, false, true);
                    }
                    reply->deleteLater();
                });
        });
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
    auto runMonitor = std::make_shared<RunMonitor>();
    auto updateStatus =
        [status, levelSelector, categorySelector, subcategorySelector,
         submitOnlineResult, runMonitor, latestPresence]() {
            const RunSelection selection{
                levelSelector->currentIndex(),
                levelSelector->currentText(),
                categorySelector->currentText(),
                subcategorySelector->currentText()
            };

            status->setText(runMonitor->update(selection));
            *latestPresence = runMonitor->presence();
            if (const auto savedAttempt = runMonitor->takeSavedAttempt()) {
                submitOnlineResult(*savedAttempt);
            }
        };
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &window, updateStatus);

    loginDialog.resize(380, 180);

    if (loginDialog.exec() != QDialog::Accepted) {
        return 0;
    }

    QObject::connect(
        levelSelector,
        &QComboBox::currentTextChanged,
        &window,
        restartScreenDetector
        );

    restartScreenDetector(levelSelector->currentText());
    timer.start(100);
    updateStatus();

    window.resize(440, 480);
    window.show();

    return app.exec();
}
