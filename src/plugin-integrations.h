#pragma once

#include "event-types.h"

#include <QHash>
#include <QList>
#include <QString>

class QMenu;

// One integration adapter at a time; detection never changes OBS's own plugins.
struct LoadedObsPlugin {
	QString id;
	QString name;
};

class PluginIntegrations {
public:
	static PluginIntegrations &instance();

	void setStoragePath(const QString &path);
	void refreshLoadedModules();
	void setDetectedModules(const QList<LoadedObsPlugin> &modules);
	QList<LoadedObsPlugin> detectedModules() const { return m_loaded; }

	bool actionAvailable(TimelineEventType type) const;
	bool isSupported(const QString &id) const;
	bool isEnabled(const QString &id) const;
	void setEnabled(const QString &id, bool enabled);
	void populateMenu(QMenu &menu);

private:
	QString m_storagePath;
	QList<LoadedObsPlugin> m_loaded;
	QHash<QString, bool> m_preferences;
};
