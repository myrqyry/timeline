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

class ObsExecutor;
class QPushButton;
class QHBoxLayout;
class QScrollArea;

class MacroStripDock : public QWidget {
	Q_OBJECT
public:
	explicit MacroStripDock(QWidget *parent = nullptr, const QString &storageFilePath = QString(),
				const QString &legacyFilePath = QString());
	~MacroStripDock() override;

	void setObsExecutor(ObsExecutor *executor);

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
	QString m_storageFilePath;
	QString m_legacyFilePath;
	QTimer m_saveTimer;
	bool m_autoSaveReady = false;
	bool m_dirty = false;

	void setupUi();
	void connectModel();
	void showAddEditor();
	void showEditEditor(const QString &id);
};
