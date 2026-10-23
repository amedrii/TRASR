#include "run_rules.h"

QString rulesetName(const QString &category, const QString &subcategory)
{
    return category + QStringLiteral(" — ") + subcategory;
}
// The level dropdown has a fixed order. Add verified TRA level-code mappings here.
// Unknown levels intentionally return empty until they have been tested.
QString expectedLevelCode(int levelIndex)
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

QString formatIgt(float seconds)
{
    const int totalSeconds = static_cast<int>(seconds);
    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60, 2, 10, QChar('0'))
        .arg(totalSeconds % 60, 2, 10, QChar('0'));
}

// A return value is present only after the corresponding completion portal has
// been observed and verified. Empty means automatic finish is not supported yet.
QString expectedEndTriggerCode(int levelIndex)
{
    switch (levelIndex) {
    // The alternate Mountain Caves route completes at Vilcabamba's verified
    // transition to The Lost Valley.
    case 0: return QStringLiteral("pu11");
    case 1: return QStringLiteral("pu11"); // City of Vilcabamba
    case 2: return QStringLiteral("pu16"); // The Lost Valley
    case 3: return QStringLiteral("cn3"); // Tomb of Qualopec
    case 4: return QStringLiteral("gr31"); // St. Francis Folly
    case 5: return QStringLiteral("gr18"); // The Coliseum
    case 7: return QStringLiteral("gr32"); // Tomb of Tihocan
    case 10: return QStringLiteral("cn7"); // Sanctuary of the Scion
    case 13: return QStringLiteral("cn8"); // Final Conflict
    case 14: return QStringLiteral("ma15"); // Croft Manor
    default: return {};
    }
}

// A signal is a level-local invisible trigger mesh. A value is added only after
// it has been observed with the separate diagnostic hook and verified in-game.
bool isExpectedEndSignal(int levelIndex, int signalId)
{
    switch (levelIndex) {
    case 0:
        return signalId == 23; // Mountain Caves normal completion

    default:
        return false;
    }
}
