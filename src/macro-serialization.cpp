#include "macro-serialization.h"

#include <QFile>
#include <QSaveFile>
#include <QJsonArray>
#include <QSet>
#include <QUuid>
#include "theme-helpers.h"

namespace {
QJsonObject encodeBinding(const MacroParameterBinding &b)
{
	return {{"sourceUuid", b.sourceUuid},
		{"filterUuid", b.filterUuid},
		{"key", b.key},
		{"domain", b.domain},
		{"type", b.type == MacroParameterType::Integer   ? "int"
			 : b.type == MacroParameterType::Float   ? "float"
			 : b.type == MacroParameterType::Boolean ? "bool"
								 : "text"}};
}
std::optional<MacroParameterBinding> decodeBinding(const QJsonValue &value)
{
	if (!value.isObject())
		return std::nullopt;
	const auto b = value.toObject();
	const QString domain = b["domain"].toString();
	const QString type = b["type"].toString();
	if ((b.contains("domain") && (!b["domain"].isString() || (!domain.isEmpty() && domain != "mixer"))) ||
	    (b.contains("filterUuid") && !b["filterUuid"].isString()) || b["sourceUuid"].toString().isEmpty() ||
	    b["key"].toString().isEmpty() || (type != "int" && type != "float" && type != "bool" && type != "text") ||
	    (type == "text" && !domain.isEmpty()))
		return std::nullopt;
	return MacroParameterBinding{b["sourceUuid"].toString(), b["filterUuid"].toString(), b["key"].toString(),
				     type == "int"     ? MacroParameterType::Integer
				     : type == "float" ? MacroParameterType::Float
				     : type == "bool"  ? MacroParameterType::Boolean
						       : MacroParameterType::Text,
				     domain};
}
} // namespace

QJsonObject MacroSerialization::toJsonObject(const MacroButtonModel &model)
{
	QJsonArray array;
	const auto buttons = model.buttons();
	for (const auto &b : buttons) {
		QJsonObject obj;
		obj["id"] = b.id;
		obj["label"] = b.label;
		if (!b.bottomLabel.isEmpty())
			obj["bottomLabel"] = b.bottomLabel;
		if (b.showsLiveCaption())
			obj["showValue"] = true;
		if (b.kind == MacroControlKind::XY && b.padSize() != 40)
			obj["xySize"] = b.padSize();
		// An older plugin must skip parameter controls, never execute them
		// as legacy scene-switch buttons.
		obj["type"] = b.kind == MacroControlKind::Action ? static_cast<int>(b.type) : -1;
		if (b.kind != MacroControlKind::Action) {
			obj["control"] = b.kind == MacroControlKind::Slider ? "slider"
					 : b.kind == MacroControlKind::Knob ? "knob"
					 : b.kind == MacroControlKind::Toggle
						 ? (b.toggleValues ? "state-toggle" : "toggle")
					 : b.kind == MacroControlKind::XY ? "xy"
									  : "text";
			obj["binding"] = encodeBinding(b.binding);
			if (b.kind == MacroControlKind::Toggle && b.toggleValues) {
				obj["toggleA"] = QJsonValue::fromVariant(b.toggleValues->a);
				obj["toggleB"] = QJsonValue::fromVariant(b.toggleValues->b);
			}
			if (b.kind == MacroControlKind::XY)
				obj["bindingY"] = encodeBinding(b.bindingY);
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
			const auto binding = decodeBinding(obj["binding"]);
			const bool preset = kind == "state-toggle";
			const MacroParameter::ToggleValues values{obj["toggleA"].toVariant(),
								  obj["toggleB"].toVariant()};
			const auto y = kind == "xy" ? decodeBinding(obj["bindingY"]) : std::nullopt;
			if (!binding ||
			    (kind != "slider" && kind != "knob" && kind != "toggle" && kind != "text" && kind != "xy" &&
			     !preset) ||
			    ((kind == "toggle") != (binding->type == MacroParameterType::Boolean)) ||
			    (!preset && ((kind == "text") != (binding->type == MacroParameterType::Text))) ||
			    (preset && !MacroParameter::validToggleValues(binding->type, values)) ||
			    (kind == "xy" && (!y || !MacroParameter::numericPair(*binding, *y)))) {
				if (errorMessage)
					*errorMessage = "Invalid or unsupported macro parameter binding";
				return false;
			}
			b.kind = kind == "slider"               ? MacroControlKind::Slider
				 : kind == "knob"               ? MacroControlKind::Knob
				 : (kind == "toggle" || preset) ? MacroControlKind::Toggle
				 : kind == "xy"                 ? MacroControlKind::XY
								: MacroControlKind::Text;
			b.binding = *binding;
			if (preset)
				b.toggleValues = values;
			if (y)
				b.bindingY = *y;
		} else {
			if (typeInt < 0 || typeInt > maxTypeValue)
				continue;
			b.type = static_cast<TimelineEventType>(typeInt);
		}
		b.bottomLabel = obj["bottomLabel"].toString();
		b.showValue = b.supportsLiveCaption() && obj["showValue"].toBool(false);
		b.xySize = obj["xySize"].toInt(40);
		b.xySize = b.padSize();
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
