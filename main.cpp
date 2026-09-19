#include <QApplication>
#include "attempt_log.h"
#include <QComboBox>
#include <QCryptographicHash>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
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

#include <optional>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

// These addresses are only trusted for this exact Steam executable.
static const QString kSupportedSteamBuildSha256 =
    QStringLiteral(
        "7461663a67bc3b9e7324afbd1e0ac8d4e092bd7083ddf147231a42a790814b0e"
        );

static const QString kSupportedOriginalPcBuildSha256 =
    QStringLiteral(
        "5ca17c3a1e40d12a54392b984aa2df0d2ecfbc65c8eab0f06e361a195525e2ed"
        );

// Reverse-engineered memory addresses for the supported TRA build.
static constexpr DWORD_PTR kLaraHealthAddress = 0x00665460;
static constexpr DWORD_PTR kGameTimeAddress = 0x00665B10;
static constexpr DWORD_PTR kLoadedLevelAddress = 0x008AE384;
static constexpr DWORD_PTR kCurrentPositionAddress = 0x008AF450;

// A read-only snapshot of the running TRA process.
struct TraProcess
{
    DWORD processId;
    QString executablePath;
    QString buildHash;
    QString blockedModule;

    float laraHealth;
    bool healthAvailable;

    float gameTime;
    bool gameTimeAvailable;

    QString loadedLevelCode;
    bool loadedLevelAvailable;

    QString currentPositionCode;
    bool currentPositionAvailable;
};

static QString rulesetName(
    const QString &category,
    const QString &subcategory
    )
{
    return category + QStringLiteral(" — ") + subcategory;
}

// QSettings keys for a PB and the most recently confirmed attempt.
static QString bestIgtKey(
    const QString &level,
    const QString &category,
    const QString &subcategory
    )
{
    return QStringLiteral("results/")
    + level + QStringLiteral("/")
        + category + QStringLiteral("/")
        + subcategory + QStringLiteral("/bestIgt");
}

static QString lastIgtKey(
    const QString &level,
    const QString &category,
    const QString &subcategory
    )
{
    return QStringLiteral("results/")
    + level + QStringLiteral("/")
        + category + QStringLiteral("/")
        + subcategory + QStringLiteral("/lastIgt");
}

// The game executable rarely changes while TRASR is open, so cache its hash.
static QString sha256ForFile(const QString &filePath)
{
    static QString cachedPath;
    static QString cachedHash;

    if (filePath == cachedPath && !cachedHash.isEmpty()) {
        return cachedHash;
    }

    QFile file(filePath);

    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("Hash unavailable");
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);

    while (!file.atEnd()) {
        hash.addData(file.read(64 * 1024));
    }

    cachedPath = filePath;
    cachedHash = hash.result().toHex();

    return cachedHash;
}

// Ranked integrity check.
// This only lists modules loaded in TRA; TRASR never injects into or writes to it.
static QString blockedModuleName(DWORD processId)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
        processId
        );

    if (snapshot == INVALID_HANDLE_VALUE) {
        return {};
    }

    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);

    QString blockedModule;

    if (Module32FirstW(snapshot, &module)) {
        do {
            const QString moduleName =
                QString::fromWCharArray(module.szModule);

            if (moduleName.compare(
                    QStringLiteral("TRAE-Menu-Hook.asi"),
                    Qt::CaseInsensitive
                    ) == 0) {
                blockedModule = moduleName;
                break;
            }
        } while (Module32NextW(snapshot, &module));
    }

    CloseHandle(snapshot);
    return blockedModule;
}

// Reads TRA's fixed 32-byte ASCII level-state fields, such as "lc11".
// The extra byte in text ensures QString always receives a null-terminated string.
static bool readFixedGameString(
    HANDLE processHandle,
    DWORD_PTR address,
    QString *value
    )
{
    char text[33] = {};
    SIZE_T bytesRead = 0;

    const bool readSucceeded = ReadProcessMemory(
                                   processHandle,
                                   reinterpret_cast<LPCVOID>(address),
                                   text,
                                   32,
                                   &bytesRead
                                   ) && bytesRead == 32;

    if (!readSucceeded) {
        return false;
    }

    *value = QString::fromLatin1(text);
    return true;
}

// Finds tra.exe and creates one current read-only snapshot for the UI.
static std::optional<TraProcess> findTraProcess()
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);

    std::optional<TraProcess> result;

    if (Process32FirstW(snapshot, &process)) {
        do {
            const QString processName =
                QString::fromWCharArray(process.szExeFile);

            if (processName.compare(
                    QStringLiteral("tra.exe"),
                    Qt::CaseInsensitive
                    ) != 0) {
                continue;
            }

            QString executablePath = QStringLiteral("Path unavailable");
            QString buildHash = QStringLiteral("Hash unavailable");

            float laraHealth = 0.0f;
            bool healthAvailable = false;

            float gameTime = 0.0f;
            bool gameTimeAvailable = false;

            QString loadedLevelCode;
            bool loadedLevelAvailable = false;

            QString currentPositionCode;
            bool currentPositionAvailable = false;

            HANDLE processHandle = OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                FALSE,
                process.th32ProcessID
                );

            if (processHandle != nullptr) {
                wchar_t path[MAX_PATH];
                DWORD pathLength = MAX_PATH;

                if (QueryFullProcessImageNameW(
                        processHandle,
                        0,
                        path,
                        &pathLength)) {
                    executablePath =
                        QString::fromWCharArray(path, pathLength);
                }

                buildHash = sha256ForFile(executablePath);

                // Health and IGT addresses are only known for the supported build.
                if (buildHash == kSupportedSteamBuildSha256
                    || buildHash == kSupportedOriginalPcBuildSha256) {
                    SIZE_T bytesRead = 0;

                    healthAvailable = ReadProcessMemory(
                                          processHandle,
                                          reinterpret_cast<LPCVOID>(kLaraHealthAddress),
                                          &laraHealth,
                                          sizeof(laraHealth),
                                          &bytesRead
                                          ) && bytesRead == sizeof(laraHealth);

                    bytesRead = 0;

                    gameTimeAvailable = ReadProcessMemory(
                                            processHandle,
                                            reinterpret_cast<LPCVOID>(kGameTimeAddress),
                                            &gameTime,
                                            sizeof(gameTime),
                                            &bytesRead
                                            ) && bytesRead == sizeof(gameTime);
                }

                // These strings let TRASR distinguish selected/preloaded levels
                // from the level Lara is actually inside.
                loadedLevelAvailable = readFixedGameString(
                    processHandle,
                    kLoadedLevelAddress,
                    &loadedLevelCode
                    );

                currentPositionAvailable = readFixedGameString(
                    processHandle,
                    kCurrentPositionAddress,
                    &currentPositionCode
                    );

                CloseHandle(processHandle);
            }

            const QString blockedModule =
                blockedModuleName(process.th32ProcessID);

            result = TraProcess{
                process.th32ProcessID,
                executablePath,
                buildHash,
                blockedModule,
                laraHealth,
                healthAvailable,
                gameTime,
                gameTimeAvailable,
                loadedLevelCode,
                loadedLevelAvailable,
                currentPositionCode,
                currentPositionAvailable
            };

            break;
        } while (Process32NextW(snapshot, &process));
    }

    CloseHandle(snapshot);
    return result;
}

// The level dropdown has a fixed order. Add verified TRA level-code mappings here.
// Unknown levels intentionally return empty until they have been tested.
static QString expectedLevelCode(int levelIndex)
{
    switch (levelIndex) {
    case 0:
        return QStringLiteral("pu1"); // Mountain Caves

    case 1:
        return QStringLiteral("pu8"); // City of Vilcabamba

    case 2:
        return QStringLiteral("pu11"); // The Lost Valley

    case 3:
        return QStringLiteral("pu16"); // Tomb of Qualopec

    case 4:
        return QStringLiteral("gr1"); // St. Francis Folly

    case 5:
        return QStringLiteral("gr31"); // The Coliseum

    case 6:
        return QStringLiteral("gr18"); // Midas Palace

    case 7:
        return QStringLiteral("gr27"); // Tomb of Tihocan

    case 8:
        return QStringLiteral("eg1"); // Temple of Khamoon

    case 9:
        return QStringLiteral("eg11"); // Obelisk of Khamoon

    case 10:
        return QStringLiteral("eg20"); // Sanctuary of the Scion

    case 11:
        return QStringLiteral("lc1"); // Natla's Mines

    case 12:
        return QStringLiteral("lc11"); // The Great Pyramid

    case 13:
        return QStringLiteral("lc17"); // Final Conflict

    case 14:
        return QStringLiteral("ma1"); // Croft Manor

    default:
        return {};
    }
}

static QString formatIgt(float seconds)
{
    const int totalSeconds = static_cast<int>(seconds);

    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60, 2, 10, QChar('0'))
        .arg(totalSeconds % 60, 2, 10, QChar('0'));
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("TRASR"));
    QCoreApplication::setApplicationName(QStringLiteral("TRASR"));

    // QSettings stores selections and PBs between application launches.
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

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);

    auto *displayNameLabel = new QLabel(
        QStringLiteral("Display name")
        );

    auto *displayNameInput = new QLineEdit;
    displayNameInput->setPlaceholderText(
        QStringLiteral("Choose a name")
        );

    displayNameInput->setText(
        settings.value(QStringLiteral("displayName")).toString()
        );

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
        QStringLiteral("Speedrun.com: not linked")
        );

    speedrunStatus->setAlignment(Qt::AlignCenter);

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

    auto *queueStatus = new QLabel(
        QStringLiteral("Matchmaking: not queued")
        );
    queueStatus->setAlignment(Qt::AlignCenter);

    QFont statusFont;
    statusFont.setPointSize(16);
    statusFont.setBold(true);
    status->setFont(statusFont);

    auto *confirmButton = new QPushButton(
        QStringLiteral("Confirm finish")
        );
    confirmButton->setEnabled(false);

    // Save a changed ruleset and load its matching local PB.
    auto refreshSelection =
        [levelSelector, categorySelector, subcategorySelector,
         confirmButton]() {
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

            confirmButton->setProperty(
                "bestIgt",
                settings.value(
                            bestIgtKey(level, category, subcategory),
                            -1.0f
                            ).toFloat()
                );
        };

    refreshSelection();

    layout->addWidget(displayNameLabel);
    layout->addWidget(displayNameInput);
    layout->addWidget(speedrunApiKeyLabel);
    layout->addWidget(speedrunApiKeyInput);
    layout->addWidget(linkSpeedrunButton);
    layout->addWidget(speedrunStatus);
    layout->addWidget(levelLabel);
    layout->addWidget(levelSelector);
    layout->addWidget(categoryLabel);
    layout->addWidget(categorySelector);
    layout->addWidget(subcategoryLabel);
    layout->addWidget(subcategorySelector);
    layout->addWidget(serverStatus);
    layout->addWidget(randomQueueButton);
    layout->addWidget(rulesetQueueButton);
    layout->addWidget(queueStatus);
    layout->addWidget(status);
    layout->addWidget(confirmButton);

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
        linkSpeedrunButton,
        &QPushButton::clicked,
        &window,
        [networkManager, speedrunApiKeyInput, linkSpeedrunButton,
         speedrunStatus, displayNameInput]() {
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
                 speedrunStatus, displayNameInput]() {
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
                    settings.setValue(
                        QStringLiteral("displayName"),
                        displayName
                        );

                    displayNameInput->setText(displayName);
                    speedrunApiKeyInput->clear();

                    speedrunStatus->setText(
                        QStringLiteral("Speedrun.com: linked as %1")
                            .arg(displayName)
                        );

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
        [networkManager, displayNameInput, randomQueueButton,
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
         confirmButton, previousGameTime = -1.0f,
         unchangedPolls = 0]() mutable {
            const auto process = findTraProcess();

            if (!process.has_value()) {
                confirmButton->setEnabled(false);

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

            const QString selectedLevelCode =
                expectedLevelCode(levelSelector->currentIndex());

            // TRA may preload the next level before Lara has left the old level.
            // We require both codes to agree before calling a mapped level active.
            const bool expectedLevelIsActive =
                !selectedLevelCode.isEmpty()
                && process->loadedLevelAvailable
                && process->currentPositionAvailable
                && process->loadedLevelCode == selectedLevelCode
                && process->currentPositionCode == selectedLevelCode;

            const QString levelVerificationStatus =
                selectedLevelCode.isEmpty()
                    ? QStringLiteral("Level verification: mapping needed")
                    : expectedLevelIsActive
                          ? QStringLiteral("Level verification: passed")
                          : QStringLiteral("Level verification: failed");

            const float bestIgt =
                confirmButton->property("bestIgt").toFloat();

            const QString bestIgtStatus =
                bestIgt >= 0.0f
                    ? formatIgt(bestIgt)
                    : QStringLiteral("--:--");

            QString runStatus;

            if (!process->gameTimeAvailable) {
                runStatus = QStringLiteral("IGT unavailable");
                previousGameTime = -1.0f;
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

            // A frozen IGT can mean pause, loading, or finish. Manual confirmation
            // remains necessary until each level's real completion transition is
            // mapped and verified.
            const bool finishCandidate =
                process->gameTimeAvailable
                && process->gameTime > 0.0f
                && unchangedPolls > 20;

            const QVariant confirmedValue =
                confirmButton->property("confirmedCandidateIgt");

            const bool alreadyConfirmed =
                confirmedValue.isValid()
                && confirmedValue.toFloat() == process->gameTime;

            // TRAE-Menu-Hook.asi blocks attempt confirmation.
            const bool integrityPassed = process->blockedModule.isEmpty();

            confirmButton->setEnabled(
                finishCandidate
                && !alreadyConfirmed
                && integrityPassed
                );

            if (finishCandidate) {
                confirmButton->setProperty(
                    "candidateIgt",
                    process->gameTime
                    );

                confirmButton->setProperty(
                    "buildHash",
                    process->buildHash
                    );
            }

            const QString integrityStatus =
                integrityPassed
                    ? QStringLiteral("Integrity: passed")
                    : QStringLiteral("Integrity failure: ")
                          + process->blockedModule;

            if(loadedLevelStatus.length() == 5 ){
                status->setText(
                    QStringLiteral(
                        "Selected speedrun:\n%1\n%2\n\nIN MAIN MENU"
                        "\n\nPB IGT\n%3\n%4"
                        )
                        .arg(levelSelector->currentText())
                        .arg(rulesetName(
                            categorySelector->currentText(),
                            subcategorySelector->currentText()
                            ))
                        .arg(bestIgtStatus)
                        .arg(integrityStatus)
                    );
            }else{
                status->setText(
                    QStringLiteral(
                        "Selected speedrun:\n%1\n%2\n%3\n\nLoaded: %4\nPosition: %5"
                        "\n\n\nIGT\n%6\n\nPB IGT\n%7\n\n%8\n\n%9"
                        )
                        .arg(levelSelector->currentText())
                        .arg(rulesetName(
                            categorySelector->currentText(),
                            subcategorySelector->currentText()
                            ))
                        .arg(levelVerificationStatus)
                        .arg(loadedLevelStatus)
                        .arg(currentPositionStatus)
                        .arg(gameTimeStatus)
                        .arg(bestIgtStatus)
                        .arg(runStatus)
                        .arg(integrityStatus)
                    );
            }
        };

    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &window, updateStatus);

    // A confirmed finish writes a durable JSON attempt before it updates the
    // convenient local PB cache.
    QObject::connect(
        confirmButton,
        &QPushButton::clicked,
        &window,
        [levelSelector, categorySelector, subcategorySelector,
         confirmButton]() {
            const float candidateIgt =
                confirmButton->property("candidateIgt").toFloat();

            const QString level = levelSelector->currentText();
            const QString category = categorySelector->currentText();
            const QString subcategory = subcategorySelector->currentText();

            const QString buildHash =
                confirmButton->property("buildHash").toString();

            QString logError;

            if (!appendAttempt(
                    AttemptRecord{
                        level,
                        category,
                        subcategory,
                        candidateIgt,
                        buildHash
                    },
                    &logError)) {
                QMessageBox::warning(
                    confirmButton,
                    QStringLiteral("Attempt not saved"),
                    logError
                    );
                return;
            }

            // Prevent duplicate confirmation for the same frozen IGT value.
            confirmButton->setProperty(
                "confirmedCandidateIgt",
                candidateIgt
                );

            QSettings settings;

            const QString bestKey =
                bestIgtKey(level, category, subcategory);

            const float previousBest =
                settings.value(bestKey, -1.0f).toFloat();

            const bool isPersonalBest =
                previousBest < 0.0f || candidateIgt < previousBest;

            settings.setValue(
                lastIgtKey(level, category, subcategory),
                candidateIgt
                );

            if (isPersonalBest) {
                settings.setValue(bestKey, candidateIgt);
                confirmButton->setProperty("bestIgt", candidateIgt);
            }

            const QString message =
                isPersonalBest
                    ? QStringLiteral("New PB: ")
                    : QStringLiteral("Confirmed IGT: ");

            QMessageBox::information(
                nullptr,
                QStringLiteral("Finish confirmed"),
                message + formatIgt(candidateIgt)
                    + QStringLiteral("\n")
                    + level + QStringLiteral("\n")
                    + rulesetName(category, subcategory)
                );
        }
        );

    timer.start(100);
    updateStatus();

    window.resize(440, 460);
    window.show();

    return app.exec();
}