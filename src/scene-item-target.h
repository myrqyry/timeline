#pragma once

#include <obs.h>
#include <QString>
#include <QStringList>
#include <QSet>

namespace SceneItemTarget {
inline QString key(obs_scene_t *owner, obs_sceneitem_t *item)
{
	return QStringLiteral("scene-item:v1:%1:%2:%3")
		.arg(QString::fromUtf8(obs_source_get_uuid(obs_scene_get_source(owner))))
		.arg(obs_sceneitem_get_id(item))
		.arg(QString::fromUtf8(obs_source_get_uuid(obs_sceneitem_get_source(item))));
}

inline bool isExplicit(const QString &value)
{
	return value.startsWith(QStringLiteral("scene-item:"));
}

// Visit each underlying scene item once, including shared nested scenes and groups.
// Visibility belongs to the owning scene item, not to the referenced source.
template<typename Visitor> void visit(obs_scene_t *root, const QString &prefix, Visitor visitor)
{
	struct Context {
		QSet<obs_scene_t *> seen;
		Visitor &visitor;
		void walk(obs_scene_t *scene, const QString &path)
		{
			if (!scene || seen.contains(scene))
				return;
			seen.insert(scene);
			struct Frame {
				Context *context;
				QString path;
			} frame{this, path};
			obs_scene_enum_items(
				scene,
				[](obs_scene_t *owner, obs_sceneitem_t *item, void *opaque) {
					auto *f = static_cast<Frame *>(opaque);
					auto *source = obs_sceneitem_get_source(item);
					const QString name = QString::fromUtf8(obs_source_get_name(source));
					const QString label = f->path + QStringLiteral(" / ") + name;
					f->context->visitor(owner, item, name, label);
					auto *nested = obs_group_or_scene_from_source(source);
					f->context->walk(nested, label);
					return true;
				},
				&frame);
		}
	} context{{}, visitor};
	context.walk(root, prefix);
}

template<typename Visitor> inline bool withExact(const QString &target, Visitor visitItem)
{
	const auto parts = target.split(QLatin1Char(':'));
	if (parts.size() != 5 || parts[0] != QStringLiteral("scene-item") || parts[1] != QStringLiteral("v1") ||
	    parts[2].isEmpty() || parts[4].isEmpty())
		return false;
	bool valid = false;
	const auto id = parts[3].toLongLong(&valid);
	if (!valid)
		return false;
	auto *owner = obs_get_source_by_uuid(parts[2].toUtf8().constData());
	if (!owner)
		return false;
	auto *scene = !obs_source_removed(owner) ? obs_group_or_scene_from_source(owner) : nullptr;
	auto *item = scene ? obs_scene_find_sceneitem_by_id(scene, id) : nullptr;
	const bool matches = item && QString::fromUtf8(obs_source_get_uuid(obs_sceneitem_get_source(item))) == parts[4];
	if (matches)
		visitItem(item);
	obs_source_release(owner);
	return matches;
}
inline bool setVisible(const QString &target, bool visible)
{
	return withExact(target, [visible](obs_sceneitem_t *item) { obs_sceneitem_set_visible(item, visible); });
}

inline bool exists(const QString &target)
{
	return withExact(target, [](obs_sceneitem_t *) {});
}
} // namespace SceneItemTarget
