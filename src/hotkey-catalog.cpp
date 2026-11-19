#include "hotkey-catalog.h"

#include <obs.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>

#include <algorithm>

namespace {

bool isMoveFilter(const QString &id)
{
	static const QStringList ids = {
		QStringLiteral("move_source_filter"),       QStringLiteral("move_source_swap_filter"),
		QStringLiteral("move_value_filter"),        QStringLiteral("move_audio_value_filter"),
		QStringLiteral("audio_move_filter"),        QStringLiteral("move_action_filter"),
		QStringLiteral("move_audio_action_filter"), QStringLiteral("move_directshow_filter"),
		QStringLiteral("nvidia_move_filter"),
	};
	return ids.contains(id);
}

QString sourceDisplayName(obs_source_t *source)
{
	const char *name = source ? obs_source_get_name(source) : nullptr;
	return name && *name ? QString::fromUtf8(name) : QStringLiteral("(unnamed source)");
}

struct OwnerIdentity {
	QString type;
	QString key;
	QString display;
	bool valid = false;
};

OwnerIdentity identifyOwner(obs_hotkey_t *hotkey)
{
	void *weak = obs_hotkey_get_registerer(hotkey);
	switch (obs_hotkey_get_registerer_type(hotkey)) {
	case OBS_HOTKEY_REGISTERER_FRONTEND:
		return {"frontend", {}, {}, true};
	case OBS_HOTKEY_REGISTERER_SOURCE: {
		auto *source = weak ? obs_weak_source_get_source(static_cast<obs_weak_source_t *>(weak)) : nullptr;
		if (!source)
			return {};
		const QString uuid = QString::fromUtf8(obs_source_get_uuid(source));
		const QString name = sourceDisplayName(source);
		const bool valid = !uuid.isEmpty() && !obs_source_removed(source);
		obs_source_release(source);
		return {"source", uuid, name, valid};
	}
	case OBS_HOTKEY_REGISTERER_OUTPUT: {
		auto *output = weak ? obs_weak_output_get_output(static_cast<obs_weak_output_t *>(weak)) : nullptr;
		if (!output)
			return {};
		const QString name = QString::fromUtf8(obs_output_get_name(output));
		obs_output_release(output);
		return {"output", name, name, !name.isEmpty()};
	}
	case OBS_HOTKEY_REGISTERER_ENCODER: {
		auto *encoder = weak ? obs_weak_encoder_get_encoder(static_cast<obs_weak_encoder_t *>(weak)) : nullptr;
		if (!encoder)
			return {};
		const QString name = QString::fromUtf8(obs_encoder_get_name(encoder));
		obs_encoder_release(encoder);
		return {"encoder", name, name, !name.isEmpty()};
	}
	case OBS_HOTKEY_REGISTERER_SERVICE: {
		auto *service = weak ? obs_weak_service_get_service(static_cast<obs_weak_service_t *>(weak)) : nullptr;
		if (!service)
			return {};
		const QString name = QString::fromUtf8(obs_service_get_name(service));
		obs_service_release(service);
		return {"service", name, name, !name.isEmpty()};
	}
	}
	return {};
}

QString bindingFor(const OwnerIdentity &owner, const QString &name)
{
	if (!owner.valid || name.isEmpty())
		return {};
	return QStringLiteral("hotkey:v1:") +
	       QString::fromUtf8(
		       QJsonDocument(QJsonObject{{"registerer", owner.type}, {"owner", owner.key}, {"name", name}})
			       .toJson(QJsonDocument::Compact));
}

int groupRank(const QString &group)
{
	if (group.startsWith(QStringLiteral("Move · ")))
		return 0;
	if (group.startsWith(QStringLiteral("Source · ")))
		return 1;
	if (group == QStringLiteral("Frontend / Plugins"))
		return 2;
	if (group.startsWith(QStringLiteral("Outputs")))
		return 3;
	if (group.startsWith(QStringLiteral("Encoders")))
		return 4;
	return 5;
}

} // namespace

QString HotkeyCatalog::sourceGroup(const QString &sourceName, const QString &hotkeyName,
				   const QList<MoveFilterIdentity> &filters)
{
	for (const auto &filter : filters) {
		if (!isMoveFilter(filter.id) || filter.name.isEmpty())
			continue;
		// Move registers source hotkeys under its filter name, with an optional Hold suffix.
		if (hotkeyName == filter.name || hotkeyName.startsWith(filter.name + QLatin1Char(' ')))
			return QStringLiteral("Move · ") + sourceName;
	}
	return QStringLiteral("Source · ") + sourceName;
}

QList<HotkeyChoice> HotkeyCatalog::collect()
{
	QList<HotkeyChoice> entries;
	obs_enum_hotkeys(
		[](void *param, obs_hotkey_id, obs_hotkey_t *hotkey) {
			auto &result = *static_cast<QList<HotkeyChoice> *>(param);
			const char *rawName = obs_hotkey_get_name(hotkey);
			if (!rawName || !*rawName)
				return true;
			HotkeyChoice item;
			item.id = QString::fromUtf8(rawName);
			const auto owner = identifyOwner(hotkey);
			item.binding = bindingFor(owner, item.id);
			if (item.binding.isEmpty())
				return true;
			const char *desc = obs_hotkey_get_description(hotkey);
			item.label = desc && *desc ? QString::fromUtf8(desc) : item.id;

			switch (obs_hotkey_get_registerer_type(hotkey)) {
			case OBS_HOTKEY_REGISTERER_SOURCE: {
				// libobs stores a weak source handle in hotkey.registerer, not a source pointer.
				auto *weak = static_cast<obs_weak_source_t *>(obs_hotkey_get_registerer(hotkey));
				obs_source_t *source = weak ? obs_weak_source_get_source(weak) : nullptr;
				if (!source) {
					item.group = QStringLiteral("Source · (unavailable)");
					break;
				}
				QList<MoveFilterIdentity> filters;
				obs_source_enum_filters(
					source,
					[](obs_source_t *, obs_source_t *filter, void *context) {
						auto &found = *static_cast<QList<MoveFilterIdentity> *>(context);
						const char *id = obs_source_get_unversioned_id(filter);
						const char *name = obs_source_get_name(filter);
						if (id && name)
							found.append({QString::fromUtf8(id), QString::fromUtf8(name)});
					},
					&filters);
				item.group = sourceGroup(sourceDisplayName(source), item.id, filters);
				obs_source_release(source);
				break;
			}
			case OBS_HOTKEY_REGISTERER_FRONTEND:
				// OBS does not expose plugin ownership for frontend-registered hotkeys.
				item.group = QStringLiteral("Frontend / Plugins");
				break;
			case OBS_HOTKEY_REGISTERER_OUTPUT:
				item.group = QStringLiteral("Outputs · ") + owner.display;
				break;
			case OBS_HOTKEY_REGISTERER_ENCODER:
				item.group = QStringLiteral("Encoders · ") + owner.display;
				break;
			case OBS_HOTKEY_REGISTERER_SERVICE:
				item.group = QStringLiteral("Services · ") + owner.display;
				break;
			}
			result.append(item);
			return true;
		},
		&entries);
	// A duplicate complete identity cannot be persisted safely (e.g. two
	// frontend registrations with the same internal name). Don't offer it.
	QHash<QString, int> counts;
	for (const auto &entry : entries)
		++counts[entry.binding];
	entries.removeIf([&](const HotkeyChoice &entry) { return counts.value(entry.binding) != 1; });
	std::sort(entries.begin(), entries.end(), [](const HotkeyChoice &a, const HotkeyChoice &b) {
		if (groupRank(a.group) != groupRank(b.group))
			return groupRank(a.group) < groupRank(b.group);
		const int groupCompare = a.group.compare(b.group, Qt::CaseInsensitive);
		if (groupCompare != 0)
			return groupCompare < 0;
		return a.label.compare(b.label, Qt::CaseInsensitive) < 0;
	});
	return entries;
}

obs_hotkey_id HotkeyCatalog::resolve(const QString &name, const QString &binding)
{
	struct Search {
		QString name;
		QString binding;
		obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
		int matches = 0;
	} search{name, binding};
	if (name.isEmpty())
		return search.id;
	obs_enum_hotkeys(
		[](void *context, obs_hotkey_id id, obs_hotkey_t *hotkey) {
			auto &s = *static_cast<Search *>(context);
			const QString name = QString::fromUtf8(obs_hotkey_get_name(hotkey));
			if (name != s.name)
				return true;
			if (!s.binding.isEmpty() && bindingFor(identifyOwner(hotkey), name) != s.binding)
				return true;
			s.id = id;
			++s.matches;
			// Historical name-only selections keep their previous first-match
			// behavior. Explicit bindings never fall back or choose ambiguously.
			return !s.binding.isEmpty();
		},
		&search);
	return search.matches == 1 ? search.id : OBS_INVALID_HOTKEY_ID;
}
