#pragma once

#include <obs.h>
#include <QString>
#include <QStringList>

namespace MediaTarget {
struct Selection {
	QString uuid;
	qint64 positionMs = 0;
	bool valid = true;
};

inline QString encode(const QString &uuid, qint64 positionMs = 0)
{
	return QStringLiteral("media:v1:%1:%2").arg(uuid).arg(positionMs);
}

inline bool isExplicit(const QString &data)
{
	return data.startsWith(QStringLiteral("media:"));
}

inline Selection decode(const QString &data)
{
	if (!isExplicit(data))
		return {};
	const auto parts = data.split(QLatin1Char(':'));
	bool ok = false;
	const qint64 ms = parts.size() == 4 ? parts[3].toLongLong(&ok) : 0;
	if (!ok || parts[1] != QStringLiteral("v1") || ms < 0)
		return {{}, 0, false};
	return {parts[2], ms, true};
}

inline obs_source_t *resolve(const QString &name, const Selection &target)
{
	if (!target.valid)
		return nullptr;
	auto *source = target.uuid.isEmpty() ? obs_get_source_by_name(name.toUtf8().constData())
					     : obs_get_source_by_uuid(target.uuid.toUtf8().constData());
	if (source &&
	    (obs_source_removed(source) || !(obs_source_get_output_flags(source) & OBS_SOURCE_CONTROLLABLE_MEDIA))) {
		obs_source_release(source);
		return nullptr;
	}
	return source;
}
} // namespace MediaTarget
