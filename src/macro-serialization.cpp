#include "macro-serialization.h"

#include <QFile>
#include <QSaveFile>
#include <QJsonArray>
#include <QSet>
#include <QUuid>
#include "theme-helpers.h"

QJsonObject MacroSerialization::toJsonObject(const MacroButtonModel &model)
{
	QJsonArray array;
	const auto buttons = model.buttons();
	for (const auto &b : buttons) {
		QJsonObject obj;
		obj["id"] = b.id;
		obj["label"] = b.label;
		// An older plugin must skip parameter controls, never execute them
		// as legacy scene-switch buttons.
		obj["type"] = b.kind == MacroControlKind::Action ? static_cast<int>(b.type) : -1;
		if (b.kind != MacroControlKind::Action) {
			obj["control"] = b.kind == MacroControlKind::Slider   ? "slider"
					 : b.kind == MacroControlKind::Knob   ? "knob"
					 : b.kind == MacroControlKind::Toggle ? "toggle"
									      : "text";
			obj["binding"] =
				QJsonObject{{"sourceUuid", b.binding.sourceUuid},
					    {"filterUuid", b.binding.filterUuid},
					    {"key", b.binding.key},
					    {"domain", b.binding.domain},
					    {"type", b.binding.type == MacroParameterType::Integer   ? "int"
						     : b.binding.type == MacroParameterType::Float   ? "float"
						     : b.binding.type == MacroParameterType::Boolean ? "bool"
												     : "text"}};
		}
		obj["source"] = b.sourceName;
		if (!b.filterName.isEmpty())
			obj["filter"] = b.filterName;
		if (!b.data.isEmpty())
			obj["data"] = b.data;
		obj["color"] = b.color.name();
		array.append(obj);
	}
	QJsonObject root;
	root["buttons"] = array;
	root["version"] = 1;
	return root;
}

bool MacroSerialization::fromJsonDocument(const QJsonDocument &doc, MacroButtonModel &model, QString *errorMessage)
{
	if (!doc.isObject()) {
		if (errorMessage)
			*errorMessage = "Macro buttons JSON must be an object";
		return false;
	}
	const QJsonObject root = doc.object();
	if (!root["buttons"].isArray()) {
		if (errorMessage)
			*errorMessage = "Macro buttons JSON requires a buttons array";
		return false;
	}
	const QJsonArray array = root["buttons"].toArray();
	QSet<QString> seen;
	QList<MacroButton> buttons;
	constexpr int maxTypeValue = static_cast<int>(TimelineEventType::SetParameterValue);
	for (const auto val : array) {
		const QJsonObject obj = val.toObject();
		MacroButton b;
		b.id = obj["id"].toString();
		if (b.id.isEmpty() || seen.contains(b.id)) {
			b.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		}
		seen.insert(b.id);
		b.label = obj["label"].toString();
		const int typeInt = obj["type"].toInt(-1);
		if (obj.contains("control")) {
			const QString kind = obj["control"].toString();
			const auto binding = obj["binding"].toObject();
			const QString type = binding["type"].toString();
			const QString domain = binding["domain"].toString();
			if ((binding.contains("domain") &&
			     (!binding["domain"].isString() || (!domain.isEmpty() && domain != "mixer"))) ||
			    (kind != "slider" && kind != "knob" && kind != "toggle" && kind != "text") ||
			    (type != "int" && type != "float" && type != "bool" && type != "text") ||
			    ((kind == "toggle") != (type == "bool")) || ((kind == "text") != (type == "text")) ||
			    (type == "text" && !domain.isEmpty()) || binding["sourceUuid"].toString().isEmpty() ||
			    binding["key"].toString().isEmpty()) {
				if (errorMessage)
					*errorMessage = "Invalid or unsupported macro parameter binding";
				return false;
			}
			b.kind = kind == "slider"   ? MacroControlKind::Slider
				 : kind == "knob"   ? MacroControlKind::Knob
				 : kind == "toggle" ? MacroControlKind::Toggle
						    : MacroControlKind::Text;
			b.binding = {binding["sourceUuid"].toString(), binding["filterUuid"].toString(),
				     binding["key"].toString(),
				     type == "int"     ? MacroParameterType::Integer
				     : type == "float" ? MacroParameterType::Float
				     : type == "bool"  ? MacroParameterType::Boolean
						       : MacroParameterType::Text,
				     domain};
		} else {
			if (typeInt < 0 || typeInt > maxTypeValue)
				continue;
			b.type = static_cast<TimelineEventType>(typeInt);
		}
		b.sourceName = obj["source"].toString();
		b.filterName = obj["filter"].toString();
		b.data = obj["data"].toString();
		if (obj.contains("color")) {
			b.color = QColor(obj["color"].toString());
		} else {
			b.color = ThemeIcons::getDefaultColor(b.type);
		}
		buttons.append(b);
	}
	model.setButtons(buttons);
	emit model.modelChanged();
	return true;
}

bool MacroSerialization::saveToFile(const QString &filePath, const QJsonObject &root, QString *errorMessage)
{
	QSaveFile file(filePath);
	if (!file.open(QIODevice::WriteOnly)) {
		if (errorMessage)
			*errorMessage = "Failed to open macro file for writing: " + file.errorString();
		return false;
	}
	const QJsonDocument doc(root);
	const QByteArray json = doc.toJson();
	if (file.write(json) != json.size()) {
		if (errorMessage)
			*errorMessage = "Failed to write macro JSON";
		return false;
	}
	if (!file.commit()) {
		if (errorMessage)
			*errorMessage = "Failed to commit and finalize save: " + file.errorString();
		return false;
	}
	return true;
}

bool MacroSerialization::loadFromFile(const QString &filePath, QJsonDocument &outDoc, QString *errorMessage)
{
	QFile file(filePath);
	if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
		if (errorMessage)
			*errorMessage = "Failed to open macro file";
		return false;
	}
	QJsonParseError parseError;
	outDoc = QJsonDocument::fromJson(file.readAll(), &parseError);
	file.close();
	if (parseError.error != QJsonParseError::NoError) {
		if (errorMessage)
			*errorMessage = parseError.errorString();
		return false;
	}
	return true;
}
