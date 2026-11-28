#pragma once

#include <QWidget>
#include <QPointer>
#include <QHash>
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTimer>
#include <obs-frontend-api.h>
#include "macro-button.h"
#include "macro-button-model.h"
#include "macro-serialization.h"

class ObsExecutor;
class QPushButton;
class QBoxLayout;
class QTabBar;
class QScrollArea;

class MacroStripDock : public QWidget {
	Q_OBJECT
public:
	explicit MacroStripDock(QWidget *parent = nullptr, const QString &storageFilePath = QString(),
				const QString &legacyFilePath = QString());
	~MacroStripDock() override;

	void setObsExecutor(ObsExecutor *executor);
	void closeIndividualDocks();

	// Public accessor for the model so tests and other widgets can drive it.
	MacroButtonModel *buttonModel() { return &m_model; }

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
	void activateAutoSave();
	bool restoreFromObsJson(const QByteArray &json);

public slots:
	void addButtonAtEnd();
	void moveLeft(const QString &id);
	void moveRight(const QString &id);
	void editButton(const QString &id);
	void editDisplay(const QString &id);
	void removeButton(const QString &id);

	void onFrontendEvent(enum obs_frontend_event event);

signals:
	void saveRequested();
	// Only successful direct Macro Strip edits, never OBS readback or timeline playback.
	void parameterCommitted(const MacroParameterBinding &binding, double value);

protected:
	void changeEvent(QEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	bool eventFilter(QObject *object, QEvent *event) override;

private slots:
	void rebuildFromModel();
	void onButtonFired(const MacroButton &button);

private:
	QScrollArea *m_scrollArea = nullptr;
	QWidget *m_buttonContainer = nullptr;
	QBoxLayout *m_buttonLayout = nullptr;
	QBoxLayout *m_stripLayout = nullptr;
	QTabBar *m_tabs = nullptr;
	QList<MacroStripDefinition> m_strips{MacroStripDefinition{}};
	int m_activeStrip = 0;
	using LayoutSnapshot = QList<QPair<QString, int>>;
	struct LayoutHistory {
		QList<LayoutSnapshot> undo, redo;
	};
	QHash<QString, LayoutHistory> m_layoutHistory;
	QPointer<QWidget> m_layoutOverlay;
	bool m_applyingLayout = false;
	bool m_spacingEdit = false;
	LayoutSnapshot captureLayout() const;
	void rememberLayout();
	void restoreLayout(bool redo);
	void applyLayout(const LayoutSnapshot &snapshot);
	QHash<QString, QPoint> controlPositions() const;
	void animateLayout(const QHash<QString, QPoint> &before);
	void stopLayoutAnimation();
	bool m_editLayout = false;
	QPointer<MacroStripDock> m_workspaceOwner;
	QList<MacroStripDefinition> m_individualStrips;
	QHash<QString, QPointer<MacroStripDock>> m_individualDocks;
	void syncIndividualDocks();
	void updateIndividualDefinition(const MacroStripDefinition &strip);
	MacroStripDefinition individualDefinition() const;
	void addIndividualStrip(bool detach);
	void removeIndividualStrip(const QString &id, bool returnToTabs);
	void showIndividualStrip(const QString &id);
	static QByteArray individualDockId(const QString &id);

	QPushButton *m_addButton = nullptr;

	MacroButtonModel m_model;
	QPointer<ObsExecutor> m_executor;
	QString m_storageFilePath;
	QString m_legacyFilePath;
	QTimer m_saveTimer;
	bool m_autoSaveReady = false;
	bool m_dirty = false;

	void setupUi();
	void updateStripHeight();
	void refreshTheme();
	void connectModel();
	void showAddEditor();
	void refreshStripTabs();
	void setEditLayout(bool editing);
	void commitLayoutDrag(const QString &id, int destination, int gap);
	void selectStrip(int index);
	void addStrip();
	void renameStrip();
	void setStripVertical(bool vertical);
	void removeStrip();
	void editGridCell(const QString &id, int index);
	void editGrid(const QString &id);
	QWidget *createGrid(const MacroButton &button);
	void showEditEditor(const QString &id);
};
