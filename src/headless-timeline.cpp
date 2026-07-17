#include "headless-timeline.h"
#include "obs-executor.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <obs-module.h>

HeadlessTimeline::HeadlessTimeline(QObject *parent) : QObject(parent)
{
	blog(LOG_INFO, "[Timeline] HeadlessTimeline created");
	model = new TimelineModel(this);
	scheduler = new TimelineScheduler(model, this);
	executor = new ObsExecutor(this);

	// Connect scheduler to executor
	connect(scheduler, &TimelineScheduler::eventStarted, executor, &ObsExecutor::executeEventStart);
	connect(scheduler, &TimelineScheduler::eventEnded, executor, &ObsExecutor::executeEventEnd);

	// Connect scheduler finished to our finished signal
	connect(scheduler, &TimelineScheduler::finished, this, &HeadlessTimeline::finished);
}

HeadlessTimeline::~HeadlessTimeline()
{
	blog(LOG_INFO, "[Timeline] HeadlessTimeline destroyed (path: %s)", currentPath.toUtf8().constData());
	// Ensure we clean up any nested timelines we may have spawned
	stop();
}

bool HeadlessTimeline::loadFromFile(const QString &filePath)
{
	currentPath = filePath;

	QFile file(filePath);
	if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
		blog(LOG_WARNING, "[Timeline] ┌─ Headless timeline: file open failed");
		blog(LOG_WARNING, "[Timeline] └─ Path : %s", filePath.toUtf8().constData());
		return false;
	}

	QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
	file.close();

	if (doc.isArray()) {
		model->fromJson(doc.array());
	} else if (doc.isObject()) {
		QJsonObject root = doc.object();
		model->fromJson(root["events"].toArray());

		// Optional: Load settings if needed (loop, endTime)
		QJsonObject settings = root["settings"].toObject();
		if (settings.contains("loop")) {
			scheduler->setLooping(settings["loop"].toBool());
		}
		if (settings.contains("endTime")) {
			scheduler->setEndTime(settings["endTime"].toDouble() * 1000.0);
		}
	} else {
		blog(LOG_WARNING, "[Timeline] ┌─ Headless timeline: invalid JSON format");
		blog(LOG_WARNING, "[Timeline] └─ Path : %s", filePath.toUtf8().constData());
		return false;
	}

	blog(LOG_INFO, "[Timeline] Headless timeline loaded: %s", filePath.toUtf8().constData());
	return true;
}

void HeadlessTimeline::start()
{
	if (scheduler) {
		scheduler->start();
		blog(LOG_INFO, "[Timeline] Headless timeline started: %s", currentPath.toUtf8().constData());
	}
}

void HeadlessTimeline::stop()
{
	if (scheduler) {
		scheduler->stop();
	}
	if (executor) {
		executor->cancelAll();
	}
	blog(LOG_INFO, "[Timeline] Headless timeline stopped: %s", currentPath.toUtf8().constData());
}