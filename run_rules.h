#pragma once

#include <QString>

QString rulesetName(const QString &category, const QString &subcategory);
QString expectedLevelCode(int levelIndex);
QString formatIgt(float seconds);
QString expectedEndTriggerCode(int levelIndex);
bool isExpectedEndSignal(int levelIndex, int signalId);