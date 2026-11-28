#pragma once

#include <QJsonObject>
#include <QJsonDocument>
#include <QString>
#include "macro-button-model.h"

struct MacroStripDefinition {
	QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	QString name = QStringLiteral("Macro Strip");
	bool vertical = false;
	QList<MacroButton> buttons;
};

class MacroSerialization {
public:
	static QJsonObject toJsonObject(const MacroButtonModel &model);
	static QJsonObject encodeStrips(const QList<MacroStripDefinition> &strips, int active);
	static bool decodeStrips(const QJsonObject &root, QList<MacroStripDefinition> &strips, int &active,
				 QString *errorMessage = nullptr);

	// Returns true on success. On failure, sets *errorMessage and the model is left empty.
	static bool fromJsonDocument(const QJsonDocument &doc, MacroButtonModel &model,
				     QString *errorMessage = nullptr);

	static bool saveToFile(const QString &filePath, const QJsonObject &root, QString *errorMessage = nullptr);

	static bool loadFromFile(const QString &filePath, QJsonDocument &outDoc, QString *errorMessage = nullptr);
};
