#include "plugin-integrations.h"

#include <obs.h>
#include <obs-module.h>

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QSettings>

#include <algorithm>

PluginIntegrations &PluginIntegrations::instance()
{
	static PluginIntegrations catalog;
	return catalog;
}

void PluginIntegrations::setStoragePath(const QString &path)
{
	m_storagePath = path;
	m_preferences.clear();
	if (path.isEmpty())
		return;
	QSettings settings(path, QSettings::IniFormat);
	settings.beginGroup("plugins");
	for (const QString &key : settings.childKeys())
		m_preferences.insert(key, settings.value(key).toBool());
	settings.endGroup();
}

void PluginIntegrations::refreshLoadedModules()
{
	QList<LoadedObsPlugin> modules;
	obs_enum_modules(
		[](void *param, obs_module_t *module) {
			auto *result = static_cast<QList<LoadedObsPlugin> *>(param);
			const char *file = obs_get_module_file_name(module);
			if (!file || !*file)
				return;
			const QString id = QString::fromUtf8(file).toLower();
			const char *description = obs_get_module_name(module);
			const QString name = description && *description ? QString::fromUtf8(description) : id;
			result->append({id, name});
		},
		&modules);
	setDetectedModules(modules);
}

void PluginIntegrations::setDetectedModules(const QList<LoadedObsPlugin> &modules)
{
	m_loaded.clear();
	for (auto module : modules) {
		module.id = module.id.trimmed().toLower();
		if (module.id.isEmpty() || module.id == "timeline")
			continue;
		if (module.name.isEmpty())
			module.name = module.id;
		if (std::none_of(m_loaded.cbegin(), m_loaded.cend(),
				 [&module](const LoadedObsPlugin &other) { return module.id == other.id; }))
			m_loaded.append(module);
	}
	std::sort(m_loaded.begin(), m_loaded.end(), [](const LoadedObsPlugin &a, const LoadedObsPlugin &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
}

bool PluginIntegrations::isSupported(const QString &id) const
{
	// Supported adapters must be explicitly implemented and validated.
#if OBS_TIMELINE_HAS_WEBSOCKET
	return id.compare(QStringLiteral("downstream-keyer"), Qt::CaseInsensitive) == 0;
#else
	Q_UNUSED(id);
	return false;
#endif
}

bool PluginIntegrations::isEnabled(const QString &id) const
{
	const QString key = id.toLower();
	// Default-on preserves the existing DSK action for users who already use it.
	return m_preferences.value(key, true);
}

bool PluginIntegrations::actionAvailable(TimelineEventType type) const
{
	if (type != TimelineEventType::DownstreamKeyerScene)
		return true; // Native OBS actions are always offered.
	const QString id = QStringLiteral("downstream-keyer");
	return isSupported(id) && isEnabled(id) &&
	       std::any_of(m_loaded.cbegin(), m_loaded.cend(),
			   [&id](const LoadedObsPlugin &module) { return module.id == id; });
}

void PluginIntegrations::setEnabled(const QString &id, bool enabled)
{
	const QString key = id.toLower();
	if (!isSupported(key))
		return;
	m_preferences.insert(key, enabled);
	if (!m_storagePath.isEmpty()) {
		QDir().mkpath(QFileInfo(m_storagePath).absolutePath());
		QSettings settings(m_storagePath, QSettings::IniFormat);
		settings.setValue(QStringLiteral("plugins/") + key, enabled);
		settings.sync();
		if (settings.status() != QSettings::NoError)
			blog(LOG_WARNING, "[Timeline] Could not save plugin integration selection");
	}
}

void PluginIntegrations::populateMenu(QMenu &menu)
{
	menu.setToolTipsVisible(true);
	menu.clear();
	QAction *builtIn = menu.addAction(QString::fromUtf8(obs_module_text("PluginIntegrations.BuiltIn")));
	builtIn->setEnabled(false);
	menu.addSeparator();
	if (m_loaded.isEmpty()) {
		QAction *empty = menu.addAction(QString::fromUtf8(obs_module_text("PluginIntegrations.NoneLoaded")));
		empty->setEnabled(false);
		return;
	}

	for (const auto &module : m_loaded) {
		QAction *action = menu.addAction(module.name);
		if (!isSupported(module.id)) {
			action->setEnabled(false);
			action->setToolTip(QString::fromUtf8(obs_module_text("PluginIntegrations.UnsupportedTooltip")));
			action->setText(module.name +
					QString::fromUtf8(obs_module_text("PluginIntegrations.UnsupportedSuffix")));
			continue;
		}
		action->setCheckable(true);
		action->setChecked(isEnabled(module.id));
		action->setToolTip(QString::fromUtf8(obs_module_text("PluginIntegrations.EnabledTooltip")));
		const QString id = module.id;
		QObject::connect(action, &QAction::toggled, &menu,
				 [this, id](bool enabled) { setEnabled(id, enabled); });
	}
}
