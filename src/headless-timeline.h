#pragma once

#include <QObject>
#include <QJsonArray>
#include "timeline-model.h"
#include "timeline-scheduler.h"

// Forward declaration
class ObsExecutor;

/**
 * HeadlessTimeline
 *
 * A lightweight, non-UI controller for running timelines in the background.
 * Primarily used by ObsExecutor when a TriggerTimeline event fires.
 *
 * Ownership notes:
 * - Owns its own Model, Scheduler, and Executor.
 * - Destructor calls stop() to ensure any nested timelines it spawned are cleaned up.
 * - Should normally be managed via QPointer or parent ownership in the executor.
 */
class HeadlessTimeline : public QObject {
	Q_OBJECT

public:
	explicit HeadlessTimeline(QObject *parent = nullptr);
	~HeadlessTimeline();

	// Load timeline from JSON array
	void load(const QJsonArray &json);

	// Load from file path (returns false if failed)
	bool loadFromFile(const QString &filePath);
	QString getFilePath() const { return currentPath; }
	void setFilePath(const QString &path) { currentPath = path; }

public slots:
	// Start playback
	void start();

	// Stop playback
	void stop();

signals:
	void finished();

private:
	QString currentPath;
	TimelineModel *model;
	TimelineScheduler *scheduler;
	ObsExecutor *executor;
};
