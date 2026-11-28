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
		if (b.gapBefore > 0)
			obj["gapBefore"] = qBound(0, b.gapBefore, MacroButton::MaxGap);
		if (!b.bottomLabel.isEmpty())
			obj["bottomLabel"] = b.bottomLabel;
		if (b.showsLiveCaption())
			obj["showValue"] = true;
		if (b.kind == MacroControlKind::XY && b.padSize() != 40)
			obj["xySize"] = b.padSize();
		// An older plugin must skip parameter controls, never execute them
		// as legacy scene-switch buttons.
		obj["type"] = b.kind == MacroControlKind::Action ? static_cast<int>(b.type) : -1;
		if (b.kind == MacroControlKind::Slider && b.orientation != MacroOrientation::FollowStrip)
			obj["orientation"] = b.orientation == MacroOrientation::Vertical ? "vertical" : "horizontal";
		if (b.kind == MacroControlKind::ButtonGrid) {
			obj["control"] = "button-grid";
			QJsonArray cells;
			for (size_t i = 0; i < 4; ++i) {
				if (i >= b.gridActions.size() || b.gridActions[i].id.isEmpty() ||
				    b.gridActions[i].kind != MacroControlKind::Action) {
					cells.append(QJsonValue());
					continue;
				}
				MacroButtonModel cell;
				cell.setButtons({b.gridActions[i]});
				cells.append(toJsonObject(cell)["buttons"].toArray().first());
			}
			obj["slots"] = cells;
		} else if (b.kind != MacroControlKind::Action) {
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
		if (obj.contains("gapBefore")) {
			b.gapBefore = obj["gapBefore"].toInt(-1);
			if (b.gapBefore < 0 || b.gapBefore > MacroButton::MaxGap) {
				if (errorMessage)
					*errorMessage = "Invalid macro control spacing";
				return false;
			}
		}
		const int typeInt = obj["type"].toInt(-1);
		if (obj["control"].toString() == "button-grid") {
			const auto cells = obj["slots"].toArray();
			if (cells.size() != 4 || typeInt != -1) {
				if (errorMessage)
					*errorMessage = "Button grid requires four action slots";
				return false;
			}
			b.kind = MacroControlKind::ButtonGrid;
			for (const auto &value : cells) {
				MacroButton cell;
				if (value.isNull()) {
					cell.id.clear();
				} else {
					const auto action = value.toObject();
					const int type = action["type"].toInt(-1);
					if (!value.isObject() || action.contains("control") || type < 0 ||
					    type > maxTypeValue) {
						if (errorMessage)
							*errorMessage = "Invalid action in button grid";
						return false;
					}
					MacroButtonModel decoded;
					if (!fromJsonDocument(
						    QJsonDocument(QJsonObject{{"buttons", QJsonArray{action}}}),
						    decoded, errorMessage) ||
					    decoded.count() != 1)
						return false;
					cell = decoded.buttons().first();
					if (seen.contains(cell.id))
						cell.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
					seen.insert(cell.id);
				}
				b.gridActions.push_back(cell);
			}
		} else if (obj.contains("control")) {
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
		if (b.kind == MacroControlKind::Slider && obj.contains("orientation")) {
			const auto direction = obj["orientation"].toString();
			if (direction != "horizontal" && direction != "vertical") {
				if (errorMessage)
					*errorMessage = "Invalid slider orientation";
				return false;
			}
			b.orientation = direction == "vertical" ? MacroOrientation::Vertical
								: MacroOrientation::Horizontal;
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

QJsonObject MacroSerialization::encodeStrips(const QList<MacroStripDefinition> &strips, int active,
					     const QList<MacroStripDefinition> &individual)
{
	auto encode = [](const MacroStripDefinition &strip) {
		MacroButtonModel model;
		model.setButtons(strip.buttons);
		return toJsonObject(model);
	};
	if (individual.isEmpty() && strips.size() == 1 && !strips.first().vertical &&
	    strips.first().name == "Macro Strip")
		return encode(strips.first());
	auto encodeArray = [&](const QList<MacroStripDefinition> &definitions) {
		QJsonArray array;
		for (const auto &strip : definitions)
			array.append(QJsonObject{{"id", strip.id},
						 {"name", strip.name},
						 {"orientation", strip.vertical ? "vertical" : "horizontal"},
						 {"buttons", encode(strip)["buttons"]}});
		return array;
	};
	QJsonObject result{{"version", 2}, {"activeStrip", strips.at(active).id}, {"strips", encodeArray(strips)}};
	if (!individual.isEmpty())
		result["individualStrips"] = encodeArray(individual);
	return result;
}

bool MacroSerialization::decodeStrips(const QJsonObject &root, QList<MacroStripDefinition> &strips, int &active,
				      QString *errorMessage)
{
	auto fail = [&](const QString &error) {
		if (errorMessage)
			*errorMessage = error;
		return false;
	};
	QList<MacroStripDefinition> parsed;
	int selected = 0;
	if (root["version"].toInt(1) == 2) {
		const auto array = root["strips"].toArray();
		if (array.isEmpty() || array.size() > 32)
			return fail("Macro document requires 1–32 strips");
		QSet<QString> ids;
		selected = -1;
		for (const auto &value : array) {
			const auto obj = value.toObject();
			MacroStripDefinition strip;
			strip.id = obj["id"].toString();
			strip.name = obj["name"].toString().trimmed();
			const auto orientation = obj["orientation"].toString();
			if (strip.id.isEmpty() || ids.contains(strip.id) || strip.name.isEmpty() ||
			    (orientation != "horizontal" && orientation != "vertical"))
				return fail("Invalid macro strip definition");
			ids.insert(strip.id);
			strip.vertical = orientation == "vertical";
			MacroButtonModel model;
			if (!fromJsonDocument(QJsonDocument(obj), model, errorMessage))
				return false;
			strip.buttons = model.buttons();
			if (strip.id == root["activeStrip"].toString())
				selected = parsed.size();
			parsed.append(strip);
		}
		if (selected < 0)
			return fail("Invalid active macro strip");
	} else {
		if (root["version"].toInt(1) != 1)
			return fail("Unsupported macro document version");
		MacroButtonModel model;
		if (!fromJsonDocument(QJsonDocument(root), model, errorMessage))
			return false;
		MacroStripDefinition strip;
		strip.buttons = model.buttons();
		parsed.append(strip);
	}
	strips = std::move(parsed);
	active = selected;
	return true;
}

bool MacroSerialization::decodeWorkspace(const QJsonObject &root, QList<MacroStripDefinition> &strips, int &active,
					 QList<MacroStripDefinition> &individual, QString *errorMessage)
{
	QList<MacroStripDefinition> parsed, extra;
	int selected = 0;
	if (!decodeStrips(root, parsed, selected, errorMessage))
		return false;
	if (root.contains("individualStrips")) {
		const auto value = root["individualStrips"];
		if (!value.isArray()) {
			if (errorMessage)
				*errorMessage = "Invalid individual strips array";
			return false;
		}
		const auto array = value.toArray();
		if (!array.isEmpty()) {
			int ignored = 0;
			if (!decodeStrips(QJsonObject{{"version", 2},
						      {"strips", array},
						      {"activeStrip", array.first().toObject()["id"]}},
					  extra, ignored, errorMessage))
				return false;
		}
		QSet<QString> ids;
		for (const auto &strip : parsed)
			ids.insert(strip.id);
		for (const auto &strip : extra) {
			if (ids.contains(strip.id)) {
				if (errorMessage)
					*errorMessage = "Duplicate macro strip ID";
				return false;
			}
			ids.insert(strip.id);
		}
	}
	strips = std::move(parsed);
	individual = std::move(extra);
	active = selected;
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
