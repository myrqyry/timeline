#pragma once

#include <QList>
#include <QString>

// display labels and groups are presentation only; id is the exact OBS hotkey name.
struct HotkeyChoice {
	QString id;
	QString label;
	QString group;
};

struct MoveFilterIdentity {
	QString id;
	QString name;
};

class HotkeyCatalog {
public:
	static QList<HotkeyChoice> collect();
	static QString sourceGroup(const QString &sourceName, const QString &hotkeyName,
				   const QList<MoveFilterIdentity> &filters);
};
