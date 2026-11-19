#pragma once

#include <QList>
#include <QString>
#include <obs.h>

// id is the internal OBS name; binding is the persisted registerer identity.
struct HotkeyChoice {
	QString id;
	QString label;
	QString group;
	QString binding;
	QString selectionKey() const { return binding.isEmpty() ? id : binding; }
};

struct MoveFilterIdentity {
	QString id;
	QString name;
};

class HotkeyCatalog {
public:
	static QList<HotkeyChoice> collect();
	static obs_hotkey_id resolve(const QString &name, const QString &binding);
	static QString sourceGroup(const QString &sourceName, const QString &hotkeyName,
				   const QList<MoveFilterIdentity> &filters);
};
