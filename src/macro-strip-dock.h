#pragma once

#include <QWidget>
#include <QPointer>
#include <QHash>
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <obs-frontend-api.h>
#include "macro-button.h"
#include "macro-button-model.h"

class ObsExecutor;
class QPushButton;
class QHBoxLayout;
class QScrollArea;

class MacroStripDock : public QWidget {
	Q_OBJECT
public:
	explicit MacroStripDock(QWidget *parent = nullptr);
	~MacroStripDock() override;

	void setObsExecutor(ObsExecutor *executor);

	// Public accessor for the model so tests and other widgets can drive it.
	MacroButtonModel *buttonModel() { return &m_model; }

	// Reflect the timeline dock's compact-mode setting so the strip's button sizes
	// stay in sync with the toolbar buttons (28 px normally, 24 px in compact).
	void setCompactMode(bool compact);

	// Persistence entry points used by the module-level save/load callbacks
	QJsonObject toJson() const;
	QString toJsonText() const
	{
		QJsonDocument doc(toJson());
		return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
	}
	bool fromJson(const QJsonObject &root, QString *errorMessage = nullptr);

	QString dataFilePath() const;
	bool saveNow();
	bool loadNow();

public slots:
	void addButtonAtEnd();
	void moveLeft(const QString &id);
	void moveRight(const QString &id);
	void editButton(const QString &id);
	void removeButton(const QString &id);

	void onFrontendEvent(enum obs_frontend_event event);

signals:
	void saveRequested();

private slots:
	void rebuildFromModel();
	void onButtonFired(const MacroButton &button);

private:
	QScrollArea *m_scrollArea = nullptr;
	QWidget *m_buttonContainer = nullptr;
	QHBoxLayout *m_buttonLayout = nullptr;
	QPushButton *m_addButton = nullptr;

	MacroButtonModel m_model;
	QPointer<ObsExecutor> m_executor;

	// Mirrors the timeline dock's compact-mode setting so strip buttons stay in
	// sync with the timeline toolbar (Add Event, Snap, Loop, etc.).
	bool m_compactMode = false;

	void setupUi();
	void connectModel();
	void showAddEditor();
	void showEditEditor(const QString &id);
};