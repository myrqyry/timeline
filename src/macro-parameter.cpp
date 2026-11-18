#include "macro-parameter.h"
#include "scene-item-target.h"
#include <obs.h>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
using Source = std::unique_ptr<obs_source_t, decltype(&obs_source_release)>;
using Properties = std::unique_ptr<obs_properties_t, decltype(&obs_properties_destroy)>;
using Data = std::unique_ptr<obs_data_t, decltype(&obs_data_release)>;

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
	obs_source_enum_filters(parent.get(), [](obs_source_t *, obs_source_t *filter, void *opaque) {
		auto *lookup = static_cast<Lookup *>(opaque);
		if (!lookup->result && !obs_source_removed(filter) && uuid(filter) == lookup->id)
			lookup->result = obs_source_get_ref(filter);
	}, &lookup);
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
		default:
			continue;
		}
		const double steps = (spec.max - spec.min) / spec.step;
		if (!key.isEmpty() && std::isfinite(spec.min) && std::isfinite(spec.max) &&
		    std::isfinite(spec.step) && spec.step > 0 && std::isfinite(steps) &&
		    steps >= 1 && steps <= 1000000)
			out.append(spec);
	}
}

QList<MacroParameter::Spec> propertiesFor(obs_source_t *source)
{
	QList<MacroParameter::Spec> result;
	if (!source)
		return result;
	Properties props(obs_source_properties(source), obs_properties_destroy);
	Data settings(obs_source_get_settings(source), obs_data_release);
	if (props && settings)
		collectProperties(props.get(), settings.get(), {}, result);
	return result;
}

std::optional<MacroParameter::Spec> describeFor(obs_source_t *source, const MacroParameterBinding &binding)
{
	for (const auto &spec : propertiesFor(source))
		if (spec.key == binding.key && spec.type == binding.type)
			return spec;
	return std::nullopt;
}
}

namespace MacroParameter {
QString encodeValue(const DefinedValue &cue)
{
	const auto &b = cue.binding;
	return QString::fromUtf8(QJsonDocument(QJsonObject{
		{"version", 1}, {"sourceUuid", b.sourceUuid}, {"filterUuid", b.filterUuid},
		{"key", b.key}, {"type", b.type == MacroParameterType::Integer ? "int" :
			b.type == MacroParameterType::Float ? "float" : "bool"}, {"value", cue.value}
	}).toJson(QJsonDocument::Compact));
}
std::optional<DefinedValue> decodeValue(const QString &payload)
{
	const auto doc = QJsonDocument::fromJson(payload.toUtf8());
	if (!doc.isObject())
		return std::nullopt;
	const auto obj = doc.object();
	const QString type = obj["type"].toString();
	if (obj["version"].toInt(-1) != 1 || !obj["sourceUuid"].isString() ||
	    obj["sourceUuid"].toString().isEmpty() || !obj["filterUuid"].isString() ||
	    obj["key"].toString().isEmpty() || !obj["value"].isDouble() ||
	    (type != "int" && type != "float" && type != "bool"))
		return std::nullopt;
	const double value = obj["value"].toDouble();
	if (!std::isfinite(value) || (type == "bool" && value != 0 && value != 1) ||
	    (type == "int" && value != std::round(value)))
		return std::nullopt;
	return DefinedValue{{obj["sourceUuid"].toString(), obj["filterUuid"].toString(),
		obj["key"].toString(), type == "int" ? MacroParameterType::Integer :
		type == "float" ? MacroParameterType::Float : MacroParameterType::Boolean}, value};
}
QString displayValue(const DefinedValue &cue)
{
	return cue.binding.key + " = " + (cue.binding.type == MacroParameterType::Boolean ?
		(cue.value != 0 ? QStringLiteral("On") : QStringLiteral("Off")) :
		QString::number(cue.value, 'g', 12));
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
			struct FilterContext { Context *self; QString id; QString label; } fc{this, id, label};
			obs_source_enum_filters(source, [](obs_source_t *, obs_source_t *filter, void *opaque) {
				auto *fc = static_cast<FilterContext *>(opaque);
				if (!obs_source_removed(filter) && !uuid(filter).isEmpty())
					fc->self->out.append({fc->id, uuid(filter),
						fc->label + " / Filter: " + QString::fromUtf8(obs_source_get_name(filter))});
			}, &fc);
		}
	} ctx;
	obs_enum_scenes([](void *opaque, obs_source_t *source) {
		auto *ctx = static_cast<Context *>(opaque);
		const QString name = QString::fromUtf8(obs_source_get_name(source));
		ctx->add(source, name);
		SceneItemTarget::visit(obs_group_or_scene_from_source(source), name,
			[&](obs_scene_t *, obs_sceneitem_t *item, const QString &, const QString &path) {
				ctx->add(obs_sceneitem_get_source(item), path);
			});
		return true;
	}, &ctx);
	obs_enum_sources([](void *opaque, obs_source_t *source) {
		static_cast<Context *>(opaque)->add(source, QString::fromUtf8(obs_source_get_name(source)));
		return true;
	}, &ctx);
	return ctx.out;
}

QList<Spec> properties(const MacroParameterBinding &binding)
{
	auto source = resolve(binding);
	return propertiesFor(source.get());
}
std::optional<Spec> describe(const MacroParameterBinding &binding)
{
	auto source = resolve(binding);
	return describeFor(source.get(), binding);
}
std::optional<double> read(const MacroParameterBinding &binding)
{
	auto source = resolve(binding);
	if (!source || binding.key.isEmpty())
		return std::nullopt;
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
	case MacroParameterType::Integer: value = obs_data_get_int(settings.get(), key.constData()); break;
	case MacroParameterType::Float: value = obs_data_get_double(settings.get(), key.constData()); break;
	case MacroParameterType::Boolean: value = obs_data_get_bool(settings.get(), key.constData()) ? 1 : 0; break;
	}
	return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}
bool write(const MacroParameterBinding &binding, double value)
{
	if (!std::isfinite(value))
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
	Data patch(obs_data_create(), obs_data_release);
	const auto key = binding.key.toUtf8();
	switch (binding.type) {
	case MacroParameterType::Integer: obs_data_set_int(patch.get(), key.constData(), std::llround(value)); break;
	case MacroParameterType::Float: obs_data_set_double(patch.get(), key.constData(), value); break;
	case MacroParameterType::Boolean: obs_data_set_bool(patch.get(), key.constData(), value != 0); break;
	}
	obs_source_update(source.get(), patch.get());
	return true;
}
} // namespace MacroParameter
