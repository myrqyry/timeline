#include "headless-timeline.h"
#include "obs-executor.h"
#include "timeline-serialization.h"
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
	connect(scheduler, &TimelineScheduler::parameterProgress, executor, &ObsExecutor::executeParameterProgress);

	// Connect scheduler finished to our finished signal
	connect(scheduler, &TimelineScheduler::finished, this, &HeadlessTimeline::finished);
}

HeadlessTimeline::~HeadlessTimeline()
{
	blog(LOG_INFO, "[Timeline] HeadlessTimeline destroyed (path: %s)", currentPath.toUtf8().constData());
	// Ensure we clean up any nested timelines we may have spawned
	stop();
}

void HeadlessTimeline::setAncestorPaths(const QStringList &paths)
{
	executor->setAncestorPaths(paths);
}

void HeadlessTimeline::setObsWebSocketVendor(void *vendor)
{
	executor->setObsWebSocketVendor(vendor);
}

bool HeadlessTimeline::loadFromFile(const QString &filePath)
{
	QJsonDocument doc;
	TimelineDocument settings;
	QString error;
	if (!TimelineSerialization::loadFromFile(filePath, doc, &error) ||
	    !TimelineSerialization::fromJsonDocument(doc, *model, settings, &error)) {
		blog(LOG_WARNING, "[Timeline] Headless timeline load failed: %s (%s)", filePath.toUtf8().constData(),
		     error.toUtf8().constData());
		return false;
	}
	stop();
	scheduler->restart();
	currentPath = filePath;
	scheduler->setLooping(settings.loop);
	scheduler->setEndTime(settings.endTimeSeconds * 1000.0);

	// Fallback: if no non-zero endTime was set, compute one from the event data
	// to prevent infinite background ticking (legacy bare-array files, missing settings, etc.)
	if (scheduler->getEndTime() <= 0.0) {
		double maxTimeMs = 0.0;
		for (const auto &evt : model->getEvents()) {
			double evtEnd =
				evt.timeMs +
				(evt.hasDuration && evt.type != TimelineEventType::FreezeTime ? evt.durationMs : 0.0);
			if (evtEnd > maxTimeMs)
				maxTimeMs = evtEnd;
		}
		scheduler->setEndTime(maxTimeMs > 0.0 ? maxTimeMs + 100.0 : 100.0);
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
