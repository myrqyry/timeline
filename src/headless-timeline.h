#pragma once

#include <QObject>
#include <QJsonArray>
#include "timeline-model.h"
#include "timeline-scheduler.h"

// Forward declaration
class ObsExecutor;

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
