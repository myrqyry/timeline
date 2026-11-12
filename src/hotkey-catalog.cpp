#include "hotkey-catalog.h"

#include <obs.h>

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

int groupRank(const QString &group)
{
	if (group.startsWith(QStringLiteral("Move · ")))
		return 0;
	if (group.startsWith(QStringLiteral("Source · ")))
		return 1;
	if (group == QStringLiteral("Frontend / Plugins"))
		return 2;
	if (group == QStringLiteral("Outputs"))
		return 3;
	if (group == QStringLiteral("Encoders"))
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
				item.group = QStringLiteral("Outputs");
				break;
			case OBS_HOTKEY_REGISTERER_ENCODER:
				item.group = QStringLiteral("Encoders");
				break;
			case OBS_HOTKEY_REGISTERER_SERVICE:
				item.group = QStringLiteral("Services");
				break;
			}
			result.append(item);
			return true;
		},
		&entries);
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
