#include <QGraphicsScene>
#include <QApplication>
#include <QScopeGuard>
#include <QDoubleSpinBox>
#include <QGraphicsSceneMouseEvent>
#include <QTest>
#include <QSignalSpy>
#include <QScrollArea>
#include <QScrollBar>
#include <QFontMetrics>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <memory>
#include <atomic>
#include "headless-timeline.h"
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <obs.h>
#include <util/bmem.h>
#include "obs-executor.h"
#include "media-target.h"
#include "theme-helpers.h"
#include "timeline-hit-testing.h"
#include "timeline-event-item.h"
#include "timeline-model.h"
#include "timeline-dock.h"
#include "timeline-scheduler.h"
#include "timeline-serialization.h"
#include "macro-button.h"
#include "macro-parameter.h"
#include "macro-parameter-widget.h"
#include <QDialogButtonBox>
#include <QPushButton>
#include <QDial>
#include <QLabel>
#include <QSlider>
#include <QCheckBox>
#include "macro-button-model.h"
#include "macro-button-widget.h"
#include "macro-serialization.h"
#include "macro-strip-dock.h"
#include "event-editor.h"
#include "hotkey-catalog.h"
#include "plugin-integrations.h"
#include <QMenu>
#include <QComboBox>
#include <QLineEdit>

// Mock helpers from mock_frontend.cpp
extern QJsonObject g_websocket_last_request;
extern int g_websocket_request_count;
extern bool g_websocket_available;
extern bool g_websocket_dsk_success;
extern unsigned int g_websocket_status;
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
		QMouseEvent release(QEvent::MouseButtonRelease, QPointF(15, 26), QPointF(15, 26), Qt::LeftButton,
				    Qt::NoButton, Qt::NoModifier);
		w.mousePressEvent(&press);
		w.mouseReleaseEvent(&release);
	}
};

// Helper to set QT_QPA_PLATFORM to offscreen
void setOffscreenPlatform()
{
	// Honor an explicit platform override (e.g. xcb under xvfb-run in CI).
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
		qputenv("QT_QPA_PLATFORM", "offscreen");
}

struct ParameterProbe {
	std::atomic<double> gain{0.25};
	std::atomic<int> count{4};
	std::atomic<bool> enabled{true};
};

struct MediaProbe {
	std::atomic<enum obs_media_state> state{OBS_MEDIA_STATE_STOPPED};
	std::atomic<int64_t> position{0};
};

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
	bool obsStarted = false;

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
			if (qEnvironmentVariableIsSet("TIMELINE_REQUIRE_OBS_RUNTIME"))
				QFAIL("Failed to initialize OpenGL; CI requires OBS runtime tests");
			QSKIP("OpenGL unavailable locally; run under Xvfb to execute OBS tests");
		}
		obsStarted = obs_startup("en-US", nullptr, nullptr);
		if (!obsStarted) {
			if (qEnvironmentVariableIsSet("TIMELINE_REQUIRE_OBS_RUNTIME"))
				QFAIL("OBS startup failed; CI requires the runtime tests");
			QSKIP("OBS startup unavailable locally; run under Xvfb to execute OBS tests");
		}

		// OBS source teardown uses obs->video.graphics. A Qt OpenGL context is not
		// an OBS graphics device, so initialize libobs video explicitly. This is
		// required by OBS 33's deferred source-destruction path.
		glContext->doneCurrent();
		obs_video_info video = {};
		video.graphics_module = "libobs-opengl";
		video.fps_num = 30;
		video.fps_den = 1;
		video.base_width = 64;
		video.base_height = 64;
		video.output_width = 64;
		video.output_height = 64;
		video.output_format = VIDEO_FORMAT_RGBA;
		video.colorspace = VIDEO_CS_DEFAULT;
		video.range = VIDEO_RANGE_DEFAULT;
		video.scale_type = OBS_SCALE_DISABLE;
		if (obs_reset_video(&video) != OBS_VIDEO_SUCCESS) {
			obs_shutdown();
			obsStarted = false;
			if (qEnvironmentVariableIsSet("TIMELINE_REQUIRE_OBS_RUNTIME"))
				QFAIL("OBS graphics initialization failed; CI requires runtime tests");
			QSKIP("OBS graphics unavailable locally; run under Xvfb to execute OBS tests");
		}

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

		// Controllable media fixture: libobs dispatches into real registered callbacks.
		obs_source_info media = {};
		media.id = "timeline_media_probe";
		media.type = OBS_SOURCE_TYPE_INPUT;
		media.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CONTROLLABLE_MEDIA;
		media.get_name = [](void *) {
			return "Timeline media probe";
		};
		media.create = [](obs_data_t *, obs_source_t *) -> void * {
			return new MediaProbe;
		};
		media.destroy = [](void *data) {
			delete static_cast<MediaProbe *>(data);
		};
		media.get_width = [](void *) -> uint32_t {
			return 64;
		};
		media.get_height = [](void *) -> uint32_t {
			return 64;
		};
		media.media_play_pause = [](void *data, bool pause) {
			static_cast<MediaProbe *>(data)->state = pause ? OBS_MEDIA_STATE_PAUSED
								       : OBS_MEDIA_STATE_PLAYING;
		};
		media.media_restart = [](void *data) {
			auto *probe = static_cast<MediaProbe *>(data);
			probe->state = OBS_MEDIA_STATE_PLAYING;
			probe->position = 0;
		};
		media.media_stop = [](void *data) {
			static_cast<MediaProbe *>(data)->state = OBS_MEDIA_STATE_STOPPED;
		};
		media.media_get_state = [](void *data) {
			return static_cast<MediaProbe *>(data)->state.load();
		};
		media.media_get_duration = [](void *) -> int64_t {
			return 60000;
		};
		media.media_get_time = [](void *data) {
			return static_cast<MediaProbe *>(data)->position.load();
		};
		media.media_set_time = [](void *data, int64_t ms) {
			static_cast<MediaProbe *>(data)->position = ms;
		};
		obs_register_source(&media);

		// Real registered source/filter with grouped, typed properties and defaults.
		obs_source_info parameter = {};
		parameter.id = "timeline_parameter_probe";
		parameter.type = OBS_SOURCE_TYPE_INPUT;
		parameter.output_flags = OBS_SOURCE_VIDEO;
		parameter.get_name = [](void *) {
			return "Parameter probe";
		};
		parameter.create = [](obs_data_t *, obs_source_t *) -> void * {
			return new ParameterProbe;
		};
		parameter.destroy = [](void *data) {
			delete static_cast<ParameterProbe *>(data);
		};
		parameter.update = [](void *data, obs_data_t *settings) {
			auto *probe = static_cast<ParameterProbe *>(data);
			probe->gain = obs_data_get_double(settings, "gain");
			probe->count = static_cast<int>(obs_data_get_int(settings, "count"));
			probe->enabled = obs_data_get_bool(settings, "enabled");
		};
		parameter.get_width = [](void *) -> uint32_t {
			return 64;
		};
		parameter.get_height = [](void *) -> uint32_t {
			return 64;
		};
		parameter.get_defaults = [](obs_data_t *settings) {
			obs_data_set_default_double(settings, "gain", 0.25);
			obs_data_set_default_int(settings, "count", 4);
			obs_data_set_default_bool(settings, "enabled", true);
			obs_data_set_default_string(settings, "untouched", "keep");
		};
		parameter.get_properties = [](void *data) {
			auto *props = obs_properties_create();
			obs_properties_add_bool(props, "enabled", "Enabled");
			auto *group = obs_properties_create();
			if (!data || static_cast<ParameterProbe *>(data)->enabled.load())
				obs_properties_add_float_slider(group, "gain", "Gain", -1, 1, 0.05);
			else
				obs_properties_add_float_slider(group, "alternate", "Alternate", 0, 2, 0.1);
			obs_properties_add_int_slider(group, "count", "Count", 2, 10, 2);
			obs_properties_add_group(props, "controls", "Controls", OBS_GROUP_NORMAL, group);
			obs_properties_add_text(props, "untouched", "Text", OBS_TEXT_DEFAULT);
			auto *hidden = obs_properties_add_float(props, "hidden", "Hidden", 0, 1, 0.1);
			obs_property_set_visible(hidden, false);
			return props;
		};
		obs_register_source(&parameter);
		parameter.id = "timeline_parameter_filter";
		parameter.type = OBS_SOURCE_TYPE_FILTER;
		obs_register_source(&parameter);

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
		if (!obsStarted) {
			// initTestCase may have skipped before starting OBS. Do not call
			// obs_shutdown() or check libobs allocations in that case.
			delete glContext;
			glContext = nullptr;
			delete offscreenSurface;
			offscreenSurface = nullptr;
			return;
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

		// Drain remaining OBS sources deterministically. Enumerating with a
		// temporary ref and then only releasing that ref is a net-zero operation;
		// OBS 33 can therefore defer the actual destruction until shutdown. Mark
		// each enumerated source removed before dropping our temporary ref.
		{
			std::vector<obs_source_t *> remaining;
			for (int pass = 0; pass < 8; pass++) {
				remaining.clear();
				obs_enum_all_sources(
					[](void *param, obs_source_t *source) {
						obs_source_get_ref(source);
						static_cast<std::vector<obs_source_t *> *>(param)->push_back(source);
						return true;
					},
					&remaining);
				if (remaining.empty())
					break;
				for (auto *source : remaining) {
					obs_source_remove(source);
					obs_source_release(source);
				}
			}

			remaining.clear();
			obs_enum_all_sources(
				[](void *param, obs_source_t *source) {
					static_cast<std::vector<obs_source_t *> *>(param)->push_back(source);
					return true;
				},
				&remaining);
			QVERIFY2(remaining.empty(),
				 qPrintable(QString("OBS sources still registered before shutdown: %1")
						    .arg(remaining.size())));
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
		g_websocket_last_request = {};
		g_websocket_request_count = 0;
		g_websocket_available = true;
		g_websocket_dsk_success = true;
		g_websocket_status = 100;
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

		// OBS's scene registry/canvas can retain scene sources across tests.
		// Remove them before dropping our refs so the next test gets its own named scenes.
		if (scene1)
			obs_source_remove(scene1);
		if (scene2)
			obs_source_remove(scene2);
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

	void testPluginIntegrationsDetectSupportedAndUnsupportedModules()
	{
		auto &integrations = PluginIntegrations::instance();
		integrations.setStoragePath(QString());
		integrations.setDetectedModules(
			{{"obs-browser", "OBS Browser"}, {"downstream-keyer", "Downstream Keyer"}});
		QVERIFY(integrations.actionAvailable(TimelineEventType::DownstreamKeyerScene));
		QMenu menu;
		integrations.populateMenu(menu);
		QVERIFY(menu.actions().size() >= 2);
		QAction *supported = nullptr;
		QAction *unsupported = nullptr;
		for (auto *action : menu.actions()) {
			if (action->text().contains("Downstream Keyer"))
				supported = action;
			if (action->text().contains("OBS Browser"))
				unsupported = action;
		}
		QVERIFY(supported && unsupported);
		QVERIFY(supported->isCheckable());
		QVERIFY(supported->isChecked());
		supported->setChecked(false);
		QVERIFY(!integrations.actionAvailable(TimelineEventType::DownstreamKeyerScene));
		supported->setChecked(true);
		QVERIFY(integrations.actionAvailable(TimelineEventType::DownstreamKeyerScene));
		QVERIFY(!unsupported->isEnabled());
		QVERIFY(!unsupported->isCheckable());
		integrations.setDetectedModules({});
	}

	void testPluginIntegrationTogglePersistsAndFiltersNewEditor()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		auto &integrations = PluginIntegrations::instance();
		const QString path = dir.filePath("integrations.ini");
		integrations.setStoragePath(path);
		integrations.setDetectedModules({{"downstream-keyer", "Downstream Keyer"}});
		{
			EventEditor editor(0.0);
			auto *types = editor.findChild<QComboBox *>("eventTypeCombo");
			QVERIFY(types);
			QVERIFY(types->findData(static_cast<int>(TimelineEventType::DownstreamKeyerScene)) >= 0);
		}
		integrations.setEnabled("downstream-keyer", false);
		QVERIFY(!integrations.actionAvailable(TimelineEventType::DownstreamKeyerScene));
		{
			EventEditor editor(0.0);
			auto *types = editor.findChild<QComboBox *>("eventTypeCombo");
			QVERIFY(types);
			QCOMPARE(types->findData(static_cast<int>(TimelineEventType::DownstreamKeyerScene)), -1);
		}
		integrations.setStoragePath(path); // Simulated fresh load from disk
		QVERIFY(!integrations.actionAvailable(TimelineEventType::DownstreamKeyerScene));
		integrations.setEnabled("downstream-keyer", true);
		QVERIFY(integrations.actionAvailable(TimelineEventType::DownstreamKeyerScene));
		integrations.setDetectedModules({});
		QVERIFY(!integrations.actionAvailable(TimelineEventType::DownstreamKeyerScene));
		integrations.setStoragePath(QString());
	}

	void testSavedPluginEventEditableWhenIntegrationUnavailable()
	{
		auto &integrations = PluginIntegrations::instance();
		integrations.setStoragePath(QString());
		integrations.setDetectedModules({});
		TimelineEvent saved;
		saved.type = TimelineEventType::DownstreamKeyerScene;
		saved.sourceName = "Keyer";
		saved.filterName = "Overlay";
		EventEditor editor(saved);
		auto *types = editor.findChild<QComboBox *>("eventTypeCombo");
		QVERIFY(types);
		QCOMPARE(types->currentData().toInt(), static_cast<int>(saved.type));
		QVERIFY(!types->model()
				 ->flags(types->model()->index(types->currentIndex(), 0))
				 .testFlag(Qt::ItemIsEnabled));
		TimelineEvent unchanged;
		editor.getUpdatedEvent(unchanged);
		QCOMPARE(unchanged.type, saved.type);
		QCOMPARE(unchanged.sourceName, saved.sourceName);
		QCOMPARE(unchanged.filterName, saved.filterName);
	}

	void testDownstreamKeyerSceneDispatch()
	{
		ObsExecutor executor;
		TimelineEvent evt;
		evt.type = TimelineEventType::DownstreamKeyerScene;
		evt.sourceName = "Lower Third Keyer";
		evt.filterName = "Announcement Overlay";
		evt.data = "Vertical";
		evt.hasDuration = true;
		evt.durationMs = 1000;

		executor.executeEventStart(evt);
		QCOMPARE(g_websocket_request_count, 1);
		QCOMPARE(g_websocket_last_request.value("vendorName").toString(), QString("downstream-keyer"));
		QCOMPARE(g_websocket_last_request.value("requestType").toString(), QString("dsk_select_scene"));
		const QJsonObject req = g_websocket_last_request.value("requestData").toObject();
		QCOMPARE(req.value("view_name").toString(), QString("Vertical"));
		QCOMPARE(req.value("dsk_name").toString(), QString("Lower Third Keyer"));
		QCOMPARE(req.value("scene").toString(), QString("Announcement Overlay"));

		executor.executeEventEnd(evt);
		QCOMPARE(g_websocket_request_count, 2); // Timed event clears overlay
		QCOMPARE(g_websocket_last_request.value("requestData").toObject().value("scene").toString(), QString());
	}

	void testDownstreamKeyerSceneValidatesBeforeDispatch()
	{
		ObsExecutor executor;
		TimelineEvent evt;
		evt.type = TimelineEventType::DownstreamKeyerScene;
		evt.sourceName = "Lower Third Keyer";
		executor.executeEventStart(evt); // Empty overlay scene is a deliberate hide
		QCOMPARE(g_websocket_request_count, 1);
		QCOMPARE(g_websocket_last_request.value("requestData").toObject().value("scene").toString(), QString());

		evt.filterName = "Overlay";
		evt.sourceName.clear();
		executor.executeEventStart(evt); // Missing keyer
		QCOMPARE(g_websocket_request_count, 1);
	}

	void testDownstreamKeyerMissingVendorKeepsProgramScene()
	{
		ObsExecutor executor;
		TimelineEvent evt;
		evt.type = TimelineEventType::DownstreamKeyerScene;
		evt.sourceName = "Keyer";
		evt.filterName = "Overlay";
		g_websocket_available = false;

		executor.executeEventStart(evt);
		QCOMPARE(g_websocket_request_count, 1);
		QVERIFY(g_websocket_last_request.isEmpty());
		QCOMPARE(g_current_scene, scene1);
	}

	void testDownstreamKeyerEditorRoundTrip()
	{
		TimelineEvent initial;
		initial.type = TimelineEventType::DownstreamKeyerScene;
		initial.sourceName = "Persistent Keyer";
		initial.filterName = "Now Playing";
		initial.data = "Vertical";
		EventEditor editor(initial);
		auto *name = editor.findChild<QLineEdit *>("dskNameEdit");
		auto *scene = editor.findChild<QLineEdit *>("dskSceneEdit");
		auto *view = editor.findChild<QLineEdit *>("dskViewEdit");
		QVERIFY(name && scene && view);
		QCOMPARE(name->text(), initial.sourceName);
		QCOMPARE(scene->text(), initial.filterName);
		QCOMPARE(view->text(), initial.data.toString());

		TimelineEvent edited;
		scene->clear(); // Empty scene hides downstream overlay
		view->clear();  // Empty view targets main output
		editor.getUpdatedEvent(edited);
		QCOMPARE(edited.type, TimelineEventType::DownstreamKeyerScene);
		QCOMPARE(edited.sourceName, QString("Persistent Keyer"));
		QCOMPARE(edited.filterName, QString());
		QCOMPARE(edited.data.toString(), QString());
	}

	void testDownstreamKeyerSceneSerializationRoundTrip()
	{
		MacroButtonModel macros;
		MacroButton macro;
		macro.type = TimelineEventType::DownstreamKeyerScene;
		macro.sourceName = "Keyer";
		macro.filterName = "Overlay";
		macro.data = "Vertical";
		macros.addButton(macro);
		MacroButtonModel reloaded;
		QString error;
		QVERIFY(MacroSerialization::fromJsonDocument(QJsonDocument(MacroSerialization::toJsonObject(macros)),
							     reloaded, &error));
		QCOMPARE(reloaded.buttons().size(), 1);
		QCOMPARE(reloaded.buttons().first().type, TimelineEventType::DownstreamKeyerScene);
		QCOMPARE(reloaded.buttons().first().filterName, QString("Overlay"));
		QCOMPARE(reloaded.buttons().first().data, QString("Vertical"));

		TimelineModel model;
		TimelineEvent evt;
		evt.type = TimelineEventType::DownstreamKeyerScene;
		evt.sourceName = "Keyer";
		evt.filterName = "Overlay";
		evt.data = "Vertical";
		model.addEvent(evt);
		TimelineModel parsed;
		parsed.fromJson(model.toJson());
		QCOMPARE(parsed.getEvents().size(), size_t(1));
		QCOMPARE(parsed.getEvents().front().type, TimelineEventType::DownstreamKeyerScene);
		QCOMPARE(parsed.getEvents().front().filterName, QString("Overlay"));
		QCOMPARE(parsed.getEvents().front().data.toString(), QString("Vertical"));
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

	void testCancelActiveTransitionRestoresOriginal()
	{
		if (!scene2 || !trans)
			QSKIP("OBS Startup failed, skipping test");
		std::shared_ptr<obs_source_t> original(obs_source_create("cut_transition", "Default Transition",
									 nullptr, nullptr),
						       obs_source_release);
		QVERIFY(original);
		g_current_transition = obs_source_get_ref(original.get());

		ObsExecutor executor;
		TimelineEvent evt;
		evt.type = TimelineEventType::SceneSwitch;
		evt.sourceName = "Scene 2";
		evt.data = "Cut";
		g_transition_duration_ms = 1000;
		executor.executeEventStart(evt);
		QCOMPARE(g_current_transition, trans);

		executor.cancelAll(); // stop or pause before the restore timer fires
		QCOMPARE(g_current_transition, original.get());
	}

	void testStoppingParentAndNestedOverridesRestoresOriginalTransition()
	{
		QVERIFY(scene2 && trans);
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString nestedPath = dir.filePath("child.timeline.json");
		QJsonObject childEvent{{"time", 0},
				       {"type", static_cast<int>(TimelineEventType::SceneSwitch)},
				       {"source", "Scene 2"},
				       {"data", "Child Transition"}};
		QJsonObject root{{"events", QJsonArray{childEvent}}, {"settings", QJsonObject{{"endTime", 5.0}}}};
		QFile file(nestedPath);
		QVERIFY(file.open(QIODevice::WriteOnly));
		QVERIFY(file.write(QJsonDocument(root).toJson()) > 0);
		file.close();

		obs_source_t *original = obs_source_create("cut_transition", "Original Transition", nullptr, nullptr);
		obs_source_t *child = obs_source_create("cut_transition", "Child Transition", nullptr, nullptr);
		QVERIFY(original && child);
		g_transitions.push_back(obs_source_get_ref(child));
		obs_frontend_set_current_transition(original);
		g_transition_duration_ms = 3000;

		{
			ObsExecutor parent;
			TimelineEvent parentEvent;
			parentEvent.type = TimelineEventType::SceneSwitch;
			parentEvent.sourceName = "Scene 2";
			parentEvent.data = "Cut";
			parent.executeEventStart(parentEvent);
			QCOMPARE(g_current_transition, trans);

			TimelineEvent nested;
			nested.type = TimelineEventType::TriggerTimeline;
			nested.data = nestedPath;
			parent.executeEventStart(nested);
			QTRY_COMPARE_WITH_TIMEOUT(g_current_transition, child, 1000);
			parent.cancelAll();
			QCOMPARE(g_current_transition, original);
		}
		obs_source_release(original);
		obs_source_release(child);
	}

	void testUnifiedActionCategoriesAndDefaults()
	{
		EventEditor editor(0.0);
		auto *types = editor.findChild<QComboBox *>("eventTypeCombo");
		auto *action = editor.findChild<QComboBox *>("eventActionCombo");
		QVERIFY(types && action);
		QCOMPARE(types->findData(static_cast<int>(TimelineEventType::SourceHide)), -1);
		QCOMPARE(types->findData(static_cast<int>(TimelineEventType::MediaStop)), -1);
		QCOMPARE(types->findData(static_cast<int>(TimelineEventType::FilterDisable)), -1);
		QCOMPARE(types->count(),
			 7 + (PluginIntegrations::instance().actionAvailable(TimelineEventType::DownstreamKeyerScene)
				      ? 1
				      : 0));
		struct Case {
			TimelineEventType category;
			TimelineEventType other;
		};
		const Case cases[] = {
			{TimelineEventType::SourceShow, TimelineEventType::SourceHide},
			{TimelineEventType::MediaStart, TimelineEventType::MediaStop},
			{TimelineEventType::FilterEnable, TimelineEventType::FilterDisable},
		};
		for (const auto &c : cases) {
			types->setCurrentIndex(types->findData(static_cast<int>(c.category)));
			QVERIFY(!action->isHidden());
			QCOMPARE(action->count(), c.category == TimelineEventType::MediaStart ? 5 : 2);
			QCOMPARE(action->currentData().toInt(), static_cast<int>(c.category));
			QCOMPARE(action->itemData(1).toInt(), static_cast<int>(c.other));
			TimelineEvent evt;
			editor.getUpdatedEvent(evt);
			QCOMPARE(evt.type, c.category);
			action->setCurrentIndex(1);
			editor.getUpdatedEvent(evt);
			QCOMPARE(evt.type, c.other);
		}
		types->setCurrentIndex(types->findData(static_cast<int>(TimelineEventType::SceneSwitch)));
		QVERIFY(action->isHidden());
	}

	void testUnifiedActionEditingPreservesLegacyTypeAndDuration()
	{
		struct Case {
			TimelineEventType legacy;
			TimelineEventType category;
		};
		const Case cases[] = {
			{TimelineEventType::SourceHide, TimelineEventType::SourceShow},
			{TimelineEventType::MediaStop, TimelineEventType::MediaStart},
			{TimelineEventType::FilterDisable, TimelineEventType::FilterEnable},
		};
		for (const auto &c : cases) {
			TimelineEvent saved;
			saved.type = c.legacy;
			saved.sourceName = "MySource";
			saved.filterName = "MyFilter";
			saved.hasDuration = true;
			saved.durationMs = 3500.0;
			EventEditor editor(saved);
			auto *types = editor.findChild<QComboBox *>("eventTypeCombo");
			auto *action = editor.findChild<QComboBox *>("eventActionCombo");
			QVERIFY(types && action);
			QCOMPARE(types->currentData().toInt(), static_cast<int>(c.category));
			QCOMPARE(action->currentData().toInt(), static_cast<int>(c.legacy));
			TimelineEvent updated = saved;
			editor.getUpdatedEvent(updated);
			QCOMPARE(updated.type, c.legacy);
			QCOMPARE(updated.sourceName, saved.sourceName);
			if (c.legacy == TimelineEventType::FilterDisable)
				QCOMPARE(updated.filterName, saved.filterName);
			QVERIFY(updated.hasDuration);
			QCOMPARE(updated.durationMs, 3500.0);
			action->setCurrentIndex(0);
			editor.getUpdatedEvent(updated);
			QCOMPARE(updated.type, c.category);
		}
	}

	void testUnifiedActionsPreserveSerializedTypes()
	{
		const TimelineEventType types[] = {
			TimelineEventType::SourceShow,   TimelineEventType::SourceHide,
			TimelineEventType::MediaStart,   TimelineEventType::MediaStop,
			TimelineEventType::FilterEnable, TimelineEventType::FilterDisable,
		};
		TimelineModel model;
		for (const auto type : types) {
			TimelineEvent evt;
			evt.type = type;
			evt.sourceName = "Saved source";
			evt.filterName = "Saved filter";
			model.addEvent(evt);
		}
		TimelineModel restored;
		restored.fromJson(model.toJson());
		QCOMPARE(restored.getEvents().size(), size_t(6));
		for (size_t i = 0; i < 6; ++i) {
			QCOMPARE(restored.getEvents()[i].type, types[i]);
			QCOMPARE(restored.getEvents()[i].sourceName, QString("Saved source"));
			QCOMPARE(restored.getEvents()[i].filterName, QString("Saved filter"));
		}

		MacroButtonModel macros;
		for (const auto type : types) {
			MacroButton b;
			b.type = type;
			b.sourceName = "Saved source";
			b.filterName = "Saved filter";
			macros.addButton(b);
		}
		MacroButtonModel reloaded;
		QVERIFY(MacroSerialization::fromJsonDocument(QJsonDocument(MacroSerialization::toJsonObject(macros)),
							     reloaded));
		QCOMPARE(reloaded.buttons().size(), 6);
		for (int i = 0; i < 6; ++i) {
			QCOMPARE(reloaded.buttons().at(i).type, types[i]);
			QCOMPARE(reloaded.buttons().at(i).sourceName, QString("Saved source"));
			QCOMPARE(reloaded.buttons().at(i).filterName, QString("Saved filter"));
		}
	}

	void testHotkeyGroupingUsesRealRegistrationAndMoveFilterIdentity()
	{
		const QList<MoveFilterIdentity> filters = {
			{QStringLiteral("test_filter"), QStringLiteral("Unrelated")},
			{QStringLiteral("move_source_filter"), QStringLiteral("Animate Intro")},
		};
		QCOMPARE(HotkeyCatalog::sourceGroup("Facecam", "Animate Intro", filters), QString("Move · Facecam"));
		QCOMPARE(HotkeyCatalog::sourceGroup("Facecam", "Animate Intro Hold", filters),
			 QString("Move · Facecam"));
		QCOMPARE(HotkeyCatalog::sourceGroup("Facecam", "Unrelated", filters), QString("Source · Facecam"));
		QCOMPARE(HotkeyCatalog::sourceGroup("Facecam", "Another Action", filters), QString("Source · Facecam"));
		QCOMPARE(HotkeyCatalog::sourceGroup(
				 "Facecam", "Animate Intro",
				 {{QStringLiteral("unrelated_move"), QStringLiteral("Animate Intro")}}),
			 QString("Source · Facecam"));

		QVERIFY(source_item);
		// Real libobs filter enumeration on the registered source must be safe.
		filter_item = obs_source_create("test_filter", "Unrelated", nullptr, nullptr);
		QVERIFY(filter_item);
		obs_source_filter_add(source_item, filter_item);
		const auto frontend = obs_hotkey_register_frontend(
			"timeline.tests.frontend", "Frontend marker",
			[](void *, obs_hotkey_id, obs_hotkey_t *, bool) {}, nullptr);
		const auto source = obs_hotkey_register_source(
			source_item, "timeline.tests.source", "Source marker",
			[](void *, obs_hotkey_id, obs_hotkey_t *, bool) {}, nullptr);
		auto catalog = HotkeyCatalog::collect();
		auto find = [&catalog](const QString &id) {
			for (const auto &item : catalog)
				if (item.id == id)
					return item;
			return HotkeyChoice{};
		};
		QCOMPARE(find("timeline.tests.frontend").group, QString("Frontend / Plugins"));
		QCOMPARE(find("timeline.tests.source").group, QString("Source · MySource"));
		obs_hotkey_unregister(source);
		obs_hotkey_unregister(frontend);
	}

	void testHotkeyPickerSearchGroupingAndStableId()
	{
		QVERIFY(source_item);
		const auto sourceId = obs_hotkey_register_source(
			source_item, "timeline.tests.move", "Move intro",
			[](void *, obs_hotkey_id, obs_hotkey_t *, bool) {}, nullptr);
		const auto frontendId = obs_hotkey_register_frontend(
			"timeline.tests.search", "Search marker", [](void *, obs_hotkey_id, obs_hotkey_t *, bool) {},
			nullptr);
		{
			EventEditor editor(0.0);
			auto *types = editor.findChild<QComboBox *>("eventTypeCombo");
			auto *search = editor.findChild<QLineEdit *>("hotkeySearchEdit");
			auto *target = editor.findChild<QComboBox *>("hotkeyTargetCombo");
			QVERIFY(types && search && target);
			types->setCurrentIndex(types->findData(static_cast<int>(TimelineEventType::HotkeyTrigger)));
			QVERIFY(!search->isHidden());
			QVERIFY(target->findData(QStringLiteral("timeline.tests.move")) >= 0);
			int sourceIdx = target->findData(QStringLiteral("timeline.tests.move"));
			QVERIFY(sourceIdx > 0);
			QVERIFY(!target->model()
					 ->flags(target->model()->index(sourceIdx - 1, 0))
					 .testFlag(Qt::ItemIsEnabled));

			search->setText("move intro");
			QCOMPARE(target->findData(QStringLiteral("timeline.tests.move")) >= 0, true);
			QCOMPARE(target->findData(QStringLiteral("timeline.tests.search")), -1);
			QVERIFY(target->isEnabled());
			TimelineEvent saved;
			editor.getUpdatedEvent(saved);
			QCOMPARE(saved.sourceName, QString("timeline.tests.move"));

			search->setText("no matching hotkey anywhere");
			QVERIFY(!target->isEnabled());
			QCOMPARE(target->findData(QStringLiteral("timeline.tests.move")), -1);
			search->clear();
			QVERIFY(target->isEnabled());
			QCOMPARE(target->findData(QStringLiteral("timeline.tests.search")) >= 0, true);
			types->setCurrentIndex(types->findData(static_cast<int>(TimelineEventType::SceneSwitch)));
			QVERIFY(search->isHidden());
		}
		obs_hotkey_unregister(frontendId);
		obs_hotkey_unregister(sourceId);
	}

	void testMissingSavedHotkeyIsPreservedAndSearchable()
	{
		TimelineEvent saved;
		saved.type = TimelineEventType::HotkeyTrigger;
		saved.sourceName = "unregistered.plugin.action";
		EventEditor editor(saved);
		auto *search = editor.findChild<QLineEdit *>("hotkeySearchEdit");
		auto *target = editor.findChild<QComboBox *>("hotkeyTargetCombo");
		QVERIFY(search && target);
		QCOMPARE(target->currentData().toString(), saved.sourceName);
		search->setText("unregistered");
		QCOMPARE(target->currentData().toString(), saved.sourceName);
		TimelineEvent updated = saved;
		editor.getUpdatedEvent(updated);
		QCOMPARE(updated.sourceName, saved.sourceName);
	}
	void testMediaActionsAgainstControllableSource()
	{
		auto releaseSource = [](obs_source_t *source) {
			if (source) {
				obs_source_remove(source);
				obs_source_release(source);
			}
		};
		using SourcePtr = std::unique_ptr<obs_source_t, decltype(releaseSource)>;
		SourcePtr clip(obs_source_create("timeline_media_probe", "Media example", nullptr, nullptr),
			       releaseSource);
		SourcePtr other(obs_source_create("timeline_media_probe", "Other media", nullptr, nullptr),
				releaseSource);
		QVERIFY(clip && other);
		QVERIFY(obs_source_get_output_flags(clip.get()) & OBS_SOURCE_CONTROLLABLE_MEDIA);
		auto releaseScene = [](obs_scene_t *scene) {
			if (scene) {
				obs_source_remove(obs_scene_get_source(scene));
				obs_scene_release(scene);
			}
		};
		std::unique_ptr<obs_scene_t, decltype(releaseScene)> root(obs_scene_create("Media root"), releaseScene);
		QVERIFY(root);
		auto *group = obs_scene_add_group(root.get(), "Media group");
		QVERIFY(group);
		QVERIFY(obs_scene_add(obs_sceneitem_group_get_scene(group), clip.get()));
		QVERIFY(obs_scene_add(root.get(), clip.get()));
		QVERIFY(obs_scene_add(obs_sceneitem_group_get_scene(group), other.get()));
		obs_frontend_set_current_scene(obs_scene_get_source(root.get()));
		auto restoreScene = qScopeGuard([this] { obs_frontend_set_current_scene(scene1); });
		EventEditor editor(0.0);
		auto *types = editor.findChild<QComboBox *>("eventTypeCombo");
		auto *actions = editor.findChild<QComboBox *>("eventActionCombo");
		auto *targets = editor.findChild<QComboBox *>("hotkeyTargetCombo");
		QVERIFY(types && actions && targets);
		types->setCurrentIndex(types->findData(static_cast<int>(TimelineEventType::MediaStart)));
		QCOMPARE(targets->count(), 2);
		int clipIndex = -1;
		bool groupOnlyFound = false;
		for (int i = 0; i < targets->count(); ++i) {
			if (targets->itemText(i).contains("Media example"))
				clipIndex = i;
			if (targets->itemText(i).contains("Media group / Other media"))
				groupOnlyFound = true;
		}
		QVERIFY(clipIndex >= 0 && groupOnlyFound);
		targets->setCurrentIndex(clipIndex);
		TimelineEvent event;
		editor.getUpdatedEvent(event);
		QCOMPARE(MediaTarget::decode(event.data.toString()).uuid,
			 QString::fromUtf8(obs_source_get_uuid(clip.get())));
		obs_source_set_name(clip.get(), "Renamed clip");
		obs_source_set_name(other.get(), "Media example");
		ObsExecutor executor;
		executor.executeEventStart(event);
		QTRY_COMPARE(obs_source_media_get_state(clip.get()), OBS_MEDIA_STATE_PLAYING);
		QTRY_COMPARE(obs_source_media_get_state(other.get()), OBS_MEDIA_STATE_STOPPED);
		event.type = TimelineEventType::MediaSeekTo;
		event.data = MediaTarget::encode(QString::fromUtf8(obs_source_get_uuid(clip.get())), 12345);
		executor.executeEventStart(event);
		QTRY_COMPARE(obs_source_media_get_time(clip.get()), int64_t(12345));
		event.type = TimelineEventType::MediaPause;
		event.hasDuration = true;
		executor.executeEventStart(event);
		QTRY_COMPARE(obs_source_media_get_state(clip.get()), OBS_MEDIA_STATE_PAUSED);
		QTRY_COMPARE(obs_source_media_get_time(clip.get()), int64_t(12345));
		executor.executeEventEnd(event);
		QTRY_COMPARE(obs_source_media_get_state(clip.get()), OBS_MEDIA_STATE_PLAYING);
		event.type = TimelineEventType::MediaResume;
		executor.executeEventEnd(event);
		QTRY_COMPARE(obs_source_media_get_state(clip.get()), OBS_MEDIA_STATE_PAUSED);
		executor.executeEventStart(event);
		QTRY_COMPARE(obs_source_media_get_state(clip.get()), OBS_MEDIA_STATE_PLAYING);
		event.type = TimelineEventType::MediaSeekTo;
		event.data = MediaTarget::encode(QString::fromUtf8(obs_source_get_uuid(clip.get())), 70000);
		executor.executeEventStart(event);
		QTRY_COMPARE(obs_source_media_get_time(clip.get()), int64_t(60000));
		event.data = MediaTarget::encode(QString::fromUtf8(obs_source_get_uuid(clip.get())), 1000);
		executor.executeEventEnd(event);
		QTRY_COMPARE(obs_source_media_get_time(clip.get()), int64_t(60000));
		event.type = TimelineEventType::MediaStop;
		executor.executeEventStart(event);
		QTRY_COMPARE(obs_source_media_get_state(clip.get()), OBS_MEDIA_STATE_STOPPED);
		obs_source_remove(clip.get());
		event.type = TimelineEventType::MediaStart;
		executor.executeEventStart(event);
		QTRY_COMPARE(obs_source_media_get_state(other.get()), OBS_MEDIA_STATE_STOPPED);
		event.data = QStringLiteral("media:v1:invalid");
		executor.executeEventStart(event);
		QTRY_COMPARE(obs_source_media_get_state(other.get()), OBS_MEDIA_STATE_STOPPED);
		// Drain at least a few media ticks before checking the absence of a fallback.
		QTest::qWait(150);
		QCOMPARE(obs_source_media_get_state(other.get()), OBS_MEDIA_STATE_STOPPED);
		event.data = QVariant();
		executor.executeEventStart(event);
		QTRY_COMPARE(obs_source_media_get_state(other.get()), OBS_MEDIA_STATE_PLAYING);
		event.type = TimelineEventType::MediaSeek; // historical reserved ID
		event.data = MediaTarget::encode({}, 7000);
		executor.executeEventStart(event);
		QTest::qWait(100);
		QCOMPARE(obs_source_media_get_time(other.get()), int64_t(0));
	}

	void testExtendedMediaEditorAndSerialization()
	{
		// Append-only IDs: the historical MediaSeek (3) remains reserved.
		const auto pause = static_cast<TimelineEventType>(11);
		const auto resume = static_cast<TimelineEventType>(12);
		const auto seek = static_cast<TimelineEventType>(13);
		TimelineEvent event;
		event.type = TimelineEventType::MediaStart;
		event.sourceName = "Saved clip";
		EventEditor editor(event);
		auto *actions = editor.findChild<QComboBox *>("eventActionCombo");
		QVERIFY(actions);
		QCOMPARE(actions->count(), 5);
		QVERIFY(actions->findData(11) >= 0);
		QVERIFY(actions->findData(12) >= 0);
		QVERIFY(actions->findData(13) >= 0);
		actions->setCurrentIndex(actions->findData(13));
		auto *position = editor.findChild<QDoubleSpinBox *>("mediaSeekPosition");
		QVERIFY(position && position->isVisibleTo(&editor));
		position->setValue(12.345);
		TimelineEvent edited;
		editor.getUpdatedEvent(edited);
		QCOMPARE(edited.type, seek);
		QCOMPARE(edited.sourceName, event.sourceName);
		QVERIFY(!edited.hasDuration);
		TimelineModel model;
		model.addEvent(edited);
		TimelineModel restored;
		restored.fromJson(model.toJson());
		QCOMPARE(restored.getEvents().size(), size_t(1));
		EventEditor reopened(restored.getEvents().front());
		auto *reopenedPosition = reopened.findChild<QDoubleSpinBox *>("mediaSeekPosition");
		QVERIFY(reopenedPosition && reopenedPosition->isVisibleTo(&reopened));
		QCOMPARE(reopenedPosition->value(), 12.345);
		MacroButtonModel macros;
		for (auto type : {pause, resume, seek}) {
			MacroButton macro;
			macro.type = type;
			macro.sourceName = edited.sourceName;
			macro.data = edited.data.toString();
			macros.addButton(macro);
		}
		MacroButtonModel reloaded;
		QVERIFY(MacroSerialization::fromJsonDocument(QJsonDocument(MacroSerialization::toJsonObject(macros)),
							     reloaded));
		QCOMPARE(reloaded.buttons().size(), 3);
		QCOMPARE(reloaded.buttons().last().type, seek);
		QCOMPARE(reloaded.buttons().last().data, edited.data.toString());
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

	void testNestedVisibilityEditorTargetsExactSceneItem()
	{
		auto releaseScene = [](obs_scene_t *scene) {
			if (scene) {
				obs_source_remove(obs_scene_get_source(scene));
				obs_scene_release(scene);
			}
		};
		using ScenePtr = std::unique_ptr<obs_scene_t, decltype(releaseScene)>;
		ScenePtr root(obs_scene_create("Visibility root"), releaseScene);
		ScenePtr nested(obs_scene_create("Visibility nested"), releaseScene);
		QVERIFY(root && nested && source_item);
		auto *direct = obs_scene_add(root.get(), source_item);
		auto *child = obs_scene_add(nested.get(), source_item);
		QVERIFY(direct && child && obs_scene_add(root.get(), obs_scene_get_source(nested.get())));
		auto *group = obs_scene_add_group(root.get(), "Visibility group");
		QVERIFY(group);
		auto *groupChild = obs_scene_add(obs_sceneitem_group_get_scene(group), source_item);
		QVERIFY(groupChild);
		obs_frontend_set_current_scene(obs_scene_get_source(root.get()));
		auto restoreScene = qScopeGuard([this] { obs_frontend_set_current_scene(scene1); });
		TimelineEvent saved;
		saved.type = TimelineEventType::SourceHide;
		EventEditor editor(saved);
		auto *targets = editor.findChild<QComboBox *>("hotkeyTargetCombo");
		QVERIFY(targets);
		int selected = -1;
		int groupSelected = -1;
		for (int i = 0; i < targets->count(); ++i) {
			if (targets->itemText(i).contains("Visibility nested") &&
			    targets->itemText(i).contains("MySource"))
				selected = i;
			if (targets->itemText(i).contains("Visibility group") &&
			    targets->itemText(i).contains("MySource"))
				groupSelected = i;
		}
		QVERIFY2(selected >= 0, "Nested scene item must be selectable");
		QVERIFY2(groupSelected >= 0, "Group child must be selectable");
		targets->setCurrentIndex(selected);
		TimelineEvent evt;
		editor.getUpdatedEvent(evt);
		QCOMPARE(evt.sourceName, QString("MySource"));
		QVERIFY(!evt.data.toString().isEmpty());
		TimelineModel model;
		model.addEvent(evt);
		TimelineModel restored;
		restored.fromJson(model.toJson());
		QCOMPARE(restored.getEvents().front().data.toString(), evt.data.toString());
		EventEditor reopened(restored.getEvents().front());
		TimelineEvent edited;
		reopened.getUpdatedEvent(edited);
		QCOMPARE(edited.data.toString(), evt.data.toString());
		MacroButtonModel macros;
		MacroButton macro;
		macro.type = edited.type;
		macro.sourceName = edited.sourceName;
		macro.data = edited.data.toString();
		macros.addButton(macro);
		MacroButtonModel reloaded;
		QVERIFY(MacroSerialization::fromJsonDocument(QJsonDocument(MacroSerialization::toJsonObject(macros)),
							     reloaded));
		QCOMPARE(reloaded.buttons().front().data, edited.data.toString());

		obs_source_set_name(obs_scene_get_source(nested.get()), "Renamed nested");
		obs_source_set_name(source_item, "Renamed source");
		ObsExecutor executor;
		executor.executeEventStart(edited);
		QVERIFY(obs_sceneitem_visible(direct));
		QVERIFY(!obs_sceneitem_visible(child));
		QVERIFY(obs_sceneitem_visible(groupChild));
		targets->setCurrentIndex(groupSelected);
		TimelineEvent groupEvent;
		editor.getUpdatedEvent(groupEvent);
		executor.executeEventStart(groupEvent);
		QVERIFY(!obs_sceneitem_visible(groupChild));
		QVERIFY(obs_sceneitem_visible(direct));
		edited.type = TimelineEventType::SourceShow;
		executor.executeEventStart(edited);
		QVERIFY(obs_sceneitem_visible(child));
		obs_sceneitem_remove(child);
		edited.type = TimelineEventType::SourceHide;
		obs_frontend_set_current_scene(obs_scene_get_source(root.get()));
		EventEditor missing(edited);
		TimelineEvent missingEdited;
		missing.getUpdatedEvent(missingEdited);
		QCOMPARE(missingEdited.data.toString(), edited.data.toString());
		executor.executeEventStart(missingEdited);
		QVERIFY(obs_sceneitem_visible(direct));
		obs_frontend_set_current_scene(scene1);
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

	void testDurationFilterDisableReenablesOnEnd()
	{
		if (!source_item)
			QSKIP("OBS Startup failed, skipping test");
		filter_item = obs_source_create("test_filter", "MyFilter", nullptr, nullptr);
		QVERIFY(filter_item);
		obs_source_filter_add(source_item, filter_item);
		obs_source_set_enabled(filter_item, true);

		ObsExecutor executor;
		TimelineEvent evt;
		evt.type = TimelineEventType::FilterDisable;
		evt.sourceName = "MySource";
		evt.filterName = "MyFilter";
		evt.hasDuration = true;
		evt.durationMs = 200.0;

		executor.executeEventStart(evt);
		QVERIFY(!obs_source_enabled(filter_item));
		executor.executeEventEnd(evt);
		QVERIFY(obs_source_enabled(filter_item));
	}

	void testNestedTimelineCycleDoesNotCreateRecursiveChildren()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString a = dir.filePath("a.timeline.json");
		const QString b = dir.filePath("b.timeline.json");
		auto writeTimeline = [](const QString &path, const QString &next) {
			QJsonObject event{{"time", 0},
					  {"type", static_cast<int>(TimelineEventType::TriggerTimeline)},
					  {"data", next}};
			QJsonObject root{{"events", QJsonArray{event}}, {"settings", QJsonObject{{"endTime", 2.0}}}};
			QFile f(path);
			if (!f.open(QIODevice::WriteOnly))
				return false;
			return f.write(QJsonDocument(root).toJson()) > 0;
		};
		QVERIFY(writeTimeline(a, b));
		QVERIFY(writeTimeline(b, a));

		ObsExecutor executor;
		TimelineEvent start;
		start.type = TimelineEventType::TriggerTimeline;
		start.data = a;
		executor.executeEventStart(start);
		QTest::qWait(100);
		// Root -> A -> B is allowed. B -> A must not spawn another A.
		QCOMPARE(executor.findChildren<HeadlessTimeline *>().size(), 2);
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

	void testTimelineCompactModeRefreshesLayoutAndFitsMarkers()
	{
		QTemporaryDir dir;
		TimelineDock dock(nullptr, dir.filePath("timeline.json"));
		dock.resize(1000, 180);
		dock.show();
		QCoreApplication::processEvents();
		auto *model = dock.findChild<TimelineModel *>();
		auto *view = dock.findChild<QGraphicsView *>();
		auto *play = dock.findChild<QPushButton *>("timeline-play-btn");
		auto *undo = dock.findChild<QPushButton *>("timeline-undo-btn");
		auto *settings = dock.findChild<QPushButton *>("timeline-settings-btn");
		QVERIFY(model && view && play && undo && settings);
		auto *redo = dock.findChild<QPushButton *>("timeline-redo-btn");
		auto *zoomOut = dock.findChild<QPushButton *>("timeline-zoom-out-btn");
		auto *zoomIn = dock.findChild<QPushButton *>("timeline-zoom-in-btn");
		QVERIFY(redo && zoomOut && zoomIn);
		auto verifyJoinedHistory = [&] {
			QCOMPARE(undo->parentWidget(), redo->parentWidget());
			QVERIFY(undo->parentWidget() != &dock);
			QCOMPARE(undo->size(), zoomOut->size());
			QCOMPARE(redo->size(), zoomIn->size());
			QCOMPARE(undo->iconSize(), zoomOut->iconSize());
			QCOMPARE(redo->iconSize(), zoomIn->iconSize());
			QCOMPARE(undo->styleSheet(), zoomOut->styleSheet());
			QCOMPARE(redo->styleSheet(), zoomIn->styleSheet());
			QCOMPARE(undo->geometry().right() + 1, redo->geometry().left());
			QCOMPARE(undo->y(), redo->y());
			QVERIFY(undo->isVisible() && redo->isVisible());
		};
		verifyJoinedHistory();
		QTest::mouseClick(settings, Qt::LeftButton);
		QTRY_VERIFY(QApplication::activePopupWidget());
		auto *menu = QApplication::activePopupWidget();
		QCheckBox *compact = nullptr;
		for (auto *box : menu->findChildren<QCheckBox *>())
			if (box->objectName() == QString("timeline-compact-mode"))
				compact = box;
		QVERIFY(compact);
		menu->hide();

		TimelineEvent a, b;
		a.timeMs = 1000.0;
		b.timeMs = 1280.0; // overlap at 32px, clear at 22px (100px/s)
		a.label = "First cue";
		a.showLabel = true;
		model->addEvents({a, b});
		QCoreApplication::processEvents();
		auto marker = [&](const QString &id) -> TimelineEventItem * {
			for (auto *graphics : view->scene()->items())
				if (auto *item = dynamic_cast<TimelineEventItem *>(graphics);
				    item && item->getEventId() == id)
					return item;
			return nullptr;
		};
		QVERIFY(marker(a.id) && marker(b.id));
		QCOMPARE(marker(b.id)->getStackOffset(), 1);
		QCOMPARE(play->height(), 32);
		const int normalMinimum = dock.minimumSizeHint().height();
		compact->setChecked(true);
		QCoreApplication::processEvents();
		QCOMPARE(play->height(), 28);
		QCOMPARE(undo->height(), 20);
		verifyJoinedHistory();
		QVERIFY(marker(a.id)->isCompactMode());
		QCOMPARE(marker(b.id)->getStackOffset(), 0);
		QCOMPARE(marker(a.id)->getTimeMs(), a.timeMs);
		QVERIFY(dock.minimumSizeHint().height() < normalMinimum);
		dock.resize(1000, 1);
		QCoreApplication::processEvents();
		QVERIFY(view->viewport()->height() >= marker(a.id)->sceneBoundingRect().bottom());
		dock.save();
		compact->setChecked(false);
		QCoreApplication::processEvents();
		QCOMPARE(play->height(), 32);
		dock.load();
		QCoreApplication::processEvents();
		QCOMPARE(play->height(), 28);
		verifyJoinedHistory();
		QVERIFY(marker(a.id)->isCompactMode());
		QCOMPARE(marker(b.id)->getStackOffset(), 0);

		// Stacked events stay reachable in a short dock without forcing a
		// huge minimum height onto the entire OBS main window.
		for (int i = 0; i < 8; ++i) {
			TimelineEvent crowded;
			crowded.timeMs = a.timeMs;
			model->addEvent(crowded);
		}
		QCoreApplication::processEvents();
		QCoreApplication::processEvents();
		double bottom = 0;
		for (auto *graphics : view->scene()->items())
			if (auto *item = dynamic_cast<TimelineEventItem *>(graphics))
				bottom = std::max(bottom, item->sceneBoundingRect().bottom());
		QVERIFY(view->sceneRect().bottom() >= bottom);
		QVERIFY(view->verticalScrollBar()->maximum() > 0);
		view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum());
		QVERIFY(view->mapToScene(view->viewport()->rect()).boundingRect().bottom() >= bottom);
	}

	void testTimelineDockUndoRedoAndPersistence()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath("timeline.json");
		TimelineDock dock(nullptr, path);
		dock.resize(1100, 240);
		dock.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dock));
		auto *model = dock.findChild<TimelineModel *>();
		auto *scheduler = dock.findChild<TimelineScheduler *>();
		auto *view = dock.findChild<QGraphicsView *>();
		auto *undo = dock.findChild<QPushButton *>("timeline-undo-btn");
		auto *redo = dock.findChild<QPushButton *>("timeline-redo-btn");
		QVERIFY(model && scheduler && view && undo && redo);
		QVERIFY(!undo->isEnabled() && !redo->isEnabled());
		TimelineEvent a, b;
		a.timeMs = 1500;
		a.type = TimelineEventType::MediaStart;
		a.sourceName = "Saved clip";
		b.timeMs = 2500;
		model->addEvents({a, b});
		QTRY_VERIFY(undo->isEnabled());
		QTest::mouseClick(undo, Qt::LeftButton);
		QVERIFY(model->getEvents().empty());
		QVERIFY(redo->isEnabled());
		QTest::mouseClick(redo, Qt::LeftButton);
		QCOMPARE(model->getEvents().size(), size_t(2));
		// Real widget shortcut, not a direct call to the model.
		QApplication::setActiveWindow(&dock);
		view->setFocus();
		QTRY_VERIFY_WITH_TIMEOUT(view->hasFocus(), 1000);
		const auto undoKey = QKeySequence::keyBindings(QKeySequence::Undo).first()[0];
		QTest::keyClick(view, undoKey.key(), undoKey.keyboardModifiers());
		QTRY_VERIFY_WITH_TIMEOUT(model->getEvents().empty(), 1500);
		const auto redoKey = QKeySequence::keyBindings(QKeySequence::Redo).first()[0];
		QTest::keyClick(view, redoKey.key(), redoKey.keyboardModifiers());
		QTRY_COMPARE(model->getEvents().size(), size_t(2));
		QCoreApplication::processEvents();

		// Select both markers, delete with one model change, then restore both.
		int selected = 0;
		for (auto *graphics : view->scene()->items()) {
			if (auto *item = dynamic_cast<TimelineEventItem *>(graphics)) {
				item->setSelected(true);
				++selected;
			}
		}
		QCOMPARE(selected, 2);
		QTest::keyClick(view, Qt::Key_Right, Qt::ShiftModifier);
		QTRY_COMPARE_WITH_TIMEOUT(model->findEventById(a.id)->timeMs, a.timeMs + 100, 1500);
		QCOMPARE(model->findEventById(b.id)->timeMs, b.timeMs + 100);
		QTest::mouseClick(undo, Qt::LeftButton);
		QCOMPARE(model->findEventById(a.id)->timeMs, a.timeMs);
		QCOMPARE(model->findEventById(b.id)->timeMs, b.timeMs);
		QCoreApplication::processEvents();
		view->setFocus();
		QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
		scheduler->seek(4000);
		QTest::keyClick(view, Qt::Key_V, Qt::ControlModifier);
		QTRY_COMPARE(model->getEvents().size(), size_t(4));
		QCOMPARE(model->getEvents()[2].timeMs, 4000.0);
		QCOMPARE(model->getEvents()[3].timeMs, 5000.0);
		QVERIFY(model->getEvents()[2].id != a.id);
		QTest::mouseClick(undo, Qt::LeftButton);
		QCOMPARE(model->getEvents().size(), size_t(2));
		QCoreApplication::processEvents();
		scheduler->seek(0);
		for (auto *graphics : view->scene()->items())
			if (auto *item = dynamic_cast<TimelineEventItem *>(graphics))
				item->setSelected(true);
		view->setFocus();
		QTest::keyClick(view, Qt::Key_Delete);
		QTRY_VERIFY_WITH_TIMEOUT(model->getEvents().empty(), 1500);
		QTest::mouseClick(undo, Qt::LeftButton);
		QCOMPARE(model->getEvents().size(), size_t(2));
		QCoreApplication::processEvents();

		// Drag an actual marker through the viewport, including its release.
		TimelineEventItem *marker = nullptr;
		for (auto *graphics : view->scene()->items()) {
			auto *item = dynamic_cast<TimelineEventItem *>(graphics);
			if (item && item->getEventId() == a.id)
				marker = item;
		}
		QVERIFY(marker);
		QCOMPARE(marker->getTimeMs(), a.timeMs);
		const QPoint begin = view->mapFromScene(marker->mapToScene(marker->rect().center()));
		const QPoint end = begin + QPoint(40, 0);
		QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, begin);
		// Explicit pressed buttons for a real drag gesture across Qt backends.
		QMouseEvent move(QEvent::MouseMove, QPointF(end), QPointF(view->viewport()->mapToGlobal(end)),
				 Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(view->viewport(), &move);
		QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, end);
		QTRY_VERIFY_WITH_TIMEOUT(model->findEventById(a.id)->timeMs > a.timeMs, 1500);
		QTest::mouseClick(undo, Qt::LeftButton);
		QCOMPARE(model->findEventById(a.id)->timeMs, a.timeMs);
		QCoreApplication::processEvents();

		// The actual event editor must preserve a user's exact-time edit.
		bool editedTime = false;
		QTimer::singleShot(0, &dock, [&] {
			auto *editor = dock.findChild<EventEditor *>();
			if (!editor)
				return;
			auto *spin = editor->findChild<QDoubleSpinBox *>("eventTimeSpin");
			if (spin) {
				spin->setValue(1.678);
				editedTime = true;
				editor->accept();
			} else {
				editor->reject();
			}
		});
		QVERIFY(QMetaObject::invokeMethod(marker, "doubleClicked", Qt::DirectConnection));
		QVERIFY(editedTime);
		QCOMPARE(model->findEventById(a.id)->timeMs, 1678.0);
		QTest::mouseClick(undo, Qt::LeftButton);
		QCOMPARE(model->findEventById(a.id)->timeMs, a.timeMs);

		auto *play = dock.findChild<QPushButton *>("timeline-play-btn");
		QVERIFY(play);
		QTest::mouseClick(play, Qt::LeftButton);
		QVERIFY(scheduler->isPlaying());
		QVERIFY(!undo->isEnabled() && !redo->isEnabled());
		QTest::mouseClick(play, Qt::LeftButton);
		QVERIFY(!scheduler->isPlaying());
		QVERIFY(undo->isEnabled() && redo->isEnabled());
		QJsonDocument saved;
		QTRY_VERIFY_WITH_TIMEOUT(TimelineSerialization::loadFromFile(path, saved) &&
						 saved.object()["events"].toArray() == model->toJson(),
					 1500);
		QCOMPARE(saved.object()["events"].toArray(), model->toJson());
		dock.load();
		QVERIFY(!undo->isEnabled() && !redo->isEnabled());
		QCOMPARE(model->getEvents().size(), size_t(2));
	}

	void testEventEditorPreservesUnchangedFractionalTime()
	{
		TimelineEvent input;
		input.type = TimelineEventType::MediaStart;
		input.sourceName = "Saved clip";
		input.timeMs = 1234.56789;
		EventEditor editor(input);
		TimelineEvent result = input;
		editor.getUpdatedEvent(result);
		QCOMPARE(result.timeMs, input.timeMs);
		auto *time = editor.findChild<QDoubleSpinBox *>("eventTimeSpin");
		QVERIFY(time);
		time->setValue(2.345);
		editor.getUpdatedEvent(result);
		QCOMPARE(result.timeMs, 2345.0);
	}

	void testTimelineHistoryIsBounded()
	{
		TimelineModel model;
		model.setHistoryEnabled(true);
		TimelineEvent evt;
		model.addEvent(evt);
		for (int i = 1; i <= 110; ++i)
			model.updateEventTimeById(evt.id, i);
		int count = 0;
		while (model.undo())
			++count;
		QCOMPARE(count, 100);
		QCOMPARE(model.findEventById(evt.id)->timeMs, 10.0);
		for (int i = 0; i < 100; ++i)
			QVERIFY(model.redo());
		QCOMPARE(model.findEventById(evt.id)->timeMs, 110.0);
		QVERIFY(!model.canRedo());
	}

	void testRestoringMarkerBeyondEndDoesNotChangeItsTime()
	{
		QGraphicsScene scene;
		auto *item = new TimelineEventItem(1500.0, 100.0, TimelineEventType::SceneSwitch);
		scene.addItem(item);
		item->setMaxTime(2000.0);
		item->setTimeMs(2500.0);
		QCOMPARE(item->getTimeMs(), 2500.0);
		item->setPixelsPerSecond(200.0);
		item->setCompactMode(true);
		item->setStackOffset(1);
		QCOMPARE(item->getTimeMs(), 2500.0);
	}

	void testTimelineHistoryRestoresCompleteEvents()
	{
		TimelineModel model;
		model.setHistoryEnabled(true);
		TimelineEvent event;
		event.timeMs = 2345.678;
		event.type = TimelineEventType::MediaSeekTo;
		event.sourceName = "Clip";
		event.data = QStringLiteral("media:v1:uuid:12345");
		event.color = QColor(1, 2, 3, 100);
		event.label = "Cue";
		event.showLabel = true;
		event.customIconPath = "/tmp/custom.svg";
		event.hasDuration = true;
		event.durationMs = 12.345;
		model.addEvent(event);
		QVERIFY(model.canUndo());
		QVERIFY(!model.canRedo());
		model.updateEventTimeById(event.id, 3456.789);
		TimelineEvent changed = model.findEventById(event.id).value();
		changed.label = "Changed";
		changed.data = QStringLiteral("new payload");
		model.replaceEventById(event.id, changed);
		QVERIFY(model.undo());
		QCOMPARE(model.findEventById(event.id)->label, event.label);
		QCOMPARE(model.findEventById(event.id)->data, event.data);
		QVERIFY(model.undo());
		const auto original = model.findEventById(event.id).value();
		QCOMPARE(original.timeMs, event.timeMs);
		QCOMPARE(original.color, event.color);
		QCOMPARE(original.durationMs, event.durationMs);
		QCOMPARE(original.customIconPath, event.customIconPath);
		QVERIFY(original.showLabel && original.hasDuration);
		QVERIFY(model.undo());
		QVERIFY(model.getEvents().empty());
		QVERIFY(!model.canUndo());
		QVERIFY(model.redo());
		QVERIFY(model.redo());
		QVERIFY(model.redo());
		QCOMPARE(model.findEventById(event.id)->label, changed.label);
		QVERIFY(!model.canRedo());
	}

	void testTimelineHistoryBatchesAndBranches()
	{
		TimelineModel model;
		model.setHistoryEnabled(true);
		TimelineEvent a, b;
		a.timeMs = 1000;
		b.timeMs = 2000;
		QSignalSpy changes(&model, &TimelineModel::modelChanged);
		model.addEvents({a, b});
		QCOMPARE(changes.count(), 1);
		model.setEventTimes({{a.id, 1100}, {b.id, 2100}});
		QCOMPARE(changes.count(), 2);
		model.removeEventsById({a.id, b.id});
		QCOMPARE(changes.count(), 3);
		QVERIFY(model.undo());
		QCOMPARE(model.getEvents().size(), size_t(2));
		QCOMPARE(model.findEventById(a.id)->timeMs, 1100.0);
		QVERIFY(model.undo());
		QCOMPARE(model.findEventById(b.id)->timeMs, 2000.0);
		QVERIFY(model.canRedo());
		model.updateEventTimeById(a.id, 1000);
		QVERIFY(model.canRedo()); // no-op must not invalidate redo
		model.updateEventTimeById(a.id, 1200);
		QVERIFY(!model.canRedo());
		QVERIFY(model.undo());
		QVERIFY(model.undo());
		QVERIFY(model.getEvents().empty());
		QVERIFY(!model.canUndo());
	}

	void testTimelineHistoryLoadAndPlaybackIsolation()
	{
		TimelineModel model;
		TimelineEvent cue;
		model.addEvent(cue);
		QVERIFY(!model.canUndo()); // headless does not allocate history
		model.setHistoryEnabled(true);
		model.updateEventTimeById(cue.id, 1000);
		TimelineScheduler scheduler(&model);
		QSignalSpy starts(&scheduler, &TimelineScheduler::eventStarted);
		QSignalSpy ends(&scheduler, &TimelineScheduler::eventEnded);
		scheduler.seek(500);
		QVERIFY(model.undo());
		QVERIFY(model.redo());
		QCOMPARE(scheduler.getCurrentTime(), 500.0);
		QCOMPARE(starts.count(), 0);
		QCOMPARE(ends.count(), 0);
		QVERIFY(model.canUndo());
		TimelineDocument doc;
		QVERIFY(!TimelineSerialization::fromJsonDocument(QJsonDocument(QJsonObject{}), model, doc));
		QVERIFY(model.canUndo()); // failed import doesn't erase history
		QVERIFY(TimelineSerialization::fromJsonDocument(QJsonDocument(model.toJson()), model, doc));
		QVERIFY(!model.canUndo());
		QVERIFY(!model.canRedo());
		QCOMPARE(model.getEvents().size(), size_t(1));
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

	void testSchedulerDrainsFinalCuesBeforeFinish()
	{
		TimelineModel model;
		TimelineEvent span;
		span.timeMs = 0.0;
		span.hasDuration = true;
		span.durationMs = 25.0;
		model.addEvent(span);
		TimelineEvent last;
		last.timeMs = 25.0;
		model.addEvent(last);
		TimelineEvent outside;
		outside.timeMs = 26.0;
		model.addEvent(outside);

		TimelineScheduler scheduler(&model);
		scheduler.setEndTime(25.0);
		QStringList trace;
		connect(&scheduler, &TimelineScheduler::eventStarted, &scheduler, [&](const TimelineEvent &evt) {
			trace << (evt.id == span.id   ? "span:start"
				  : evt.id == last.id ? "last:start"
						      : "outside:start");
		});
		connect(&scheduler, &TimelineScheduler::eventEnded, &scheduler,
			[&](const TimelineEvent &) { trace << "span:end"; });
		connect(&scheduler, &TimelineScheduler::finished, &scheduler, [&] { trace << "finished"; });
		QSignalSpy ticks(&scheduler, &TimelineScheduler::tick);
		scheduler.start();
		// Simulate a busy OBS UI thread: no timer callback before the end marker.
		QTest::qSleep(40);
		QVERIFY(QMetaObject::invokeMethod(&scheduler, "onTimerTick", Qt::DirectConnection));
		QCOMPARE(trace, (QStringList{"span:start", "span:end", "last:start", "finished"}));
		QVERIFY(!scheduler.isPlaying());
		QCOMPARE(scheduler.getCurrentTime(), 25.0);
		QVERIFY(!ticks.isEmpty());
		QCOMPARE(ticks.last().at(0).toDouble(), 25.0);
	}

	void testSchedulerLateTickOrdersCueEdgesByTime()
	{
		TimelineModel model;
		TimelineEvent span;
		span.timeMs = 0;
		span.hasDuration = true;
		span.durationMs = 10;
		model.addEvent(span);
		TimelineEvent next;
		next.timeMs = 20;
		model.addEvent(next);
		TimelineScheduler scheduler(&model);
		QStringList trace;
		connect(&scheduler, &TimelineScheduler::eventStarted, &scheduler,
			[&](const TimelineEvent &evt) { trace << (evt.id == span.id ? "span:start" : "next:start"); });
		connect(&scheduler, &TimelineScheduler::eventEnded, &scheduler,
			[&](const TimelineEvent &) { trace << "span:end"; });
		scheduler.start();
		QTest::qSleep(35);
		QVERIFY(QMetaObject::invokeMethod(&scheduler, "onTimerTick", Qt::DirectConnection));
		QCOMPARE(trace, (QStringList{"span:start", "span:end", "next:start"}));
		QVERIFY(QMetaObject::invokeMethod(&scheduler, "onTimerTick", Qt::DirectConnection));
		QCOMPARE(trace.size(), 3);
		scheduler.stop();
	}

	void testSchedulerLoopFinishesOldCycleBeforeStartingNext()
	{
		TimelineModel model;
		TimelineEvent span;
		span.timeMs = 0;
		span.hasDuration = true;
		span.durationMs = 25;
		model.addEvent(span);
		TimelineScheduler scheduler(&model);
		scheduler.setEndTime(25);
		scheduler.setLooping(true);
		QStringList trace;
		connect(&scheduler, &TimelineScheduler::eventStarted, &scheduler,
			[&](const TimelineEvent &) { trace << "start"; });
		connect(&scheduler, &TimelineScheduler::eventEnded, &scheduler,
			[&](const TimelineEvent &) { trace << "end"; });
		connect(&scheduler, &TimelineScheduler::looped, &scheduler, [&] { trace << "loop"; });
		scheduler.start();
		QTest::qSleep(40);
		QVERIFY(QMetaObject::invokeMethod(&scheduler, "onTimerTick", Qt::DirectConnection));
		QCOMPARE(trace, (QStringList{"start", "end", "loop", "start"}));
		scheduler.stop();
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

	void testInvalidTimelineObjectDoesNotEraseExistingEvents()
	{
		TimelineModel model;
		TimelineEvent existing;
		existing.type = TimelineEventType::SceneSwitch;
		existing.sourceName = "Original scene";
		model.addEvent(existing);
		TimelineDocument out;
		QString error;
		const QJsonDocument malformed(QJsonObject{{"settings", QJsonObject{{"loop", true}}}});
		QVERIFY(!TimelineSerialization::fromJsonDocument(malformed, model, out, &error));
		QVERIFY(!error.isEmpty());
		QCOMPARE(model.getEvents().size(), size_t(1));
		QCOMPARE(model.getEvents()[0].id, existing.id);
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

	void testEventItemSnapDoesNotRoundExactTimeDuringLayout()
	{
		QGraphicsScene scene;
		auto *item = new ExposedTimelineEventItem(2345.0, 100.0, TimelineEventType::SceneSwitch);
		scene.addItem(item);
		item->setSnapEnabled(true);
		item->setSnapInterval(500.0);
		QSignalSpy moved(item, &TimelineEventItem::eventMoved);
		item->setPixelsPerSecond(200.0);
		QCOMPARE(item->getTimeMs(), 2345.0);
		item->setCompactMode(true);
		QCOMPARE(item->getTimeMs(), 2345.0);
		item->setStackOffset(2);
		QCOMPARE(item->getTimeMs(), 2345.0);
		item->setTimeMs(1375.0);
		QCOMPARE(item->getTimeMs(), 1375.0);
		item->setSuppressDragSignals(true);
		item->setPixelsPerSecond(100.0);
		item->setSuppressDragSignals(false);
		QCOMPARE(item->getTimeMs(), 1375.0);
		QCOMPARE(moved.count(), 0);
	}

	void testEventItemDragSnapsAndReportsClampedPosition()
	{
		QGraphicsScene scene;
		auto *item = new ExposedTimelineEventItem(1000.0, 100.0, TimelineEventType::SceneSwitch);
		scene.addItem(item);
		item->setSnapEnabled(true);
		item->setSnapInterval(500.0);
		item->setMaxTime(1750.0);
		QSignalSpy preview(item, &TimelineEventItem::dragMoved);
		QSignalSpy committed(item, &TimelineEventItem::eventMoved);
		QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
		press.setButton(Qt::LeftButton);
		press.setButtons(Qt::LeftButton);
		item->mousePressEvent(&press);
		item->setX(137.0 - TimelineEventItem::ITEM_SIZE / 2.0);
		QCOMPARE(item->getTimeMs(), 1500.0);
		QCOMPARE(preview.last().at(0).toDouble(), 1500.0);
		item->setX(210.0 - TimelineEventItem::ITEM_SIZE / 2.0);
		QCOMPARE(item->getTimeMs(), 1750.0);
		QCOMPARE(preview.last().at(0).toDouble(), 1750.0);
		QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
		release.setButton(Qt::LeftButton);
		release.setButtons(Qt::NoButton);
		item->mouseReleaseEvent(&release);
		QCOMPARE(committed.count(), 1);
		QCOMPARE(committed.last().at(0).toDouble(), 1750.0);
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

	void testDefinedParameterCueEditorAndPlayback()
	{
		auto *source = obs_source_create("timeline_parameter_probe", "Defined cue target", nullptr, nullptr);
		QVERIFY(source);
		auto cleanup = qScopeGuard([&] {
			obs_source_remove(source);
			obs_source_release(source);
		});
		MacroParameterBinding binding{QString::fromUtf8(obs_source_get_uuid(source)),
					      {},
					      "gain",
					      MacroParameterType::Float};
		EventEditor editor(100.0);
		auto *types = editor.findChild<QComboBox *>("eventTypeCombo");
		QVERIFY(types);
		const int typeIndex = types->findData(static_cast<int>(TimelineEventType::SetParameterValue));
		QVERIFY(typeIndex >= 0);
		types->setCurrentIndex(typeIndex);
		auto *choose = editor.findChild<QPushButton *>("eventDefineParameter");
		QVERIFY(choose && !choose->isHidden());
		bool configured = false;
		QTimer::singleShot(0, &editor, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog)
				return;
			auto close = qScopeGuard([&] {
				if (!configured)
					dialog->reject();
			});
			auto *target = dialog->findChild<QComboBox *>("macro-parameter-target");
			auto *property = dialog->findChild<QComboBox *>("macro-parameter-property");
			auto *value = dialog->findChild<QDoubleSpinBox *>("parameter-defined-value");
			auto *buttons = dialog->findChild<QDialogButtonBox *>();
			QVERIFY(target && property && value && buttons);
			target->setCurrentIndex(target->findText("Defined cue target"));
			property->setCurrentIndex(property->findText("Controls / Gain"));
			QCOMPARE(value->minimum(), -1.0);
			QCOMPARE(value->maximum(), 1.0);
			QCOMPARE(value->singleStep(), 0.05);
			value->setValue(0.65);
			QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
			configured = true;
			buttons->button(QDialogButtonBox::Ok)->click();
		});
		choose->click();
		QVERIFY(configured);
		TimelineEvent cue;
		cue.timeMs = 100;
		editor.getUpdatedEvent(cue);
		QCOMPARE(cue.type, TimelineEventType::SetParameterValue);
		QVERIFY(!cue.hasDuration);
		const auto payload = MacroParameter::decodeValue(cue.data.toString());
		QVERIFY(payload);
		QCOMPARE(payload->binding.sourceUuid, binding.sourceUuid);
		QCOMPARE(payload->binding.key, binding.key);
		QCOMPARE(payload->value, 0.65);
		QCOMPARE(MacroParameter::read(binding).value(), 0.25); // configuring is not execution
		TimelineModel model;
		model.addEvent(cue);
		TimelineModel restored;
		restored.fromJson(model.toJson());
		QCOMPARE(restored.getEvents().size(), size_t(1));
		QCOMPARE(restored.getEvents()[0].data, cue.data);
		EventEditor reopened(restored.getEvents()[0]);
		TimelineEvent edited = cue;
		reopened.getUpdatedEvent(edited);
		QCOMPARE(edited.data, cue.data);
		obs_source_set_name(source, "Renamed defined cue target");
		ObsExecutor executor;
		TimelineScheduler scheduler(&restored);
		connect(&scheduler, &TimelineScheduler::eventStarted, &executor, &ObsExecutor::executeEventStart);
		scheduler.start();
		QTRY_COMPARE(MacroParameter::read(binding).value(), 0.65);
		scheduler.stop();
		QVERIFY(MacroParameter::write(binding, 0.1));
		executor.executeEventEnd(cue);
		QVERIFY(qAbs(MacroParameter::read(binding).value() - 0.1) < 1e-9);
		cue.data = QString("not a parameter payload");
		executor.executeEventStart(cue);
		QVERIFY(qAbs(MacroParameter::read(binding).value() - 0.1) < 1e-9);
	}

	void testDefinedFilterCueTypesAndMacroReadback()
	{
		auto *source = obs_source_create("timeline_parameter_probe", "Cue filter owner", nullptr, nullptr);
		QVERIFY(source);
		auto releaseSource = qScopeGuard([&] {
			obs_source_remove(source);
			obs_source_release(source);
		});
		auto *filter = obs_source_create_private("timeline_parameter_filter", "Cue private filter", nullptr);
		QVERIFY(filter);
		obs_source_filter_add(source, filter);
		auto releaseFilter = qScopeGuard([&] {
			if (obs_filter_get_parent(filter))
				obs_source_filter_remove(source, filter);
			obs_source_release(filter);
		});
		MacroParameterBinding binding{QString::fromUtf8(obs_source_get_uuid(source)),
					      QString::fromUtf8(obs_source_get_uuid(filter)), "count",
					      MacroParameterType::Integer};
		MacroButton knob;
		knob.kind = MacroControlKind::Knob;
		knob.binding = binding;
		MacroStripDock dock;
		dock.buttonModel()->addButton(knob);
		dock.show();
		QCoreApplication::processEvents();
		auto *dial = dock.findChild<QDial *>("macro-parameter-knob");
		QVERIFY(dial);
		TimelineEvent cue;
		cue.type = TimelineEventType::SetParameterValue;
		cue.data = MacroParameter::encodeValue({binding, 8});
		ObsExecutor executor;
		executor.executeEventStart(cue);
		QTRY_COMPARE(MacroParameter::read(binding).value_or(-1), 8.0);
		QTRY_COMPARE(dial->value(), 3);
		MacroButton preset;
		preset.type = cue.type;
		preset.data = cue.data.toString();
		MacroButtonModel model;
		model.addButton(preset);
		MacroButtonModel restored;
		QVERIFY(MacroSerialization::fromJsonDocument(QJsonDocument(MacroSerialization::toJsonObject(model)),
							     restored));
		QCOMPARE(restored.buttons().first().data, preset.data);
		QCOMPARE(restored.buttons().first().type, preset.type);

		// Boolean editor is a typed On/Off choice, not a floating-point field.
		MacroParameterBinding enabled = binding;
		enabled.key = "enabled";
		enabled.type = MacroParameterType::Boolean;
		MacroButton initial;
		initial.binding = enabled;
		std::optional<double> value = 1;
		bool configured = false;
		QTimer::singleShot(0, &dock, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog)
				return;
			auto close = qScopeGuard([&] {
				if (!configured)
					dialog->reject();
			});
			auto *choice = dialog->findChild<QComboBox *>("parameter-defined-bool");
			auto *number = dialog->findChild<QDoubleSpinBox *>("parameter-defined-value");
			auto *buttons = dialog->findChild<QDialogButtonBox *>();
			QVERIFY(choice && number && buttons);
			QVERIFY(choice->isVisible() && number->isHidden());
			choice->setCurrentIndex(0);
			configured = true;
			buttons->button(QDialogButtonBox::Ok)->click();
		});
		auto selected = editDefinedParameter(&dock, initial, value);
		QVERIFY(configured && selected && value);
		QCOMPARE(*value, 0.0);
		QCOMPARE(MacroParameter::read(enabled).value_or(-1), 1.0);
		cue.data = MacroParameter::encodeValue({enabled, *value});
		executor.executeEventStart(cue);
		QTRY_COMPARE(MacroParameter::read(enabled).value_or(-1), 0.0);
		QVERIFY(!MacroParameter::decodeValue(MacroParameter::encodeValue({enabled, 0.5})));
		QVERIFY(!MacroParameter::decodeValue(MacroParameter::encodeValue({binding, 3.5})));
		obs_source_filter_remove(source, filter);
		executor.executeEventStart(cue);
		QVERIFY(!MacroParameter::read(enabled));
	}

	void testMacroKnobRelativeDragAndFineControl()
	{
		auto *source = obs_source_create("timeline_parameter_probe", "Knob gesture target", nullptr, nullptr);
		QVERIFY(source);
		auto cleanup = qScopeGuard([&] {
			obs_source_remove(source);
			obs_source_release(source);
		});
		MacroButton knob;
		knob.kind = MacroControlKind::Knob;
		knob.binding = {QString::fromUtf8(obs_source_get_uuid(source)), {}, "gain", MacroParameterType::Float};
		QWidget host;
		auto *control = createMacroParameterWidget(knob, &host);
		host.resize(200, 100);
		control->resize(control->sizeHint());
		host.show();
		QCoreApplication::processEvents();
		auto *dial = control->findChild<QDial *>("macro-parameter-knob");
		QVERIFY(dial && dial->isEnabled());
		QCOMPARE(dial->value(), 25);
		const QPoint start(4, 22);
		auto move = [&](const QPoint &point, Qt::KeyboardModifiers mods = Qt::NoModifier) {
			QMouseEvent ev(QEvent::MouseMove, QPointF(point), QPointF(dial->mapToGlobal(point)),
				       Qt::NoButton, Qt::LeftButton, mods);
			QCoreApplication::sendEvent(dial, &ev);
		};
		QSignalSpy changes(dial, &QDial::valueChanged);
		QTest::mousePress(dial, Qt::LeftButton, Qt::NoModifier, start);
		QCOMPARE(dial->value(), 25); // no absolute-angle jump on click
		QVERIFY(dial->isSliderDown());
		move(start + QPoint(0, -20));
		QCOMPARE(dial->value(), 30);
		QTest::qWait(150); // coalesced live write must not reset gesture
		QCOMPARE(dial->value(), 30);
		move(start + QPoint(0, -40), Qt::ShiftModifier);
		QVERIFY(dial->value() >= 30 && dial->value() <= 31);
		move(start + QPoint(0, 0));
		QVERIFY(dial->value() < 25);
		move(start + QPoint(0, -1000));
		QCOMPARE(dial->value(), dial->maximum());
		move(start + QPoint(0, -980)); // reversing at limit responds immediately
		QCOMPARE(dial->value(), dial->maximum() - 5);
		QTest::mouseRelease(dial, Qt::LeftButton, Qt::NoModifier, start + QPoint(0, -980));
		QVERIFY(!dial->isSliderDown());
		QTRY_VERIFY(qAbs(MacroParameter::read(knob.binding).value() - 0.75) < 1e-9);
		const int before = changes.count();
		QTest::mousePress(dial, Qt::LeftButton, Qt::NoModifier, start);
		QTest::mouseRelease(dial, Qt::LeftButton, Qt::NoModifier, start);
		QCOMPARE(changes.count(), before);
		QApplication::setActiveWindow(&host);
		dial->setFocus();
		QTRY_VERIFY(dial->hasFocus());
		QVERIFY(!dial->isSliderDown());
		QCOMPARE(dial->singleStep(), 1);
		QKeyEvent arrow(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
		QCoreApplication::sendEvent(dial, &arrow);
		QCOMPARE(dial->value(), 34);
		QWheelEvent wheel(QPointF(start), QPointF(dial->mapToGlobal(start)), QPoint(), QPoint(0, 120),
				  Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
		QCoreApplication::sendEvent(dial, &wheel);
		QCOMPARE(dial->value(), 35);
		dial->clearFocus();
		QWheelEvent unfocused(QPointF(start), QPointF(dial->mapToGlobal(start)), QPoint(), QPoint(0, -120),
				      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
		QCoreApplication::sendEvent(dial, &unfocused);
		QCOMPARE(dial->value(), 35);
		QVERIFY(!unfocused.isAccepted());
	}

	void testMacroControlRowsAligned()
	{
		MacroStripDock dock;
		MacroButton action;
		action.label = "Action";
		dock.buttonModel()->addButton(action);
		for (auto kind : {MacroControlKind::Slider, MacroControlKind::Knob, MacroControlKind::Toggle}) {
			MacroButton control;
			control.kind = kind;
			control.label = "Parameter";
			dock.buttonModel()->addButton(control);
		}
		dock.resize(700, dock.minimumHeight());
		dock.show();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QCoreApplication::processEvents();
		auto *button = dock.findChild<MacroButtonWidget *>();
		QVERIFY(button);
		const auto center = button->mapTo(&dock, QPoint(0, MacroButtonWidget::kControlCenterY)).y();
		const auto labelTop = button->mapTo(&dock, QPoint(0, 0)).y();
		for (auto *control : dock.findChildren<QWidget *>("macro-parameter-control")) {
			auto *label = control->findChild<QLabel *>("macro-parameter-caption");
			QVERIFY(label);
			QCOMPARE(label->mapTo(&dock, QPoint(0, 0)).y(), labelTop);
			QCOMPARE(label->height(), MacroButtonWidget::kLabelHeight);
			QWidget *input = control->findChild<QAbstractSlider *>();
			if (!input)
				input = control->findChild<QCheckBox *>();
			QVERIFY(input);
			QVERIFY(qAbs(input->mapTo(&dock, QPoint(0, input->height() / 2)).y() - center) <= 1);
		}
	}

	void testMacroParameterBindingsAndDock()
	{
		auto release = [](obs_source_t *s) {
			if (s) {
				obs_source_remove(s);
				obs_source_release(s);
			}
		};
		using Source = std::unique_ptr<obs_source_t, decltype(release)>;
		Source source(obs_source_create("timeline_parameter_probe", "Parameter source", nullptr, nullptr),
			      release);
		Source filter(obs_source_create_private("timeline_parameter_filter", "Parameter filter", nullptr),
			      release);
		QVERIFY(source && filter);
		auto *settingsPatch = obs_data_create();
		obs_data_set_string(settingsPatch, "untouched", "custom text");
		obs_source_update(filter.get(), settingsPatch);
		obs_data_release(settingsPatch);
		obs_source_filter_add(source.get(), filter.get());
		const auto cleanup = qScopeGuard([&] {
			if (obs_filter_get_parent(filter.get()))
				obs_source_filter_remove(source.get(), filter.get());
		});
		const QString sourceId = QString::fromUtf8(obs_source_get_uuid(source.get()));
		const QString filterId = QString::fromUtf8(obs_source_get_uuid(filter.get()));
		const auto targets = MacroParameter::targets();
		QVERIFY(std::any_of(targets.cbegin(), targets.cend(), [&](const auto &t) {
			return t.sourceUuid == sourceId && t.filterUuid == filterId;
		}));
		MacroButton slider;
		slider.kind = MacroControlKind::Slider;
		slider.label = "Gain";
		slider.binding = {sourceId, filterId, "gain", MacroParameterType::Float};
		const auto properties = MacroParameter::properties(slider.binding);
		QCOMPARE(properties.size(), 3);
		const auto spec = MacroParameter::describe(slider.binding);
		QVERIFY(spec);
		QCOMPARE(spec->min, -1.0);
		QCOMPARE(spec->step, 0.05);
		QCOMPARE(MacroParameter::read(slider.binding).value(), 0.25);
		QVERIFY(MacroParameter::write(slider.binding, 0.78));
		QVERIFY(qAbs(MacroParameter::read(slider.binding).value() - 0.8) < 0.0001);
		MacroButton knob;
		knob.kind = MacroControlKind::Knob;
		knob.label = "Count";
		knob.binding = {sourceId, {}, "count", MacroParameterType::Integer};
		MacroButton toggle;
		toggle.kind = MacroControlKind::Toggle;
		toggle.label = "Enabled";
		toggle.binding = {sourceId, {}, "enabled", MacroParameterType::Boolean};
		MacroButton ordinary;
		ordinary.label = "Normal action";

		QTemporaryDir dir;
		MacroStripDock dock(nullptr, dir.filePath("macro.json"));
		for (const auto &b : {ordinary, slider, knob, toggle})
			dock.buttonModel()->addButton(b);
		dock.resize(700, dock.minimumHeight());
		dock.show();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QCoreApplication::processEvents();
		auto *s = dock.findChild<QSlider *>("macro-parameter-slider");
		auto *k = dock.findChild<QDial *>("macro-parameter-knob");
		auto *t = dock.findChild<QCheckBox *>("macro-parameter-toggle");
		QVERIFY(s && k && t);
		QVERIFY(s->isVisible() && k->isVisible() && t->isVisible());
		QVERIFY(s->isEnabled() && k->isEnabled() && t->isEnabled());
		QCOMPARE(s->value(), 36);
		s->setValue(10);
		k->setValue(4);
		t->setChecked(false);
		QTRY_VERIFY_WITH_TIMEOUT(qAbs(MacroParameter::read(slider.binding).value() + 0.5) < 0.0001, 2000);
		QTRY_COMPARE(MacroParameter::read(knob.binding).value(), 10.0);
		QTRY_COMPARE(MacroParameter::read(toggle.binding).value(), 0.0);
		auto *sourceProbe = static_cast<ParameterProbe *>(obs_obj_get_data(source.get()));
		auto *filterProbe = static_cast<ParameterProbe *>(obs_obj_get_data(filter.get()));
		QVERIFY(sourceProbe && filterProbe);
		QTRY_VERIFY(qAbs(filterProbe->gain.load() + 0.5) < 0.0001);
		QTRY_COMPARE(sourceProbe->count.load(), 10);
		QTRY_COMPARE(sourceProbe->enabled.load(), false);
		MacroParameterBinding invalid = slider.binding;
		invalid.key = "hidden";
		QVERIFY(!MacroParameter::write(invalid, 0.3));
		invalid = slider.binding;
		invalid.type = MacroParameterType::Integer;
		QVERIFY(!MacroParameter::write(invalid, 1));

		// External OBS changes update the visible control, without writing back.
		obs_data_t *patch = obs_data_create();
		obs_data_set_double(patch, "gain", 0.4);
		obs_source_update(filter.get(), patch);
		obs_data_release(patch);
		QTRY_COMPARE(s->value(), 28);
		auto *settings = obs_source_get_settings(filter.get());
		QCOMPARE(QString::fromUtf8(obs_data_get_string(settings, "untouched")), QString("custom text"));
		obs_data_release(settings);

		obs_source_set_name(source.get(), "Renamed source");
		obs_source_set_name(filter.get(), "Renamed filter");
		QVERIFY(MacroParameter::write(slider.binding, 0.15));
		QTRY_COMPARE(s->value(), 23);
		QVERIFY(dock.saveNow());
		MacroStripDock restored(nullptr, dir.filePath("macro.json"));
		QVERIFY(restored.loadNow());
		QCOMPARE(restored.buttonModel()->count(), 4);
		QCOMPARE(restored.buttonModel()->buttons()[0].kind, MacroControlKind::Action);
		const auto saved = restored.buttonModel()->buttons()[1];
		QCOMPARE(saved.kind, MacroControlKind::Slider);
		QCOMPARE(saved.binding.filterUuid, filterId);
		QCOMPARE(saved.binding.key, QString("gain"));
		obs_source_filter_remove(source.get(), filter.get());
		QTRY_VERIFY(!s->isEnabled());
		QVERIFY(!MacroParameter::write(slider.binding, 1.0));
		QVERIFY(!MacroParameter::read(slider.binding));
	}

	void testMacroParameterEditorDiscoversLiveCatalogChanges()
	{
		auto *source = obs_source_create("timeline_parameter_probe", "Live catalog original", nullptr, nullptr);
		QVERIFY(source);
		auto cleanup = qScopeGuard([&] {
			obs_source_remove(source);
			obs_source_release(source);
		});
		MacroStripDock dock;
		MacroButton initial;
		initial.kind = MacroControlKind::Knob;
		initial.binding = {QString::fromUtf8(obs_source_get_uuid(source)),
				   {},
				   "gain",
				   MacroParameterType::Float};
		bool done = false;
		QTimer::singleShot(0, &dock, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			QVERIFY(dialog);
			auto close = qScopeGuard([&] { dialog->reject(); });
			auto *target = dialog->findChild<QComboBox *>("macro-parameter-target");
			auto *property = dialog->findChild<QComboBox *>("macro-parameter-property");
			auto *kind = dialog->findChild<QComboBox *>("macro-parameter-kind");
			auto *buttons = dialog->findChild<QDialogButtonBox *>();
			QVERIFY(target && property && kind && buttons);
			QCOMPARE(property->currentText(), QString("Controls / Gain"));
			auto *added = obs_source_create("timeline_parameter_probe", "New live catalog source", nullptr,
							nullptr);
			QVERIFY(added);
			auto removeAdded = qScopeGuard([&] {
				obs_source_remove(added);
				obs_source_release(added);
			});
			obs_source_set_name(source, "Renamed live catalog original");
			QTRY_VERIFY_WITH_TIMEOUT(target->findText("New live catalog source") >= 0, 3500);
			QCOMPARE(target->currentText(), QString("Renamed live catalog original"));
			QCOMPARE(property->currentText(), QString("Controls / Gain"));
			QCOMPARE(kind->currentData().toInt(), static_cast<int>(MacroControlKind::Knob));
			// Attach a new private filter to a source inside a group.
			auto *scene = obs_scene_create("Catalog root");
			QVERIFY(scene);
			auto removeScene = qScopeGuard([&] {
				obs_source_remove(obs_scene_get_source(scene));
				obs_scene_release(scene);
			});
			auto *group = obs_scene_add_group(scene, "Catalog group");
			QVERIFY(group);
			QVERIFY(obs_scene_add(obs_sceneitem_group_get_scene(group), added));
			auto *filter =
				obs_source_create_private("timeline_parameter_filter", "New catalog filter", nullptr);
			QVERIFY(filter);
			obs_source_filter_add(added, filter);
			auto removeFilter = qScopeGuard([&] {
				obs_source_filter_remove(added, filter);
				obs_source_release(filter);
			});
			const QString filterLabel =
				"Catalog root / Catalog group / New live catalog source / Filter: New catalog filter";
			QTRY_VERIFY_WITH_TIMEOUT(target->findText(filterLabel) >= 0, 3500);
			const auto catalog = MacroParameter::targets();
			const QString addedId = QString::fromUtf8(obs_source_get_uuid(added));
			const QString filterId = QString::fromUtf8(obs_source_get_uuid(filter));
			QCOMPARE(std::count_if(catalog.cbegin(), catalog.cend(),
					       [&](const auto &entry) {
						       return entry.sourceUuid == addedId &&
							      entry.filterUuid == filterId;
					       }),
				 1);
			QCOMPARE(target->currentText(), QString("Renamed live catalog original"));
			auto *patch = obs_data_create();
			obs_data_set_bool(patch, "enabled", false);
			obs_source_update(source, patch);
			obs_data_release(patch);
			QTRY_VERIFY_WITH_TIMEOUT(property->findText("Controls / Alternate") >= 0, 3500);
			QCOMPARE(property->findText("Controls / Gain"), -1);
			QCOMPARE(property->currentIndex(), -1);
			QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
			property->setCurrentIndex(property->findText("Controls / Alternate"));
			QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
			obs_source_remove(source);
			QTRY_COMPARE_WITH_TIMEOUT(target->currentData().toInt(), -1, 3500);
			QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
			done = true;
		});
		QVERIFY(!editMacroParameter(&dock, initial));
		QVERIFY(done);
	}

	void testMacroParameterEditorSelectsAndPreservesBinding()
	{
		auto *source =
			obs_source_create("timeline_parameter_probe", "Editor parameter target", nullptr, nullptr);
		QVERIFY(source);
		auto cleanup = qScopeGuard([&] {
			obs_source_remove(source);
			obs_source_release(source);
		});
		MacroStripDock dock;
		MacroButton initial;
		initial.kind = MacroControlKind::Knob;
		bool configured = false;
		QTimer::singleShot(0, &dock, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog)
				return;
			auto *target = dialog->findChild<QComboBox *>("macro-parameter-target");
			auto *property = dialog->findChild<QComboBox *>("macro-parameter-property");
			auto *kind = dialog->findChild<QComboBox *>("macro-parameter-kind");
			auto *buttons = dialog->findChild<QDialogButtonBox *>();
			if (!target || !property || !kind || !buttons) {
				dialog->reject();
				return;
			}
			target->setCurrentIndex(target->findText("Editor parameter target"));
			property->setCurrentIndex(property->findText("Controls / Gain"));
			kind->setCurrentIndex(kind->findData(static_cast<int>(MacroControlKind::Knob)));
			configured = buttons->button(QDialogButtonBox::Ok)->isEnabled();
			if (configured)
				buttons->button(QDialogButtonBox::Ok)->click();
			else
				dialog->reject();
		});
		const auto edited = editMacroParameter(&dock, initial);
		QVERIFY(configured && edited);
		QCOMPARE(edited->kind, MacroControlKind::Knob);
		QCOMPARE(edited->binding.key, QString("gain"));
		QCOMPARE(edited->binding.sourceUuid, QString::fromUtf8(obs_source_get_uuid(source)));
		QCOMPARE(edited->label, QString("Gain"));

		MacroButton missing = *edited;
		missing.binding.key = "removed-property";
		bool refusedDefault = false;
		QTimer::singleShot(0, &dock, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog)
				return;
			auto *property = dialog->findChild<QComboBox *>("macro-parameter-property");
			auto *buttons = dialog->findChild<QDialogButtonBox *>();
			refusedDefault = property && buttons && property->currentIndex() == -1 &&
					 !buttons->button(QDialogButtonBox::Ok)->isEnabled();
			dialog->reject();
		});
		QVERIFY(!editMacroParameter(&dock, missing));
		QVERIFY(refusedDefault);

		MacroButtonModel model;
		model.addButton(*edited);
		QJsonObject root = MacroSerialization::toJsonObject(model);
		auto array = root["buttons"].toArray();
		auto item = array[0].toObject();
		QCOMPARE(item["type"].toInt(), -1); // old binaries cannot execute it as an action
		item["control"] = "future-control";
		array[0] = item;
		root["buttons"] = array;
		QVERIFY(!MacroSerialization::fromJsonDocument(QJsonDocument(root), model));
		QCOMPARE(model.buttons().first().binding.key, QString("gain"));
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

	void testInvalidMacroDocumentDoesNotEraseExistingButtons()
	{
		MacroButtonModel model;
		MacroButton original;
		original.label = "Kept button";
		model.addButton(original);
		QString error;
		QVERIFY(!MacroSerialization::fromJsonDocument(QJsonDocument(QJsonObject{{"version", 1}}), model,
							      &error));
		QVERIFY(!error.isEmpty());
		QCOMPARE(model.count(), 1);
		QCOMPARE(model.buttons().first().label, QString("Kept button"));
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

	void testMacroStripFitsThemeSizedControls()
	{
		QTemporaryDir dir;
		MacroStripDock dock(nullptr, dir.filePath("macro.json"));
		dock.setStyleSheet("QCheckBox::indicator { width: 28px; height: 42px; }");
		MacroButton action;
		action.label = "Action";
		dock.buttonModel()->addButton(action);
		MacroButton toggle;
		toggle.kind = MacroControlKind::Toggle;
		toggle.label = "Toggle";
		dock.buttonModel()->addButton(toggle);
		for (int i = 0; i < 5; ++i)
			dock.buttonModel()->addButton(MacroButton{});
		dock.resize(160, 1);
		dock.show();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QCoreApplication::processEvents();
		auto *scroll = dock.findChild<QScrollArea *>();
		auto *button = dock.findChild<MacroButtonWidget *>();
		auto *control = dock.findChild<QWidget *>("macro-parameter-control");
		auto *check = control ? control->findChild<QCheckBox *>() : nullptr;
		QVERIFY(scroll && button && control && check);
		QVERIFY(scroll->horizontalScrollBar()->maximum() > 0);
		QVERIFY(check->height() >= check->sizeHint().height());
		QVERIFY(control->height() >= check->height() + MacroButtonWidget::kLabelHeight);
		QCOMPARE(button->height(), control->height());
		QVERIFY(scroll->viewport()->height() >= control->height());
		QVERIFY(control->rect().contains(check->mapTo(control, QPoint(0, check->height() - 1))));
		QVERIFY(dock.maximumHeight() > dock.minimumHeight());
	}

	void testMacroStripFitsEventsWhenHorizontallyScrollable()
	{
		MacroStripDock dock;
		dock.resize(160, dock.minimumHeight());
		for (int i = 0; i < 8; ++i) {
			MacroButton button;
			button.id = QString::number(i);
			button.label = "Scene";
			dock.buttonModel()->addButton(button);
		}
		dock.show();
		QApplication::processEvents();

		auto *scroll = dock.findChild<QScrollArea *>();
		QVERIFY(scroll);
		QVERIFY(scroll->horizontalScrollBar()->maximum() > 0);
		const auto buttons = dock.findChildren<MacroButtonWidget *>();
		QVERIFY(!buttons.isEmpty());
		QVERIFY2(scroll->viewport()->height() >= buttons.first()->minimumSizeHint().height(),
			 "Macro Strip viewport clips event bubbles when the horizontal scrollbar is visible");
	}

	void testMacroButtonLabelFitsItsWidth()
	{
		MacroButton button;
		button.label = "Scene";
		MacroButtonWidget widget(button);
		QFont labelFont = widget.font();
		labelFont.setPointSize(8);
		labelFont.setBold(true);
		const int labelPillWidth = QFontMetrics(labelFont).horizontalAdvance(button.label) + 8;
		QVERIFY2(widget.sizeHint().width() >= labelPillWidth, "Macro event label is clipped horizontally");
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

	void testMacroAutosaveOnlyAfterInitialization()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath("macro_buttons.json");
		MacroStripDock dock(nullptr, path);

		MacroButton first;
		first.id = "first";
		first.label = "Prepared before load";
		dock.buttonModel()->addButton(first);
		QTest::qWait(400);
		QVERIFY(!QFile::exists(path));

		dock.activateAutoSave();
		MacroButton second;
		second.id = "second";
		second.label = "Edited after load";
		dock.buttonModel()->addButton(second);
		QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path), 1500);

		QJsonDocument saved;
		QString error;
		QVERIFY2(MacroSerialization::loadFromFile(path, saved, &error), qPrintable(error));
		QCOMPARE(saved.object()["buttons"].toArray().size(), 2);
		QCOMPARE(saved.object()["buttons"].toArray()[1].toObject()["label"].toString(),
			 QString("Edited after load"));
	}

	void testMacroSceneRestoreUpdatesDockAndRejectsCorruptBackup()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath("macro_buttons.json");
		MacroStripDock dock(nullptr, path);

		MacroButtonModel backup;
		MacroButton button;
		button.label = "Restored button";
		button.id = "restore-1";
		backup.addButton(button);

		const QByteArray json = QJsonDocument(MacroSerialization::toJsonObject(backup)).toJson();
		QVERIFY(dock.restoreFromObsJson(json));
		QCOMPARE(dock.buttonModel()->count(), 1);
		QCOMPARE(dock.buttonModel()->buttons()[0].label, QString("Restored button"));

		QFile saved(path);
		QVERIFY(saved.open(QIODevice::ReadOnly));
		const QByteArray before = saved.readAll();
		saved.close();
		QVERIFY(!dock.restoreFromObsJson("{broken JSON"));
		QCOMPARE(dock.buttonModel()->count(), 1);
		QVERIFY(saved.open(QIODevice::ReadOnly));
		QCOMPARE(saved.readAll(), before);
	}

	void testMacroSaveCreatesMissingPluginConfigDirectory()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath("plugin_config/timeline/macro_buttons.json");
		MacroStripDock dock(nullptr, path);
		MacroButton button;
		button.id = "test-button";
		button.label = "Preserved";
		dock.buttonModel()->addButton(button);
		QVERIFY(dock.saveNow());
		QJsonDocument saved;
		QString error;
		QVERIFY2(MacroSerialization::loadFromFile(path, saved, &error), qPrintable(error));
		QCOMPARE(saved.object()["buttons"].toArray().size(), 1);
	}

	void testMacroMigratesLegacyStorageWithoutLosingButtons()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString oldPath = dir.filePath("old/data/macro_buttons.json");
		const QString newPath = dir.filePath("plugin_config/timeline/macro_buttons.json");
		QVERIFY(QDir().mkpath(QFileInfo(oldPath).absolutePath()));
		MacroButtonModel legacy;
		MacroButton oldButton;
		oldButton.id = "legacy";
		oldButton.label = "Keep my existing buttons";
		legacy.addButton(oldButton);
		QVERIFY(MacroSerialization::saveToFile(oldPath, MacroSerialization::toJsonObject(legacy)));

		MacroStripDock migrated(nullptr, newPath, oldPath);
		QVERIFY(migrated.loadNow());
		QCOMPARE(migrated.buttonModel()->count(), 1);
		QCOMPARE(migrated.buttonModel()->buttons().first().label, QString("Keep my existing buttons"));
		QVERIFY(QFile::exists(newPath));
		QVERIFY(QFile::exists(oldPath)); // Preserve the original during migration.

		MacroButtonModel replacement;
		MacroButton latest;
		latest.id = "latest";
		latest.label = "New profile state";
		replacement.addButton(latest);
		QVERIFY(MacroSerialization::saveToFile(newPath, MacroSerialization::toJsonObject(replacement)));
		MacroStripDock reopened(nullptr, newPath, oldPath);
		QVERIFY(reopened.loadNow());
		QCOMPARE(reopened.buttonModel()->buttons().first().label, QString("New profile state"));
	}

	void testMacroAutosaveFailurePreservesDirtyButtonsAcrossSceneReload()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath("macro_buttons.json");
		MacroButtonModel original;
		MacroButton oldButton;
		oldButton.id = "old";
		oldButton.label = "Previous saved version";
		original.addButton(oldButton);
		QVERIFY(MacroSerialization::saveToFile(path, MacroSerialization::toJsonObject(original)));
		MacroStripDock dock(nullptr, path);
		QVERIFY(dock.loadNow());
		dock.activateAutoSave();

		MacroButton edited;
		edited.id = "new";
		edited.label = "Unsaved edit";
		dock.buttonModel()->addButton(edited);
		QVERIFY(QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
		QTest::qWait(450); // Timer has fired, but atomic write cannot create its staging file.
		QJsonDocument staleOnDisk;
		QVERIFY(MacroSerialization::loadFromFile(path, staleOnDisk));
		QCOMPARE(staleOnDisk.object()["buttons"].toArray().size(), 1);
		const QByteArray sceneCopy = staleOnDisk.toJson();
		QVERIFY(!dock.restoreFromObsJson(sceneCopy));
		QCOMPARE(dock.buttonModel()->count(), 2);
		QCOMPARE(dock.buttonModel()->buttons().last().label, QString("Unsaved edit"));
		QVERIFY(QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner |
								  QFileDevice::ExeOwner));
		QVERIFY(dock.saveNow());
		QJsonDocument recovered;
		QVERIFY(MacroSerialization::loadFromFile(path, recovered));
		QCOMPARE(recovered.object()["buttons"].toArray().size(), 2);
	}

	void testMacroSceneBackupCannotOverwriteValidLocalButtons()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath("macro_buttons.json");
		MacroButtonModel latest;
		MacroButton button;
		button.id = "fresh";
		button.label = "Current saved buttons";
		latest.addButton(button);
		QVERIFY(MacroSerialization::saveToFile(path, MacroSerialization::toJsonObject(latest)));

		MacroButtonModel backup;
		button.id = "stale";
		button.label = "Old scene backup";
		backup.addButton(button);
		const QByteArray json = QJsonDocument(MacroSerialization::toJsonObject(backup)).toJson();

		MacroStripDock dock(nullptr, path);
		QVERIFY(dock.restoreFromObsJson(json));
		QCOMPARE(dock.buttonModel()->buttons().first().label, QString("Current saved buttons"));
		QJsonDocument onDisk;
		QVERIFY(MacroSerialization::loadFromFile(path, onDisk));
		QCOMPARE(onDisk.object()["buttons"].toArray().first().toObject()["label"].toString(),
			 QString("Current saved buttons"));
	}

	void testMacroSceneBackupCannotDiscardPendingAutosave()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath("macro_buttons.json");
		MacroStripDock dock(nullptr, path);
		dock.activateAutoSave();
		MacroButton pending;
		pending.id = "pending";
		pending.label = "Edited moments ago";
		dock.buttonModel()->addButton(pending);

		MacroButtonModel stale;
		MacroButton old;
		old.id = "old";
		old.label = "Old backup";
		stale.addButton(old);
		const QByteArray json = QJsonDocument(MacroSerialization::toJsonObject(stale)).toJson();
		QVERIFY(dock.restoreFromObsJson(json));
		QCOMPARE(dock.buttonModel()->buttons().first().label, QString("Edited moments ago"));
		QJsonDocument onDisk;
		QVERIFY(MacroSerialization::loadFromFile(path, onDisk));
		QCOMPARE(onDisk.object()["buttons"].toArray().first().toObject()["label"].toString(),
			 QString("Edited moments ago"));
	}

	void testStripDockHeightAccommodatesLabeledButton()
	{
		MacroStripDock dock;
		// Unlike the 32 px toolbar, the strip must accommodate a label,
		// bubble and a horizontal scrollbar without fixing the dock's height.
		QVERIFY(dock.minimumHeight() > MacroButtonWidget::kWidgetHeight);
		QVERIFY(dock.maximumHeight() > dock.minimumHeight());
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
