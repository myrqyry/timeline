#include "macro-parameter.h"
#include "scene-item-target.h"
#include <obs.h>
#include <obs-audio-controls.h>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEasingCurve>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
using Source = std::unique_ptr<obs_source_t, decltype(&obs_source_release)>;
using Properties = std::unique_ptr<obs_properties_t, decltype(&obs_properties_destroy)>;
using Data = std::unique_ptr<obs_data_t, decltype(&obs_data_release)>;

using Fader = std::unique_ptr<obs_fader_t, decltype(&obs_fader_destroy)>;

bool isMixerVolume(const MacroParameterBinding &b)
{
	return b.domain == "mixer" && b.key == "timeline:mixer:volume" && b.type == MacroParameterType::Float;
}

bool isMixerMute(const MacroParameterBinding &b)
{
	return b.domain == "mixer" && b.key == "timeline:mixer:mute" && b.type == MacroParameterType::Boolean;
}

bool supportsMixer(obs_source_t *source)
{
	return source && !obs_source_removed(source) && (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) &&
	       obs_source_get_type(source) != OBS_SOURCE_TYPE_FILTER;
}

QString uuid(obs_source_t *source)
{
	return source ? QString::fromUtf8(obs_source_get_uuid(source)) : QString();
}

Source resolve(const MacroParameterBinding &binding)
{
	if (binding.sourceUuid.isEmpty())
		return Source(nullptr, obs_source_release);
	Source parent(obs_get_source_by_uuid(binding.sourceUuid.toUtf8().constData()), obs_source_release);
	if (!parent || obs_source_removed(parent.get()))
		return Source(nullptr, obs_source_release);
	if (binding.filterUuid.isEmpty())
		return parent;
	struct Lookup {
		QString id;
		obs_source_t *result = nullptr;
	} lookup{binding.filterUuid};
	// Filters can be private; resolve through their actual owner.
	obs_source_enum_filters(
		parent.get(),
		[](obs_source_t *, obs_source_t *filter, void *opaque) {
			auto *lookup = static_cast<Lookup *>(opaque);
			if (!lookup->result && !obs_source_removed(filter) && uuid(filter) == lookup->id)
				lookup->result = obs_source_get_ref(filter);
		},
		&lookup);
	return Source(lookup.result, obs_source_release);
}

void collectProperties(obs_properties_t *props, obs_data_t *settings, const QString &prefix,
		       QList<MacroParameter::Spec> &out)
{
	if (!props)
		return;
	for (auto *p = obs_properties_first(props); p; obs_property_next(&p)) {
		if (!obs_property_enabled(p) || !obs_property_visible(p))
			continue;
		const QString key = QString::fromUtf8(obs_property_name(p));
		const QString description = QString::fromUtf8(obs_property_description(p));
		const QString label = prefix + (description.isEmpty() ? key : description);
		if (obs_property_get_type(p) == OBS_PROPERTY_GROUP) {
			if (obs_property_group_type(p) != OBS_GROUP_CHECKABLE ||
			    obs_data_get_bool(settings, key.toUtf8().constData()))
				collectProperties(obs_property_group_content(p), settings, label + " / ", out);
			continue;
		}
		MacroParameter::Spec spec{key, label, {}, MacroParameterType::Float, 0, 1, 1};
		switch (obs_property_get_type(p)) {
		case OBS_PROPERTY_INT:
			spec.type = MacroParameterType::Integer;
			spec.min = obs_property_int_min(p);
			spec.max = obs_property_int_max(p);
			spec.step = obs_property_int_step(p);
			spec.suffix = QString::fromUtf8(obs_property_int_suffix(p));
			break;
		case OBS_PROPERTY_FLOAT:
			spec.min = obs_property_float_min(p);
			spec.max = obs_property_float_max(p);
			spec.step = obs_property_float_step(p);
			spec.suffix = QString::fromUtf8(obs_property_float_suffix(p));
			break;
		case OBS_PROPERTY_BOOL:
			spec.type = MacroParameterType::Boolean;
			break;
		case OBS_PROPERTY_TEXT:
			if (obs_property_text_type(p) != OBS_TEXT_DEFAULT &&
			    obs_property_text_type(p) != OBS_TEXT_MULTILINE)
				continue;
			spec.type = MacroParameterType::Text;
			spec.multiline = obs_property_text_type(p) == OBS_TEXT_MULTILINE;
			break;
		default:
			continue;
		}
		const double steps = (spec.max - spec.min) / spec.step;
		if (!key.isEmpty() && std::isfinite(spec.min) && std::isfinite(spec.max) && std::isfinite(spec.step) &&
		    spec.step > 0 && std::isfinite(steps) && steps >= 1 && steps <= 1000000)
			out.append(spec);
	}
}

QList<MacroParameter::Spec> mixerProperties(obs_source_t *source)
{
	if (!supportsMixer(source))
		return {};
	return {{"timeline:mixer:volume", "Mixer / Volume (fader position)", " %", MacroParameterType::Float, 0, 100,
		 0.1, "mixer"},
		{"timeline:mixer:mute", "Mixer / Mute", {}, MacroParameterType::Boolean, 0, 1, 1, "mixer"}};
}

QList<MacroParameter::Spec> propertiesFor(obs_source_t *source, bool includeMixer)
{
	QList<MacroParameter::Spec> result;
	if (!source)
		return result;
	if (includeMixer)
		result = mixerProperties(source);
	Properties props(obs_source_properties(source), obs_properties_destroy);
	Data settings(obs_source_get_settings(source), obs_data_release);
	if (props && settings)
		collectProperties(props.get(), settings.get(), {}, result);
	return result;
}

std::optional<MacroParameter::Spec> describeFor(obs_source_t *source, const MacroParameterBinding &binding)
{
	const auto specs = binding.domain == "mixer" ? (binding.filterUuid.isEmpty() ? mixerProperties(source)
										     : QList<MacroParameter::Spec>{})
						     : propertiesFor(source, false);
	for (const auto &spec : specs)
		if (spec.key == binding.key && spec.type == binding.type && spec.domain == binding.domain)
			return spec;
	return std::nullopt;
}
} // namespace

namespace MacroParameter {
QString encodeValue(const DefinedValue &cue)
{
	const auto &b = cue.binding;
	const auto type = b.type == MacroParameterType::Integer   ? "int"
			  : b.type == MacroParameterType::Float   ? "float"
			  : b.type == MacroParameterType::Boolean ? "bool"
								  : "text";
	return QString::fromUtf8(QJsonDocument(QJsonObject{{"version", 1},
							   {"sourceUuid", b.sourceUuid},
							   {"filterUuid", b.filterUuid},
							   {"key", b.key},
							   {"domain", b.domain},
							   {"type", type},
							   {"value", QJsonValue::fromVariant(cue.value)}})
					 .toJson(QJsonDocument::Compact));
}
std::optional<DefinedValue> decodeValue(const QString &payload)
{
	const auto doc = QJsonDocument::fromJson(payload.toUtf8());
	if (!doc.isObject())
		return std::nullopt;
	const auto obj = doc.object();
	const QString type = obj["type"].toString();
	const QString domain = obj["domain"].toString();
	if (obj.contains("domain") && (!obj["domain"].isString() || (!domain.isEmpty() && domain != "mixer")))
		return std::nullopt;
	if (obj["version"].toInt(-1) != 1 || !obj["sourceUuid"].isString() || obj["sourceUuid"].toString().isEmpty() ||
	    !obj["filterUuid"].isString() || obj["key"].toString().isEmpty())
		return std::nullopt;
	MacroParameterBinding binding{obj["sourceUuid"].toString(), obj["filterUuid"].toString(), obj["key"].toString(),
				      MacroParameterType::Float, domain};
	if (type == "text") {
		if (!domain.isEmpty() || !obj["value"].isString() || obj["value"].toString().contains(QChar::Null))
			return std::nullopt;
		binding.type = MacroParameterType::Text;
		return DefinedValue{binding, obj["value"].toString()};
	}
	if ((type != "int" && type != "float" && type != "bool") || !obj["value"].isDouble())
		return std::nullopt;
	const double value = obj["value"].toDouble();
	if (!std::isfinite(value) || (type == "bool" && value != 0 && value != 1) ||
	    (type == "int" && value != std::round(value)))
		return std::nullopt;
	binding.type = type == "int"    ? MacroParameterType::Integer
		       : type == "bool" ? MacroParameterType::Boolean
					: MacroParameterType::Float;
	return DefinedValue{binding, value};
}
QString bindingIdentity(const MacroParameterBinding &b)
{
	return QString::fromUtf8(
		QJsonDocument(QJsonArray{b.sourceUuid, b.filterUuid, b.domain, b.key}).toJson(QJsonDocument::Compact));
}
QString encodeRamp(const Ramp &ramp)
{
	auto obj = QJsonDocument::fromJson(encodeValue(ramp.start).toUtf8()).object();
	obj["version"] = 2;
	obj["endValue"] = ramp.end;
	obj["easing"] = ramp.easing;
	return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}
std::optional<Ramp> decodeRamp(const QString &payload)
{
	auto obj = QJsonDocument::fromJson(payload.toUtf8()).object();
	if (obj["version"].toInt(-1) != 2 || !obj["endValue"].isDouble())
		return std::nullopt;
	const double end = obj["endValue"].toDouble();
	const QString easing = obj["easing"].toString();
	if (!std::isfinite(end) ||
	    (easing != "linear" && easing != "ease-in" && easing != "ease-out" && easing != "ease-in-out"))
		return std::nullopt;
	obj["version"] = 1;
	const auto start = decodeValue(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
	if (!start ||
	    (start->binding.type != MacroParameterType::Float && start->binding.type != MacroParameterType::Integer) ||
	    (start->binding.type == MacroParameterType::Integer && end != std::round(end)))
		return std::nullopt;
	return Ramp{*start, end, easing};
}
double rampValue(const Ramp &ramp, double progress)
{
	const double p = std::clamp(progress, 0.0, 1.0);
	const double start = ramp.start.value.toDouble();
	if (p <= 0)
		return start;
	if (p >= 1)
		return ramp.end;
	const auto type = ramp.easing == "ease-in"       ? QEasingCurve::InQuad
			  : ramp.easing == "ease-out"    ? QEasingCurve::OutQuad
			  : ramp.easing == "ease-in-out" ? QEasingCurve::InOutQuad
							 : QEasingCurve::Linear;
	const double eased = QEasingCurve(type).valueForProgress(p);
	return (1.0 - eased) * start + eased * ramp.end;
}

QString mixerVolumeLabel(double position)
{
	Fader fader(obs_fader_create(OBS_FADER_LOG), obs_fader_destroy);
	if (!fader)
		return QString::number(position, 'f', 1) + " %";
	obs_fader_set_deflection(fader.get(), static_cast<float>(std::clamp(position, 0.0, 100.0) / 100.0));
	const float db = obs_fader_get_db(fader.get());
	return std::isinf(db) ? QStringLiteral("-inf dB") : QString::number(db, 'f', 1) + " dB";
}

QString displayValue(const DefinedValue &cue)
{
	if (cue.binding.type == MacroParameterType::Text)
		return cue.binding.key + " = " + cue.value.toString();
	const double value = cue.value.toDouble();
	if (isMixerVolume(cue.binding))
		return "Mixer volume = " + QString::number(value, 'g', 7) + "% (" + mixerVolumeLabel(value) + ")";
	if (isMixerMute(cue.binding))
		return value != 0 ? QStringLiteral("Mixer mute = On") : QStringLiteral("Mixer mute = Off");
	return cue.binding.key + " = " +
	       (cue.binding.type == MacroParameterType::Boolean
			? (value != 0 ? QStringLiteral("On") : QStringLiteral("Off"))
			: QString::number(value, 'g', 12));
}
int Spec::steps() const
{
	return static_cast<int>(std::floor((max - min) / step + 1e-8));
}
double Spec::valueAt(int index) const
{
	return std::clamp(min + step * std::clamp(index, 0, steps()), min, max);
}
int Spec::indexOf(double value) const
{
	return static_cast<int>(std::round((std::clamp(value, min, max) - min) / step));
}

QList<Target> targets()
{
	struct Context {
		QList<Target> out;
		QSet<QString> seen;
		void add(obs_source_t *source, const QString &label)
		{
			const QString id = uuid(source);
			if (id.isEmpty() || obs_source_removed(source) || seen.contains(id))
				return;
			seen.insert(id);
			out.append({id, {}, label});
			struct FilterContext {
				Context *self;
				QString id;
				QString label;
			} fc{this, id, label};
			obs_source_enum_filters(
				source,
				[](obs_source_t *, obs_source_t *filter, void *opaque) {
					auto *fc = static_cast<FilterContext *>(opaque);
					if (!obs_source_removed(filter) && !uuid(filter).isEmpty())
						fc->self->out.append(
							{fc->id, uuid(filter),
							 fc->label + " / Filter: " +
								 QString::fromUtf8(obs_source_get_name(filter))});
				},
				&fc);
		}
	} ctx;
	obs_enum_scenes(
		[](void *opaque, obs_source_t *source) {
			auto *ctx = static_cast<Context *>(opaque);
			const QString name = QString::fromUtf8(obs_source_get_name(source));
			ctx->add(source, name);
			SceneItemTarget::visit(obs_group_or_scene_from_source(source), name,
					       [&](obs_scene_t *, obs_sceneitem_t *item, const QString &,
						   const QString &path) {
						       ctx->add(obs_sceneitem_get_source(item), path);
					       });
			return true;
		},
		&ctx);
	obs_enum_sources(
		[](void *opaque, obs_source_t *source) {
			static_cast<Context *>(opaque)->add(source, QString::fromUtf8(obs_source_get_name(source)));
			return true;
		},
		&ctx);
	return ctx.out;
}

QList<Spec> properties(const MacroParameterBinding &binding)
{
	auto source = resolve(binding);
	return propertiesFor(source.get(), binding.filterUuid.isEmpty());
}
std::optional<Spec> describe(const MacroParameterBinding &binding)
{
	auto source = resolve(binding);
	return describeFor(source.get(), binding);
}
std::optional<double> read(const MacroParameterBinding &binding)
{
	auto source = resolve(binding);
	if (!source || binding.key.isEmpty() || binding.type == MacroParameterType::Text)
		return std::nullopt;
	if (!binding.domain.isEmpty()) {
		if (!binding.filterUuid.isEmpty() || !supportsMixer(source.get()))
			return std::nullopt;
		if (isMixerMute(binding))
			return obs_source_muted(source.get()) ? 1.0 : 0.0;
		if (!isMixerVolume(binding))
			return std::nullopt;
		Fader fader(obs_fader_create(OBS_FADER_LOG), obs_fader_destroy);
		if (!fader)
			return std::nullopt;
		const float volume = obs_source_get_volume(source.get());
		if (!std::isfinite(volume))
			return std::nullopt;
		obs_fader_set_mul(fader.get(), volume);
		return std::clamp(static_cast<double>(obs_fader_get_deflection(fader.get())) * 100.0, 0.0, 100.0);
	}
	Data settings(obs_source_get_settings(source.get()), obs_data_release);
	if (!settings)
		return std::nullopt;
	const auto key = binding.key.toUtf8();
	// OBS getters return the implicit zero/false value for unset properties.
	// Only accept that fallback for a property that still exists.
	auto *item = obs_data_item_byname(settings.get(), key.constData());
	if (item) {
		const auto dataType = obs_data_item_gettype(item);
		obs_data_item_release(&item);
		if (dataType != (binding.type == MacroParameterType::Boolean ? OBS_DATA_BOOLEAN : OBS_DATA_NUMBER))
			return std::nullopt;
	} else if (!describeFor(source.get(), binding)) {
		return std::nullopt;
	}
	double value = 0;
	switch (binding.type) {
	case MacroParameterType::Integer:
		value = obs_data_get_int(settings.get(), key.constData());
		break;
	case MacroParameterType::Float:
		value = obs_data_get_double(settings.get(), key.constData());
		break;
	case MacroParameterType::Boolean:
		value = obs_data_get_bool(settings.get(), key.constData()) ? 1 : 0;
		break;
	case MacroParameterType::Text:
		return std::nullopt;
	}
	return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}
bool write(const MacroParameterBinding &binding, double value, bool skipUnchanged)
{
	if (!std::isfinite(value) || binding.type == MacroParameterType::Text)
		return false;
	auto source = resolve(binding);
	if (!source)
		return false;
	// Revalidate the live property schema at write time. A different shader
	// or source configuration can remove, hide or change a parameter.
	const auto spec = describeFor(source.get(), binding);
	if (!spec)
		return false;
	value = spec->valueAt(spec->indexOf(value));
	// Animation ticks often quantize to an already-applied value. Avoid
	// reconfiguring the source unnecessarily, but still validate its schema.
	if (skipUnchanged && !isMixerVolume(binding)) {
		const auto current = read(binding);
		if (current && *current == value)
			return true;
	}
	if (isMixerMute(binding)) {
		obs_source_set_muted(source.get(), value != 0);
		return true;
	}
	if (isMixerVolume(binding)) {
		Fader fader(obs_fader_create(OBS_FADER_LOG), obs_fader_destroy);
		if (!fader)
			return false;
		obs_fader_set_deflection(fader.get(), static_cast<float>(value / 100.0));
		const auto gain = obs_fader_get_mul(fader.get());
		if (!skipUnchanged || obs_source_get_volume(source.get()) != gain)
			obs_source_set_volume(source.get(), gain);
		return true;
	}
	Data patch(obs_data_create(), obs_data_release);
	const auto key = binding.key.toUtf8();
	switch (binding.type) {
	case MacroParameterType::Integer:
		obs_data_set_int(patch.get(), key.constData(), std::llround(value));
		break;
	case MacroParameterType::Float:
		obs_data_set_double(patch.get(), key.constData(), value);
		break;
	case MacroParameterType::Boolean:
		obs_data_set_bool(patch.get(), key.constData(), value != 0);
		break;
	case MacroParameterType::Text:
		return false;
	}
	obs_source_update(source.get(), patch.get());
	return true;
}
bool numericPair(const MacroParameterBinding &x, const MacroParameterBinding &y)
{
	auto numeric = [](const MacroParameterBinding &b) {
		return !b.sourceUuid.isEmpty() && !b.key.isEmpty() &&
		       (b.type == MacroParameterType::Integer || b.type == MacroParameterType::Float) &&
		       (b.domain.isEmpty() || (isMixerVolume(b) && b.filterUuid.isEmpty()));
	};
	return numeric(x) && numeric(y) && bindingIdentity(x) != bindingIdentity(y);
}

bool writePair(const MacroParameterBinding &x, double xValue, const MacroParameterBinding &y, double yValue)
{
	if (!numericPair(x, y) || !std::isfinite(xValue) || !std::isfinite(yValue))
		return false;
	// Validate both axes before touching either target.
	auto xSource = resolve(x);
	auto ySource = resolve(y);
	const auto xs = describeFor(xSource.get(), x);
	const auto ys = describeFor(ySource.get(), y);
	if (!xs || !ys)
		return false;
	xValue = xs->valueAt(xs->indexOf(xValue));
	yValue = ys->valueAt(ys->indexOf(yValue));
	if (xSource.get() == ySource.get() && x.domain.isEmpty() && y.domain.isEmpty()) {
		// One settings update for paired uniforms on the same source/filter.
		Data patch(obs_data_create(), obs_data_release);
		if (!patch)
			return false;
		auto add = [&](const MacroParameterBinding &b, double value) {
			const auto key = b.key.toUtf8();
			if (b.type == MacroParameterType::Integer)
				obs_data_set_int(patch.get(), key.constData(), std::llround(value));
			else
				obs_data_set_double(patch.get(), key.constData(), value);
		};
		add(x, xValue);
		add(y, yValue);
		obs_source_update(xSource.get(), patch.get());
		return true;
	}
	// Separate OBS objects/native mixer operations cannot be transactional.
	return write(x, xValue, true) && write(y, yValue, true);
}

std::optional<QString> readText(const MacroParameterBinding &binding)
{
	if (binding.type != MacroParameterType::Text || !binding.domain.isEmpty())
		return std::nullopt;
	auto source = resolve(binding);
	if (!source || !describeFor(source.get(), binding))
		return std::nullopt;
	Data settings(obs_source_get_settings(source.get()), obs_data_release);
	if (!settings)
		return std::nullopt;
	const auto key = binding.key.toUtf8();
	auto *item = obs_data_item_byname(settings.get(), key.constData());
	if (item) {
		const auto type = obs_data_item_gettype(item);
		obs_data_item_release(&item);
		if (type != OBS_DATA_STRING)
			return std::nullopt;
	}
	return QString::fromUtf8(obs_data_get_string(settings.get(), key.constData()));
}
bool writeText(const MacroParameterBinding &binding, const QString &value)
{
	if (binding.type != MacroParameterType::Text || !binding.domain.isEmpty() || value.contains(QChar::Null))
		return false;
	auto source = resolve(binding);
	if (!source || !describeFor(source.get(), binding))
		return false;
	Data patch(obs_data_create(), obs_data_release);
	if (!patch)
		return false;
	obs_data_set_string(patch.get(), binding.key.toUtf8().constData(), value.toUtf8().constData());
	obs_source_update(source.get(), patch.get());
	return true;
}
} // namespace MacroParameter
