#include "macro-strip-dock.h"

#include "macro-button-widget.h"
#include "macro-serialization.h"
#include "obs-executor.h"
#include "event-editor.h"
#include "theme-helpers.h"

#include <obs-module.h>

#include <QPushButton>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QMenu>
#include <QAction>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>

MacroStripDock::MacroStripDock(QWidget *parent, const QString &storageFilePath)
	: QWidget(parent),
	  m_storageFilePath(storageFilePath)
{
	m_saveTimer.setSingleShot(true);
	m_saveTimer.setInterval(300);
	connect(&m_saveTimer, &QTimer::timeout, this, [this]() { saveNow(); });
	connect(this, &MacroStripDock::saveRequested, this, [this]() {
		if (m_autoSaveReady)
			m_saveTimer.start();
	});
	setupUi();
	connectModel();
	rebuildFromModel();
}

MacroStripDock::~MacroStripDock() = default;

void MacroStripDock::setupUi()
{
	setObjectName("timeline-macro-strip-dock");
	// Match the timeline dock's toolbar row height (controlsLayout uses 2 px
	// margins around 28 px secondary buttons → 32 px total).
	constexpr int kDockHeight = 32;
	setFixedHeight(kDockHeight);
	auto *root = new QHBoxLayout(this);
	root->setContentsMargins(6, 0, 6, 0);
	root->setSpacing(6);

	m_addButton = new QPushButton(this);
	m_addButton->setText("+");
	m_addButton->setFixedSize(28, 28);
	connect(m_addButton, &QPushButton::clicked, this, &MacroStripDock::addButtonAtEnd);
	root->addWidget(m_addButton);

	m_scrollArea = new QScrollArea(this);
	m_scrollArea->setWidgetResizable(true);
	m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	m_scrollArea->setFixedHeight(kDockHeight);

	m_buttonContainer = new QWidget(m_scrollArea);
	m_buttonLayout = new QHBoxLayout(m_buttonContainer);
	m_buttonLayout->setContentsMargins(0, 0, 0, 0);
	m_buttonLayout->setSpacing(4);
	m_buttonLayout->addStretch();

	m_scrollArea->setWidget(m_buttonContainer);
	root->addWidget(m_scrollArea, 1);
}

void MacroStripDock::connectModel()
{
	connect(&m_model, &MacroButtonModel::modelChanged, this, &MacroStripDock::rebuildFromModel);
}

void MacroStripDock::setObsExecutor(ObsExecutor *executor)
{
	m_executor = executor;
}

void MacroStripDock::rebuildFromModel()
{
	QLayoutItem *child;
	while ((child = m_buttonLayout->takeAt(0)) != nullptr) {
		if (child->widget())
			child->widget()->deleteLater();
		delete child;
	}

	const auto buttons = m_model.buttons();
	for (const auto &b : buttons) {
		auto *w = new MacroButtonWidget(b, m_buttonContainer);
		connect(w, &MacroButtonWidget::fireRequested, this, &MacroStripDock::onButtonFired);
		w->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(w, &QWidget::customContextMenuRequested, this, [this, id = b.id](const QPoint &pos) {
			QPointer<MacroStripDock> safeThis(this);
			QMenu menu;
			QAction *edit = menu.addAction(QObject::tr("Edit"));
			QAction *left = menu.addAction(QObject::tr("Move Left"));
			QAction *right = menu.addAction(QObject::tr("Move Right"));
			menu.addSeparator();
			QAction *del = menu.addAction(QObject::tr("Delete"));
			QAction *picked = menu.exec(static_cast<QWidget *>(QObject::sender())->mapToGlobal(pos));
			if (!safeThis || !picked)
				return;
			if (picked == edit)
				safeThis->editButton(id);
			else if (picked == left)
				safeThis->moveLeft(id);
			else if (picked == right)
				safeThis->moveRight(id);
			else if (picked == del)
				safeThis->removeButton(id);
		});
		m_buttonLayout->addWidget(w);
	}
	m_buttonLayout->addStretch();
	emit saveRequested();
}

void MacroStripDock::onButtonFired(const MacroButton &button)
{
	if (!m_executor)
		return;
	TimelineEvent evt;
	evt.id = button.id;
	evt.type = button.type;
	evt.sourceName = button.sourceName;
	evt.filterName = button.filterName;
	evt.data = button.data;
	evt.color = button.color;
	evt.label = button.label;
	m_executor->executeEventStart(evt);
}

void MacroStripDock::addButtonAtEnd()
{
	showAddEditor();
}

void MacroStripDock::showAddEditor()
{
	EventEditor editor(0.0, this);
	if (editor.exec() != QDialog::Accepted)
		return;

	TimelineEvent evt;
	editor.getUpdatedEvent(evt);

	MacroButton b;
	b.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	b.type = evt.type;
	b.sourceName = evt.sourceName;
	b.filterName = evt.filterName;
	b.color = evt.color;
	b.label = evt.label;
	if (evt.data.canConvert<QString>())
		b.data = evt.data.toString();
	if (b.label.isEmpty())
		b.label = b.sourceName;
	m_model.addButton(b);
}

void MacroStripDock::editButton(const QString &id)
{
	showEditEditor(id);
}

void MacroStripDock::showEditEditor(const QString &id)
{
	auto existing = m_model.findById(id);
	if (!existing)
		return;

	TimelineEvent evt;
	evt.id = existing->id;
	evt.type = existing->type;
	evt.sourceName = existing->sourceName;
	evt.filterName = existing->filterName;
	evt.color = existing->color;
	evt.label = existing->label;
	evt.data = existing->data;
	EventEditor editor(evt, this);
	if (editor.exec() != QDialog::Accepted)
		return;

	editor.getUpdatedEvent(evt);

	MacroButton updated;
	updated.id = existing->id;
	updated.type = evt.type;
	updated.sourceName = evt.sourceName;
	updated.filterName = evt.filterName;
	updated.color = evt.color;
	updated.label = evt.label;
	if (evt.data.canConvert<QString>())
		updated.data = evt.data.toString();
	else
		updated.data = QString();
	m_model.replaceButtonById(id, updated);
}

void MacroStripDock::moveLeft(const QString &id)
{
	const auto buttons = m_model.buttons();
	for (int i = 0; i < buttons.size(); ++i) {
		if (buttons[i].id == id) {
			m_model.moveButton(id, std::max(0, i - 1));
			return;
		}
	}
}

void MacroStripDock::moveRight(const QString &id)
{
	const auto buttons = m_model.buttons();
	for (int i = 0; i < buttons.size(); ++i) {
		if (buttons[i].id == id) {
			m_model.moveButton(id, std::min(static_cast<int>(buttons.size()) - 1, i + 1));
			return;
		}
	}
}

void MacroStripDock::removeButton(const QString &id)
{
	m_model.removeButton(id);
}

QJsonObject MacroStripDock::toJson() const
{
	return MacroSerialization::toJsonObject(m_model);
}

bool MacroStripDock::fromJson(const QJsonObject &root, QString *errorMessage)
{
	return MacroSerialization::fromJsonDocument(QJsonDocument(root), m_model, errorMessage);
}

QString MacroStripDock::dataFilePath() const
{
	if (!m_storageFilePath.isEmpty())
		return m_storageFilePath;
	const QString dir = QDir::homePath() + "/.config/obs-studio/plugins/timeline/data";
	QDir().mkpath(dir);
	return dir + "/macro_buttons.json";
}

bool MacroStripDock::saveNow()
{
	QString err;
	const bool ok = MacroSerialization::saveToFile(dataFilePath(), toJson(), &err);
	if (!ok)
		blog(LOG_WARNING, "[MacroStrip] save failed: %s", err.toUtf8().constData());
	return ok;
}

void MacroStripDock::activateAutoSave()
{
	// Delay autosaving until the OBS profile and on-disk macro state have loaded.
	m_autoSaveReady = true;
}

bool MacroStripDock::restoreFromObsJson(const QByteArray &json)
{
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
	if (parseError.error != QJsonParseError::NoError) {
		blog(LOG_WARNING, "[MacroStrip] Invalid OBS profile macro JSON: %s",
		     parseError.errorString().toUtf8().constData());
		return false;
	}
	QString error;
	if (!MacroSerialization::fromJsonDocument(document, m_model, &error)) {
		blog(LOG_WARNING, "[MacroStrip] Ignoring invalid OBS profile macros: %s", error.toUtf8().constData());
		return false;
	}
	m_saveTimer.stop();
	return saveNow(); // QSaveFile commits the verified snapshot atomically.
}

bool MacroStripDock::loadNow()
{
	const QString path = dataFilePath();
	if (!QFile::exists(path))
		return false;
	QJsonDocument doc;
	QString err;
	if (!MacroSerialization::loadFromFile(path, doc, &err)) {
		const QString broken = path + ".broken-" + QString::number(QDateTime::currentSecsSinceEpoch());
		QFile::rename(path, broken);
		blog(LOG_WARNING, "[MacroStrip] %s; renamed to %s", err.toUtf8().constData(),
		     broken.toUtf8().constData());
		return false;
	}
	return fromJson(doc.object(), &err);
}

void MacroStripDock::onFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_THEME_CHANGED:
		rebuildFromModel();
		break;
	default:
		break;
	}
}