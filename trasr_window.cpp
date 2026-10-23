#include <QApplication>
#include "attempt_log.h"
#include "tra_process.h"
#include "run_rules.h"
#include "run_monitor.h"
#include "trasr_window.h"
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
         runMonitor = RunMonitor()]() mutable {
            const RunSelection selection{
                levelSelector->currentIndex(),
                levelSelector->currentText(),
                categorySelector->currentText(),
                subcategorySelector->currentText()
            };

            status->setText(runMonitor.update(selection));
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