#pragma once

#include <QJsonObject>
#include <QJsonDocument>
#include <QString>
#include "macro-button-model.h"

class MacroSerialization {
public:
	static QJsonObject toJsonObject(const MacroButtonModel &model);

	// Returns true on success. On failure, sets *errorMessage and the model is left empty.
	static bool fromJsonDocument(const QJsonDocument &doc, MacroButtonModel &model,
				 QString *errorMessage = nullptr);

	static bool saveToFile(const QString &filePath, const QJsonObject &root,
			       QString *errorMessage = nullptr);

	static bool loadFromFile(const QString &filePath, QJsonDocument &outDoc,
				QString *errorMessage = nullptr);
};