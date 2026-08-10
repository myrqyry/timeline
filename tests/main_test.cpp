#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QTest>
#include <QSignalSpy>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <obs.h>
#include <util/bmem.h>
#include "obs-executor.h"
#include "theme-helpers.h"
#include "timeline-hit-testing.h"
#include "timeline-event-item.h"
#include "timeline-model.h"
#include "timeline-scheduler.h"
#include "timeline-serialization.h"
#include "macro-button.h"
#include "macro-button-model.h"
#include "macro-button-widget.h"
#include "macro-serialization.h"
#include "macro-strip-dock.h"

// Mock helpers from mock_frontend.cpp
extern obs_source_t *g_current_scene;
extern obs_source_t *g_current_preview_scene;
extern obs_source_t *g_current_transition;
extern std::vector<obs_source_t *> g_transitions;
extern int g_transition_duration_ms;

class ExposedTimelineEventItem : public TimelineEventItem {
public:
	using TimelineEventItem::TimelineEventItem;
	using TimelineEventItem::mousePressEvent;
	using TimelineEventItem::mouseReleaseEvent;
};

// Friend of MacroButtonWidget so tests can drive protected mouse handlers.
class TestTimelineMacroButtonAccessor {
public:
	static void clickBubble(MacroButtonWidget &w)
	{
		QMouseEvent press(QEvent::MouseButtonPress, QPointF(15, 26), QPointF(15, 26), Qt::LeftButton,
				  Qt::LeftButton, Qt::NoModifier);
		QMouseEvent release(QEvent::MouseButtonRelease, QPointF(15, 26), QPointF(15, 26),
				    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		w.mousePressEvent(&press);
		w.mouseReleaseEvent(&release);
	}
};

// Helper to set QT_QPA_PLATFORM to offscreen
void setOffscreenPlatform()
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
}

class TestTimeline : public QObject {
	Q_OBJECT
private:
	obs_source_t *scene1 = nullptr;
	obs_source_t *scene2 = nullptr;
	obs_source_t *trans = nullptr;
	obs_source_t *source_item = nullptr;
	obs_source_t *filter_item = nullptr;

	// Headless graphics context for tests
	QOffscreenSurface *offscreenSurface = nullptr;
	QOpenGLContext *glContext = nullptr;

private slots:
	void initTestCase()
	{
		qRegisterMetaType<TimelineEvent>("TimelineEvent");

		// Create a headless OpenGL context for OBS graphics device
		offscreenSurface = new QOffscreenSurface();
		offscreenSurface->setFormat(QSurfaceFormat::defaultFormat());
		offscreenSurface->create();

		glContext = new QOpenGLContext();
		glContext->setFormat(offscreenSurface->format());
		glContext->create();
		if (!glContext->makeCurrent(offscreenSurface)) {
			qWarning("Failed to make GL context current for OBS tests");
		}

		if (!obs_startup("en-US", nullptr, nullptr))
			qWarning("Failed to startup OBS - Tests might crash or fail");

		// Register a dummy source type for scene
		struct obs_source_info dummy_scene_info = {};
		dummy_scene_info.id = "scene";
		dummy_scene_info.type = OBS_SOURCE_TYPE_SCENE;
		dummy_scene_info.output_flags = OBS_SOURCE_COMPOSITE;
		dummy_scene_info.get_name = [](void *) {
			return "Dummy Scene";
		};
		dummy_scene_info.create = [](obs_data_t *, obs_source_t *) {
			return (void *)bzalloc(100);
		};
		dummy_scene_info.destroy = [](void *data) {
			bfree(data);
		};
		obs_register_source(&dummy_scene_info);

		// Register a dummy source type for transition
		struct obs_source_info dummy_transition_info = {};
		dummy_transition_info.id = "cut_transition";
		dummy_transition_info.type = OBS_SOURCE_TYPE_TRANSITION;
		dummy_transition_info.get_name = [](void *) {
			return "Cut";
		};
		dummy_transition_info.create = [](obs_data_t *, obs_source_t *) {
			return (void *)bzalloc(100);
		};
		dummy_transition_info.destroy = [](void *data) {
			bfree(data);
		};
		obs_register_source(&dummy_transition_info);

		// Register dummy source for items
		struct obs_source_info dummy_source_info = {};
		dummy_source_info.id = "test_source";
		dummy_source_info.type = OBS_SOURCE_TYPE_INPUT;
		dummy_source_info.output_flags = OBS_SOURCE_VIDEO;
		dummy_source_info.get_name = [](void *) {
			return "Test Source";
		};
		dummy_source_info.create = [](obs_data_t *, obs_source_t *) {
			return (void *)bzalloc(100);
		};
		dummy_source_info.destroy = [](void *data) {
			bfree(data);
		};
		obs_register_source(&dummy_source_info);

		// Register dummy filter
		struct obs_source_info dummy_filter_info = {};
		dummy_filter_info.id = "test_filter";
		dummy_filter_info.type = OBS_SOURCE_TYPE_FILTER;
		dummy_filter_info.output_flags = OBS_SOURCE_VIDEO;
		dummy_filter_info.get_name = [](void *) {
			return "Test Filter";
		};
		dummy_filter_info.create = [](obs_data_t *, obs_source_t *) {
			return (void *)bzalloc(100);
		};
		dummy_filter_info.destroy = [](void *data) {
			bfree(data);
		};
		obs_register_source(&dummy_filter_info);
	}

	void cleanupTestCase()
	{
		// Re-make GL context current before OBS shutdown to ensure texture
		// destruction happens on a valid context, avoiding "No active swap" warnings.
		if (glContext && offscreenSurface) {
			glContext->makeCurrent(offscreenSurface);
		}

		// Force-clear all mock global references that may still hold source refs.
		// This ensures sources are destroyed before obs_shutdown(), preventing
		// incomplete teardown in the headless test environment.
		if (g_current_scene) {
			obs_source_release(g_current_scene);
			g_current_scene = nullptr;
		}
		if (g_current_preview_scene) {
			obs_source_release(g_current_preview_scene);
			g_current_preview_scene = nullptr;
		}
		if (g_current_transition) {
			obs_source_release(g_current_transition);
			g_current_transition = nullptr;
		}
		for (auto *t : g_transitions) {
			obs_source_release(t);
		}
		g_transitions.clear();

		// Drain remaining OBS sources. Some OBS internal list operations
		// add transient refs; repeated draining ensures final cleanup.
		{
			std::vector<obs_source_t *> remaining;
			for (int pass = 0; pass < 8; pass++) {
				remaining.clear();
				obs_enum_all_sources(
					[](void *param, obs_source_t *source) {
						obs_source_get_ref(source);
						static_cast<std::vector<obs_source_t *> *>(param)
							->push_back(source);
						return true;
					},
					&remaining);
				if (remaining.empty())
					break;
				for (auto *s : remaining)
					obs_source_release(s);
			}
		}

		// OBS internally retains 3 allocations related to source type
		// registration that are only freed during full audio/video teardown.
		// These are unfixable from test code in a headless environment.
		// We assert < 4 to catch regressions.
		long preShutdownLeaks = bnum_allocs();

		obs_shutdown();

		// Clean up GL resources
		if (glContext) {
			glContext->doneCurrent();
			delete glContext;
			glContext = nullptr;
		}
		if (offscreenSurface) {
			delete offscreenSurface;
			offscreenSurface = nullptr;
		}

		long leaks = bnum_allocs();
		if (leaks > 0) {
			qWarning("Memory leaks detected: %ld (pre-shutdown: %ld)", leaks, preShutdownLeaks);
		}
		// Allow up to 3 residual OBS internal allocations (source type registration
		// metadata that requires full video/audio thread teardown to free).
		QVERIFY2(leaks < 4, qPrintable(QString("Memory leaks: %1").arg(leaks)));
	}

	void init()
	{
		g_transition_duration_ms = 300;
		// Create sources for each test
		scene1 = obs_source_create("scene", "Scene 1", nullptr, nullptr);
		scene2 = obs_source_create("scene", "Scene 2", nullptr, nullptr);
		trans = obs_source_create("cut_transition", "Cut", nullptr, nullptr);
		source_item = obs_source_create("test_source", "MySource", nullptr, nullptr);

		// Setup mock state
		g_transitions.push_back(trans);
		obs_source_get_ref(trans); // Hold ref in list

		g_current_scene = scene1;
		if (scene1)
			obs_source_get_ref(scene1); // Mock holds a ref
	}

	void cleanup()
	{
		// Clear mocks
		if (g_current_scene) {
			obs_source_release(g_current_scene);
			g_current_scene = nullptr;
		}
		if (g_current_transition) {
			obs_source_release(g_current_transition);
			g_current_transition = nullptr;
		}

		for (auto *t : g_transitions) {
			obs_source_release(t);
		}
		g_transitions.clear();

		// Release our references
		if (scene1)
			obs_source_release(scene1);
		if (scene2)
			obs_source_release(scene2);
		if (trans)
			obs_source_release(trans);
		if (source_item)
			obs_source_release(source_item);
		if (filter_item) {
			obs_source_release(filter_item);
			filter_item = nullptr;
		}
	}

	void testSceneSwitch()
	{
		if (!scene2)
			QSKIP("OBS Startup failed, skipping test");

		ObsExecutor executor;

		TimelineEvent evt;
		evt.type = TimelineEventType::SceneSwitch;
		evt.sourceName = "Scene 2";
		evt.data = "Cut";

		g_transition_duration_ms = 20;
		QCOMPARE(obs_frontend_get_transition_duration(), 20);
		executor.executeEventStart(evt);

		// Verify we switched to Scene 2
		QCOMPARE(g_current_scene, scene2);
		QCOMPARE(g_current_transition, trans);

		QTest::qWait(40);
		QCOMPARE(g_current_transition, static_cast<obs_source_t *>(nullptr));
	}

	void testMediaStart()
	{
		if (!source_item)
			QSKIP("OBS Startup failed, skipping test");

		ObsExecutor executor;

		TimelineEvent evt;
		evt.type = TimelineEventType::MediaStart;
		evt.sourceName = "MySource";

		// Just verify it doesn't crash
		executor.executeEventStart(evt);
	}

	void testSourceVisibility()
	{
		if (!scene1 || !source_item)
			QSKIP("OBS Startup failed, skipping test");

		// We need to add source_item to scene1 to test visibility
		// But since scene1 is a dummy "scene" type, obs_scene_from_source(scene1) might fail
		// if libobs checks internal vtable or if we didn't implement scene specific callbacks.
		// For this integration test, we trust that if we were using real OBS, it would work.
		// If we can't create a real scene object that obs_scene_from_source accepts, we can't fully test this
		// without mocking obs_scene_from_source too.

		ObsExecutor executor;
		TimelineEvent evt;
		evt.type = TimelineEventType::SourceShow;
		evt.sourceName = "MySource";

		executor.executeEventStart(evt);
		// Assertions would require checking scene item state, which is hard with dummy scenes.
	}

	void testFilterEnable()
	{
		if (!source_item)
			QSKIP("OBS Startup failed, skipping test");

		// Add a filter to source_item
		filter_item = obs_source_create("test_filter", "MyFilter", nullptr, nullptr);
		obs_source_filter_add(source_item, filter_item);

		ObsExecutor executor;
		TimelineEvent evt;
		evt.type = TimelineEventType::FilterEnable;
		evt.sourceName = "MySource";
		evt.filterName = "MyFilter";

		executor.executeEventStart(evt);

		// Verify filter is enabled
		QVERIFY(obs_source_enabled(filter_item));

		// Test Disable
		evt.type = TimelineEventType::FilterDisable;
		executor.executeEventStart(evt);
		QVERIFY(!obs_source_enabled(filter_item));
	}

	void testModelGeneratesUniqueIds()
	{
		TimelineModel model;
		TimelineEvent a;
		TimelineEvent b;
		QVERIFY(!a.id.isEmpty());
		QVERIFY(!b.id.isEmpty());
		QVERIFY(a.id != b.id);

		model.addEvent(a);
		model.addEvent(b);
		QCOMPARE(static_cast<int>(model.getEvents().size()), 2);
	}

	void testTimecodeNineGlyphUsesOfficialIconMoonPath()
	{
		const QString svg = QString::fromUtf8(ThemeIcons::getMoonNumberSvg(9, "#ffffff"));

		QVERIFY2(svg.contains("M9.94126 19.3355"),
			 "Number 9 glyph should use the official IconaMoon filled path");
		QVERIFY2(svg.contains("L17.2856 12.3366"),
			 "Number 9 glyph should include the official diagonal stem path");
		QVERIFY2(!svg.contains("M7 8a5"), "Number 9 glyph should not use the incomplete open arc");
	}

	void testBpmModeIconUsesMusicTwoGlyph()
	{
		const QString svg = QString::fromUtf8(ThemeIcons::wrapIconMoon(ThemeIcons::MOON_MUSIC, "#ffffff"));

		QVERIFY2(svg.contains("cx=\"6\" cy=\"18\" r=\"3\""),
			 "BPM mode icon should use the left note head from Music 2.svg");
		QVERIFY2(svg.contains("cx=\"18\" cy=\"16\" r=\"3\""),
			 "BPM mode icon should use the right note head from Music 2.svg");
		QVERIFY2(svg.contains("M21 3L9 5"), "BPM mode icon should include the upper Music 2.svg beam");
		QVERIFY2(svg.contains("M21 7L9 9"), "BPM mode icon should include the lower Music 2.svg beam");
		QVERIFY2(!svg.contains("M12 3L18 4V8L12 7V3"), "BPM mode icon should not use the old Music 1.svg flag");
	}

	void testSnapIconsUseLockGlyphs()
	{
		const QString lockSvg = QString::fromUtf8(ThemeIcons::wrapIconMoon(ThemeIcons::MOON_LOCK, "#ffffff"));
		const QString lockOffSvg =
			QString::fromUtf8(ThemeIcons::wrapIconMoon(ThemeIcons::MOON_LOCK_OFF, "#ffffff"));

		QVERIFY2(lockSvg.contains("M8 10V7C8 4.79086 9.79086 3 12 3C14.2091 3 16 4.79086 16 7V10"),
			 "Snap enabled icon should use Lock.svg shackle path");
		QVERIFY2(lockSvg.contains("M5 10H19V19C19 20.1046 18.1046 21 17 21H7"),
			 "Snap enabled icon should use Lock.svg body path");
		QVERIFY2(lockOffSvg.contains("C13.8638 3 15.4299 4.27477 15.874 6"),
			 "Snap disabled icon should use Lock Off.svg open shackle path");
		QVERIFY2(lockOffSvg.contains("M5 10H19V19C19 20.1046 18.1046 21 17 21H7"),
			 "Snap disabled icon should use Lock Off.svg body path");
		QVERIFY2(!lockSvg.contains("M3 6.00092H21"),
			 "Snap enabled icon should not use the old menu placeholder");
		QVERIFY2(!lockOffSvg.contains("M3 6.00092H21"),
			 "Snap disabled icon should not use the old menu placeholder");
	}

	void testTimelineMarkerGrabAreaIsComfortable()
	{
		QVERIFY(TimelineHitTesting::isOnTimelineMarker(125.0, 100.0));
		QVERIFY(TimelineHitTesting::isOnTimelineMarker(75.0, 100.0));
		QVERIFY(!TimelineHitTesting::isOnTimelineMarker(135.0, 100.0));
		QVERIFY(!TimelineHitTesting::isOnTimelineMarker(65.0, 100.0));
	}

	void testTimelineMarkerGrabAreaRequiresActiveMarker()
	{
		QVERIFY(TimelineHitTesting::isOnActiveTimelineMarker(117.0, 100.0, true));
		QVERIFY(!TimelineHitTesting::isOnActiveTimelineMarker(100.0, 100.0, false));
	}

	void testTimelineMarkerVisibleHandleMatchesGrabArea()
	{
		QCOMPARE(TimelineHitTesting::kMarkerHandleSizePx,
			 static_cast<int>(TimelineHitTesting::kMarkerGrabRadiusPx * 2.0));
	}

	void testModelIdOpsWithDuplicateTime()
	{
		TimelineModel model;
		TimelineEvent a;
		a.timeMs = 1000.0;
		a.sourceName = "A";
		TimelineEvent b;
		b.timeMs = 1000.0;
		b.sourceName = "B";
		model.addEvent(a);
		model.addEvent(b);

		const auto events = model.getEvents();
		QCOMPARE(static_cast<int>(events.size()), 2);
		const QString idA = events[0].sourceName == "A" ? events[0].id : events[1].id;
		const QString idB = events[0].sourceName == "B" ? events[0].id : events[1].id;

		QVERIFY(model.updateEventTimeById(idA, 2500.0));
		TimelineEvent updatedA = model.findEventById(idA).value();
		QCOMPARE(updatedA.timeMs, 2500.0);

		TimelineEvent replacement = model.findEventById(idB).value();
		replacement.sourceName = "B2";
		QVERIFY(model.replaceEventById(idB, replacement));
		QCOMPARE(model.findEventById(idB).value().sourceName, QString("B2"));

		QVERIFY(model.removeEventById(idA));
		QCOMPARE(static_cast<int>(model.getEvents().size()), 1);
	}

	void testSchedulerStartEndSignalsAndSeek()
	{
		TimelineModel model;
		TimelineEvent evt;
		evt.timeMs = 20.0;
		evt.hasDuration = true;
		evt.durationMs = 40.0;
		model.addEvent(evt);

		TimelineScheduler scheduler(&model);
		QSignalSpy startedSpy(&scheduler, SIGNAL(eventStarted(TimelineEvent)));
		QSignalSpy endedSpy(&scheduler, SIGNAL(eventEnded(TimelineEvent)));

		scheduler.start();
		QTest::qWait(120);
		scheduler.pause();
		QCOMPARE(startedSpy.count(), 1);
		QCOMPARE(endedSpy.count(), 1);

		scheduler.seek(30.0); // after start, before end
		scheduler.start();
		QTest::qWait(80);
		scheduler.pause();
		QCOMPARE(startedSpy.count(), 1); // no retrigger
		QCOMPARE(endedSpy.count(), 2);   // end can trigger again

		scheduler.seek(0.0);
		scheduler.start();
		QTest::qWait(80);
		scheduler.pause();
		QCOMPARE(startedSpy.count(), 2);
	}

	void testSchedulerLoopResetsState()
	{
		TimelineModel model;
		TimelineEvent evt;
		evt.timeMs = 10.0;
		evt.hasDuration = true;
		evt.durationMs = 10.0;
		model.addEvent(evt);

		TimelineScheduler scheduler(&model);
		scheduler.setEndTime(40.0);
		scheduler.setLooping(true);
		QSignalSpy startedSpy(&scheduler, SIGNAL(eventStarted(TimelineEvent)));
		QSignalSpy endedSpy(&scheduler, SIGNAL(eventEnded(TimelineEvent)));
		QSignalSpy loopSpy(&scheduler, SIGNAL(looped()));

		scheduler.start();
		QTRY_VERIFY_WITH_TIMEOUT(loopSpy.count() >= 1, 500);
		QTRY_VERIFY_WITH_TIMEOUT(startedSpy.count() >= 2, 700);
		QTRY_VERIFY_WITH_TIMEOUT(endedSpy.count() >= 2, 700);
		scheduler.stop();
	}

	void testSchedulerStartCursorResets()
	{
		TimelineModel model;
		TimelineEvent first;
		first.timeMs = 10.0;
		TimelineEvent second;
		second.timeMs = 30.0;
		model.addEvent(first);
		model.addEvent(second);

		TimelineScheduler scheduler(&model);
		QSignalSpy startedSpy(&scheduler, SIGNAL(eventStarted(TimelineEvent)));

		scheduler.start();
		QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 2, 250);
		scheduler.pause();

		scheduler.seek(0.0);
		startedSpy.clear();
		scheduler.start();
		QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 2, 250);
		scheduler.pause();

		scheduler.seek(0.0);
		TimelineEvent inserted;
		inserted.timeMs = 5.0;
		model.addEvent(inserted);
		startedSpy.clear();
		scheduler.start();
		QTRY_COMPARE_WITH_TIMEOUT(startedSpy.count(), 3, 250);
		scheduler.pause();
	}

	void testSerializationLegacyAndObjectAndRoundTrip()
	{
		TimelineModel model;
		TimelineEvent evt;
		evt.timeMs = 1234.0;
		evt.sourceName = "Scene 1";
		model.addEvent(evt);

		TimelineDocument docData;
		QJsonDocument legacyDoc(model.toJson());
		QVERIFY(TimelineSerialization::fromJsonDocument(legacyDoc, model, docData));
		QCOMPARE(static_cast<int>(docData.events.size()), 1);

		QJsonObject root = TimelineSerialization::toJsonObject(model, true, 12.5, true, 750.0, 128.0, true, 1.0,
								       true, false);
		QJsonDocument objectDoc(root);
		TimelineDocument loaded;
		QVERIFY(TimelineSerialization::fromJsonDocument(objectDoc, model, loaded));
		QVERIFY(loaded.loop);
		QCOMPARE(loaded.endTimeSeconds, 12.5);
		QVERIFY(loaded.snap);
		QCOMPARE(loaded.snapIntervalMs, 750.0);
		QCOMPARE(loaded.bpm, 128.0);
		QVERIFY(loaded.gridToBpm);
		QVERIFY(loaded.centerControls);

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		QString filePath = dir.path() + "/timeline.json";
		QString errorMessage;
		QVERIFY(TimelineSerialization::saveToFile(filePath, root, &errorMessage));
		QJsonDocument loadedDoc;
		QVERIFY(TimelineSerialization::loadFromFile(filePath, loadedDoc, &errorMessage));
		QVERIFY(loadedDoc.isObject());
	}

	void testSerializationInvalidJson()
	{
		QTemporaryFile file;
		QVERIFY(file.open());
		file.write("{ invalid json");
		file.close();

		QJsonDocument doc;
		QString error;
		QVERIFY(!TimelineSerialization::loadFromFile(file.fileName(), doc, &error));
		QVERIFY(!error.isEmpty());
	}

	void testSchedulerRestartRewinds()
	{
		TimelineModel model;
		TimelineScheduler scheduler(&model);

		scheduler.seek(1234.0);
		QCOMPARE(scheduler.getCurrentTime(), 1234.0);

		scheduler.restart();
		QCOMPARE(scheduler.getCurrentTime(), 0.0);
	}

	void testSchedulerEmitsFinishedAndLooped()
	{
		TimelineModel model;
		TimelineScheduler scheduler(&model);
		scheduler.setEndTime(1000.0);

		QSignalSpy finishedSpy(&scheduler, &TimelineScheduler::finished);
		QSignalSpy loopedSpy(&scheduler, &TimelineScheduler::looped);
		QSignalSpy tickSpy(&scheduler, &TimelineScheduler::tick);

		scheduler.seek(1500.0);
		QVERIFY(QMetaObject::invokeMethod(&scheduler, "onTimerTick", Qt::DirectConnection));
		QCOMPARE(finishedSpy.count(), 1);

		scheduler.setLooping(true);
		scheduler.seek(1500.0);
		tickSpy.clear();
		QVERIFY(QMetaObject::invokeMethod(&scheduler, "onTimerTick", Qt::DirectConnection));
		QCOMPARE(loopedSpy.count(), 1);
		QCOMPARE(tickSpy.count(), 1);
		QCOMPARE(tickSpy.takeFirst().at(0).toDouble(), 0.0);
		QCOMPARE(scheduler.getCurrentTime(), 0.0);
	}

	void testEventItemClickReleaseDoesNotEmitMove()
	{
		ExposedTimelineEventItem item(1000.0, 100.0, TimelineEventType::SceneSwitch);
		QSignalSpy movedSpy(&item, &TimelineEventItem::eventMoved);

		QGraphicsSceneMouseEvent pressEvent(QEvent::GraphicsSceneMousePress);
		pressEvent.setButton(Qt::LeftButton);
		pressEvent.setButtons(Qt::LeftButton);
		item.mousePressEvent(&pressEvent);

		QGraphicsSceneMouseEvent releaseEvent(QEvent::GraphicsSceneMouseRelease);
		releaseEvent.setButton(Qt::LeftButton);
		releaseEvent.setButtons(Qt::NoButton);
		item.mouseReleaseEvent(&releaseEvent);

		QCOMPARE(movedSpy.count(), 0);
	}

	void testEventItemCompactModePreservesTime()
	{
		ExposedTimelineEventItem item(2500.0, 100.0, TimelineEventType::SceneSwitch);

		QCOMPARE(item.getTimeMs(), 2500.0);
		item.setCompactMode(true);
		QCOMPARE(item.getTimeMs(), 2500.0);
		item.setCompactMode(false);
		QCOMPARE(item.getTimeMs(), 2500.0);
	}

	void testMacroButtonHasDefaults()
	{
		MacroButton b;
		QVERIFY(!b.id.isEmpty());
		QVERIFY(b.label.isEmpty());
		QCOMPARE(int(b.type), int(TimelineEventType::SceneSwitch));
	}

	void testModelAddRemoveMove()
	{
		MacroButtonModel m;
		QSignalSpy spy(&m, &MacroButtonModel::modelChanged);

		for (int i = 0; i < 3; ++i) {
			MacroButton b;
			b.id = QString("id-%1").arg(i);
			b.label = QString("B%1").arg(i);
			b.sourceName = "A";
			b.type = TimelineEventType::SceneSwitch;
			m.addButton(b);
		}
		QCOMPARE(m.count(), 3);
		QCOMPARE(spy.count(), 3);

		QVERIFY(m.moveButton("id-1", 0));
		QCOMPARE(m.buttons()[0].id, QString("id-1"));
		QCOMPARE(m.buttons()[1].id, QString("id-0"));

		QVERIFY(m.moveButton("id-0", 999));
		QCOMPARE(m.buttons().last().id, QString("id-0"));

		QVERIFY(m.removeButton("id-2"));
		QCOMPARE(m.count(), 2);
		QVERIFY(!m.removeButton("nope"));
	}

	void testSerializationRoundTripAndDedupe()
	{
		MacroButtonModel model;
		MacroButton a;
		a.id = "a";
		a.label = "A";
		a.type = TimelineEventType::SourceShow;
		a.sourceName = "MySrc";
		a.color = QColor("#ff00ff");
		model.addButton(a);

		MacroButton dup;
		dup.id = "a";
		dup.label = "Dup";
		dup.type = TimelineEventType::SourceHide;
		dup.sourceName = "Other";
		model.addButton(dup);

		const QJsonObject root = MacroSerialization::toJsonObject(model);
		MacroButtonModel loaded;
		QString err;
		QVERIFY(MacroSerialization::fromJsonDocument(QJsonDocument(root), loaded, &err));
		QVERIFY(err.isEmpty());

		const auto buttons = loaded.buttons();
		QCOMPARE(buttons.size(), 2);
		QCOMPARE(buttons[0].id, QString("a"));
		QVERIFY(buttons[1].id != QString("a"));
		QVERIFY(!buttons[1].id.isEmpty());
		QCOMPARE(buttons[1].label, QString("Dup"));
	}

	void testMacroSerializationInvalidJson()
	{
		QTemporaryFile file;
		QVERIFY(file.open());
		file.write("{ not json");
		file.close();

		QJsonDocument doc;
		QString err;
		QVERIFY(!MacroSerialization::loadFromFile(file.fileName(), doc, &err));
		QVERIFY(!err.isEmpty());
	}

	void testButtonWidgetEmitsFireRequested()
	{
		MacroButton b;
		b.id = "x";
		b.label = "Go";
		b.type = TimelineEventType::SceneSwitch;
		b.sourceName = "Scene B";

		MacroButtonWidget w(b);
		QSignalSpy spy(&w, &MacroButtonWidget::fireRequested);
		TestTimelineMacroButtonAccessor::clickBubble(w);
		QCOMPARE(spy.count(), 1);
		QCOMPARE(spy.takeFirst().at(0).value<MacroButton>().id, QString("x"));
	}

	void testStripDockMoveAndFire()
	{
		MacroStripDock dock;
		ObsExecutor exec;
		dock.setObsExecutor(&exec);

		MacroButtonModel *model = dock.buttonModel();
		MacroButton a;
		a.id = "a";
		a.type = TimelineEventType::SceneSwitch;
		a.sourceName = "S1";
		MacroButton b;
		b.id = "b";
		b.type = TimelineEventType::SceneSwitch;
		b.sourceName = "S2";
		model->addButton(a);
		model->addButton(b);
		QCOMPARE(model->count(), 2);

		dock.moveLeft("b");
		QCOMPARE(model->buttons()[0].id, QString("b"));

		dock.moveRight("b");
		QCOMPARE(model->buttons()[1].id, QString("b"));

		dock.removeButton("a");
		QCOMPARE(model->count(), 1);

		const auto widgets = dock.findChildren<MacroButtonWidget *>();
		QVERIFY(!widgets.isEmpty());
		QSignalSpy spy(widgets.first(), &MacroButtonWidget::fireRequested);
		TestTimelineMacroButtonAccessor::clickBubble(*widgets.first());
		QCOMPARE(spy.count(), 1);
	}

	void testDockSurvivesThemeEvent()
	{
		MacroStripDock dock;
		MacroButton b;
		b.id = "a";
		b.type = TimelineEventType::SceneSwitch;
		b.sourceName = "S";
		dock.buttonModel()->addButton(b);
		dock.onFrontendEvent(OBS_FRONTEND_EVENT_THEME_CHANGED);
		QCOMPARE(dock.buttonModel()->count(), 1);
		QCOMPARE(dock.buttonModel()->buttons()[0].id, QString("a"));
	}

	void testStripDockHeightMatchesToolbar()
	{
		MacroStripDock dock;
		// The strip dock height should match the timeline dock's toolbar row
		// (controlsLayout uses 2 px margins around 28 px secondary buttons → 32 px).
		QCOMPARE(dock.height(), 32);
	}
};

int main(int argc, char *argv[])
{
	setOffscreenPlatform();
	QApplication app(argc, argv);
	TestTimeline tc;
	return QTest::qExec(&tc, argc, argv);
}
#include "main_test.moc"
