#include "tra_process.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QVector>

#include <cmath>
#include <cstring>
#include <utility>

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
// These three values describe TRA's own endscreen transition. The level
// counter changes only when the completed level is committed; the menu state
// distinguishes that endscreen from normal play. They are Steam-build only.
static constexpr DWORD_PTR kLevelCountRootAddress = 0x004553A8;
static constexpr DWORD_PTR kLevelCountOffset = 0x80;
static constexpr DWORD_PTR kMenuStateRootAddress = 0x004F7464;
static constexpr DWORD_PTR kMenuStateOffset = 0x20;
static constexpr DWORD_PTR kLoadingAddress = 0x00812C64;

// Known, read-only Anniversary Steam-build layout used to find the level's
// signal mesh and Lara's centre. These offsets are guarded by the executable
// hash below; TRASR never writes to the game process.
static constexpr DWORD_PTR kGameTrackerAddress = 0x00838330;
static constexpr DWORD_PTR kGameTrackerLevelOffset = 0x08;
static constexpr DWORD_PTR kGameTrackerPlayerOffset = 0x0C;
static constexpr DWORD_PTR kLevelTerrainOffset = 0x00;
static constexpr DWORD_PTR kTerrainSignalGroupOffset = 0x1C;
static constexpr DWORD_PTR kTerrainGroupMeshOffset = 0x38;
static constexpr DWORD_PTR kMeshPositionOffset = 0x20;
static constexpr DWORD_PTR kMeshVerticesOffset = 0x30;
static constexpr DWORD_PTR kMeshFacesOffset = 0x34;
static constexpr DWORD_PTR kMeshVertexTypeOffset = 0x40;
static constexpr DWORD_PTR kMeshFaceCountOffset = 0x44;
static constexpr DWORD_PTR kMeshVertexCountOffset = 0x46;
static constexpr DWORD_PTR kPlayerPositionOffset = 0x10;

struct RemoteVector3
{
    float x;
    float y;
    float z;
    float w;
};

struct RemoteSignalFace
{
    quint16 i0;
    quint16 i1;
    quint16 i2;
    quint8 adjacencyFlags;
    quint8 collisionFlags;
    quint16 id;
};

struct SignalTriangle
{
    RemoteVector3 a;
    RemoteVector3 b;
    RemoteVector3 c;
    quint16 id;
};

static_assert(sizeof(RemoteVector3) == 16);
static_assert(sizeof(RemoteSignalFace) == 10);

struct SignalDetectionCache
{
    DWORD processId = 0;
    DWORD_PTR meshAddress = 0;
    QVector<SignalTriangle> triangles;
    RemoteVector3 previousPlayerPosition{};
    bool hasPreviousPlayerPosition = false;
};

static SignalDetectionCache s_signalDetectionCache;

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

// Reads a primitive value from the target process without ever writing to it.
template <typename T>
static bool readRemoteValue(HANDLE processHandle, DWORD_PTR address, T *value)
{
    SIZE_T bytesRead = 0;
    return ReadProcessMemory(
               processHandle,
               reinterpret_cast<LPCVOID>(address),
               value,
               sizeof(T),
               &bytesRead
               ) && bytesRead == sizeof(T);
}

static bool readRemotePointer(
    HANDLE processHandle,
    DWORD_PTR address,
    DWORD_PTR *value
    )
{
    quint32 remotePointer = 0;

    if (!readRemoteValue(processHandle, address, &remotePointer)) {
        return false;
    }

    *value = static_cast<DWORD_PTR>(remotePointer);
    return remotePointer != 0;
}

static bool segmentIntersectsTriangle(
    const RemoteVector3 &start,
    const RemoteVector3 &end,
    const SignalTriangle &triangle,
    float *distance
    )
{
    const float directionX = end.x - start.x;
    const float directionY = end.y - start.y;
    const float directionZ = end.z - start.z;
    const float edge1X = triangle.b.x - triangle.a.x;
    const float edge1Y = triangle.b.y - triangle.a.y;
    const float edge1Z = triangle.b.z - triangle.a.z;
    const float edge2X = triangle.c.x - triangle.a.x;
    const float edge2Y = triangle.c.y - triangle.a.y;
    const float edge2Z = triangle.c.z - triangle.a.z;
    const float pX = directionY * edge2Z - directionZ * edge2Y;
    const float pY = directionZ * edge2X - directionX * edge2Z;
    const float pZ = directionX * edge2Y - directionY * edge2X;
    const float determinant = edge1X * pX + edge1Y * pY + edge1Z * pZ;

    if (std::fabs(determinant) < 0.0001f) {
        return false;
    }

    const float inverseDeterminant = 1.0f / determinant;
    const float startToAX = start.x - triangle.a.x;
    const float startToAY = start.y - triangle.a.y;
    const float startToAZ = start.z - triangle.a.z;
    const float u = (startToAX * pX + startToAY * pY + startToAZ * pZ)
        * inverseDeterminant;

    if (u < 0.0f || u > 1.0f) {
        return false;
    }

    const float qX = startToAY * edge1Z - startToAZ * edge1Y;
    const float qY = startToAZ * edge1X - startToAX * edge1Z;
    const float qZ = startToAX * edge1Y - startToAY * edge1X;
    const float qYValue = (directionX * qX + directionY * qY + directionZ * qZ)
        * inverseDeterminant;

    if (qYValue < 0.0f || u + qYValue > 1.0f) {
        return false;
    }

    const float hitDistance = (edge2X * qX + edge2Y * qY + edge2Z * qZ)
        * inverseDeterminant;

    if (hitDistance < 0.0f || hitDistance > 1.0f) {
        return false;
    }

    *distance = hitDistance;
    return true;
}

static bool cacheSignalTriangles(
    HANDLE processHandle,
    DWORD processId,
    DWORD_PTR meshAddress
    )
{
    DWORD_PTR verticesAddress = 0;
    DWORD_PTR facesAddress = 0;
    RemoteVector3 meshPosition{};
    quint16 vertexType = 0;
    quint16 faceCount = 0;
    quint16 vertexCount = 0;

    if (!readRemotePointer(
            processHandle,
            meshAddress + kMeshVerticesOffset,
            &verticesAddress)
        || !readRemotePointer(
            processHandle,
            meshAddress + kMeshFacesOffset,
            &facesAddress)
        || !readRemoteValue(
            processHandle,
            meshAddress + kMeshPositionOffset,
            &meshPosition)
        || !readRemoteValue(
            processHandle,
            meshAddress + kMeshVertexTypeOffset,
            &vertexType)
        || !readRemoteValue(
            processHandle,
            meshAddress + kMeshFaceCountOffset,
            &faceCount)
        || !readRemoteValue(
            processHandle,
            meshAddress + kMeshVertexCountOffset,
            &vertexCount)
        || vertexCount < 3 || faceCount == 0
        || vertexCount > 60000 || faceCount > 60000
        || (vertexType != 0 && vertexType != 1)) {
        return false;
    }

    const int vertexSize = vertexType == 0 ? 6 : 16;
    QByteArray vertices(vertexCount * vertexSize, Qt::Uninitialized);
    QByteArray faces(faceCount * static_cast<int>(sizeof(RemoteSignalFace)),
                     Qt::Uninitialized);
    SIZE_T bytesRead = 0;

    if (!ReadProcessMemory(
            processHandle,
            reinterpret_cast<LPCVOID>(verticesAddress),
            vertices.data(),
            static_cast<SIZE_T>(vertices.size()),
            &bytesRead)
        || bytesRead != static_cast<SIZE_T>(vertices.size())) {
        return false;
    }

    bytesRead = 0;
    if (!ReadProcessMemory(
            processHandle,
            reinterpret_cast<LPCVOID>(facesAddress),
            faces.data(),
            static_cast<SIZE_T>(faces.size()),
            &bytesRead)
        || bytesRead != static_cast<SIZE_T>(faces.size())) {
        return false;
    }

    auto vertexAt = [&vertices, vertexType, vertexSize, meshPosition](quint16 index) {
        RemoteVector3 vertex{0.0f, 0.0f, 0.0f, 0.0f};
        const char *source = vertices.constData() + index * vertexSize;

        if (vertexType == 0) {
            qint16 coordinates[3]{};
            std::memcpy(coordinates, source, sizeof(coordinates));
            vertex.x = static_cast<float>(coordinates[0]);
            vertex.y = static_cast<float>(coordinates[1]);
            vertex.z = static_cast<float>(coordinates[2]);
        } else {
            std::memcpy(&vertex, source, sizeof(RemoteVector3));
        }

        vertex.x += meshPosition.x;
        vertex.y += meshPosition.y;
        vertex.z += meshPosition.z;
        return vertex;
    };

    QVector<SignalTriangle> triangles;
    triangles.reserve(faceCount);

    for (quint16 i = 0; i < faceCount; ++i) {
        RemoteSignalFace face{};
        std::memcpy(
            &face,
            faces.constData() + i * sizeof(RemoteSignalFace),
            sizeof(face)
            );

        if (face.i0 >= vertexCount || face.i1 >= vertexCount
            || face.i2 >= vertexCount) {
            continue;
        }

        triangles.append(SignalTriangle{
            vertexAt(face.i0), vertexAt(face.i1), vertexAt(face.i2), face.id
            });
    }

    if (triangles.isEmpty()) {
        return false;
    }

    s_signalDetectionCache.processId = processId;
    s_signalDetectionCache.meshAddress = meshAddress;
    s_signalDetectionCache.triangles = std::move(triangles);
    s_signalDetectionCache.hasPreviousPlayerPosition = false;
    return true;
}

// Returns the signal face Lara crossed since the prior poll, if any. This
// reproduces the proven diagnostic geometry externally and remains read-only.
static bool readCrossedSignal(
    HANDLE processHandle,
    DWORD processId,
    int *crossedSignalId
    )
{
    *crossedSignalId = -1;
    DWORD_PTR levelAddress = 0;
    DWORD_PTR playerAddress = 0;
    DWORD_PTR terrainAddress = 0;
    DWORD_PTR signalGroupAddress = 0;
    DWORD_PTR meshAddress = 0;
    RemoteVector3 playerPosition{};

    if (!readRemotePointer(
            processHandle,
            kGameTrackerAddress + kGameTrackerLevelOffset,
            &levelAddress)
        || !readRemotePointer(
            processHandle,
            kGameTrackerAddress + kGameTrackerPlayerOffset,
            &playerAddress)
        || !readRemotePointer(
            processHandle,
            levelAddress + kLevelTerrainOffset,
            &terrainAddress)
        || !readRemotePointer(
            processHandle,
            terrainAddress + kTerrainSignalGroupOffset,
            &signalGroupAddress)
        || !readRemotePointer(
            processHandle,
            signalGroupAddress + kTerrainGroupMeshOffset,
            &meshAddress)
        || !readRemoteValue(
            processHandle,
            playerAddress + kPlayerPositionOffset,
            &playerPosition)) {
        // Loading can temporarily invalidate the tracker graph. Do not compare
        // the next stable position against a pre-transition player position.
        s_signalDetectionCache.hasPreviousPlayerPosition = false;
        return false;
    }

    if (s_signalDetectionCache.processId != processId
        || s_signalDetectionCache.meshAddress != meshAddress) {
        if (!cacheSignalTriangles(processHandle, processId, meshAddress)) {
            s_signalDetectionCache.hasPreviousPlayerPosition = false;
            return false;
        }
    }

    if (s_signalDetectionCache.hasPreviousPlayerPosition) {
        float firstHitDistance = 2.0f;

        for (const SignalTriangle &triangle : s_signalDetectionCache.triangles) {
            float hitDistance = 0.0f;
            if (segmentIntersectsTriangle(
                    s_signalDetectionCache.previousPlayerPosition,
                    playerPosition,
                    triangle,
                    &hitDistance
                    ) && hitDistance < firstHitDistance) {
                firstHitDistance = hitDistance;
                *crossedSignalId = triangle.id;
            }
        }
    }

    s_signalDetectionCache.previousPlayerPosition = playerPosition;
    s_signalDetectionCache.hasPreviousPlayerPosition = true;
    return true;
}
// Finds tra.exe and creates one current read-only snapshot for the UI.
std::optional<TraProcess> findTraProcess()
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

            float levelCount = 0.0f;
            bool levelCountAvailable = false;

            quint32 menuState = 0;
            bool menuStateAvailable = false;

            bool loading = false;
            bool loadingAvailable = false;

            QString loadedLevelCode;
            bool loadedLevelAvailable = false;

            QString currentPositionCode;
            bool currentPositionAvailable = false;

            int crossedSignalId = -1;
            bool signalCrossingAvailable = false;

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

                // The endscreen counter is separately verified for this Steam
                // build. A failed pointer read is treated as unavailable.
                if (buildHash == kSupportedSteamBuildSha256) {
                    DWORD_PTR levelCountAddress = 0;
                    DWORD_PTR menuStateAddress = 0;

                    levelCountAvailable =
                        readRemotePointer(
                            processHandle,
                            kLevelCountRootAddress,
                            &levelCountAddress
                            )
                        && readRemoteValue(
                            processHandle,
                            levelCountAddress + kLevelCountOffset,
                            &levelCount
                            );

                    menuStateAvailable =
                        readRemotePointer(
                            processHandle,
                            kMenuStateRootAddress,
                            &menuStateAddress
                            )
                        && readRemoteValue(
                            processHandle,
                            menuStateAddress + kMenuStateOffset,
                            &menuState
                            );

                    loadingAvailable = readRemoteValue(
                        processHandle,
                        kLoadingAddress,
                        &loading
                        );
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

                // Signal geometry is currently verified for the supported Steam build.
                if (buildHash == kSupportedSteamBuildSha256) {
                    signalCrossingAvailable = readCrossedSignal(
                        processHandle,
                        process.th32ProcessID,
                        &crossedSignalId
                        );
                }

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
                levelCount,
                levelCountAvailable,
                menuState,
                menuStateAvailable,
                loading,
                loadingAvailable,
                loadedLevelCode,
                loadedLevelAvailable,
                currentPositionCode,
                currentPositionAvailable,
                crossedSignalId,
                signalCrossingAvailable
            };

            break;
        } while (Process32NextW(snapshot, &process));
    }

    CloseHandle(snapshot);
    return result;
}
