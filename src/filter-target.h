#pragma once

#include <obs.h>
#include <QString>
#include <QStringList>

namespace FilterTarget {
struct Selection {
	QString sourceUuid;
	QString filterUuid;
};

inline QString encode(const QString &sourceUuid, const QString &filterUuid)
{
	return QStringLiteral("filter:v1:%1:%2").arg(sourceUuid, filterUuid);
}

inline bool isExplicit(const QString &payload)
{
	return payload.startsWith(QStringLiteral("filter:"));
}

inline bool decode(const QString &payload, Selection &out)
{
	const auto parts = payload.split(QLatin1Char(':'));
	if (parts.size() != 4 || parts[0] != "filter" || parts[1] != "v1" || parts[2].isEmpty() || parts[3].isEmpty())
		return false;
	out = {parts[2], parts[3]};
	return true;
}

// Filters may be private, so resolve through the actual owner rather than
// a global UUID lookup. Caller owns the returned reference.
inline obs_source_t *resolveOwned(obs_source_t *owner, const QString &filterUuid)
{
	if (!owner || obs_source_removed(owner) || filterUuid.isEmpty())
		return nullptr;
	struct Lookup {
		QString id;
		obs_source_t *result = nullptr;
	} lookup{filterUuid};
	obs_source_enum_filters(
		owner,
		[](obs_source_t *, obs_source_t *filter, void *opaque) {
			auto *lookup = static_cast<Lookup *>(opaque);
			if (!lookup->result && !obs_source_removed(filter) &&
			    QString::fromUtf8(obs_source_get_uuid(filter)) == lookup->id)
				lookup->result = obs_source_get_ref(filter);
		},
		&lookup);
	return lookup.result;
}

inline obs_source_t *resolve(const QString &payload)
{
	Selection selection;
	if (!decode(payload, selection))
		return nullptr;
	auto *owner = obs_get_source_by_uuid(selection.sourceUuid.toUtf8().constData());
	auto *filter = resolveOwned(owner, selection.filterUuid);
	if (owner)
		obs_source_release(owner);
	return filter;
}
} // namespace FilterTarget
