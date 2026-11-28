#include <QGraphicsScene>
#include <QPainter>
#include <QApplication>
#include <QElapsedTimer>
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
#include <QDir>
#include <QDataStream>
#include <util/text-lookup.h>
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
#include "data-condition.h"
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
#include "macro-automation-recorder.h"
#include "event-editor.h"
#include "hotkey-catalog.h"
#include <obs-audio-controls.h>
#include <QPlainTextEdit>
#include <QInputDialog>
#include "plugin-integrations.h"
#include <QMenu>
#include <QComboBox>
#include <QLineEdit>

// Mock helpers from mock_frontend.cpp
extern QJsonObject g_websocket_last_request;
extern QJsonArray g_websocket_events;
extern int g_websocket_request_count;
extern bool g_websocket_available;
extern bool g_websocket_dsk_success;
extern unsigned int g_websocket_status;
extern obs_source_t *g_current_scene;
extern obs_source_t *g_current_preview_scene;
extern obs_source_t *g_current_transition;
extern std::vector<obs_source_t *> g_transitions;
extern int g_transition_duration_ms;
extern lookup_t *g_documentation_locale;

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
	std::atomic<int> propertyReads{0};
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
			if (data)
				++static_cast<ParameterProbe *>(data)->propertyReads;
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
			obs_properties_add_text(props, "caption", "Caption", OBS_TEXT_MULTILINE);
			obs_properties_add_text(props, "secret", "Secret", OBS_TEXT_PASSWORD);
			obs_properties_add_text(props, "notice", "Notice", OBS_TEXT_INFO);
			auto *hidden = obs_properties_add_float(props, "hidden", "Hidden", 0, 1, 0.1);
			obs_property_set_visible(hidden, false);
			return props;
		};
		obs_register_source(&parameter);
		parameter.id = "timeline_parameter_filter";
		parameter.type = OBS_SOURCE_TYPE_FILTER;
		obs_register_source(&parameter);

		parameter.id = "timeline_audio_probe";
		parameter.type = OBS_SOURCE_TYPE_INPUT;
		parameter.output_flags = OBS_SOURCE_AUDIO;
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
		g_websocket_events = {};
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
		evt.endAction = "select-overlay";
		evt.endScene = "Next Overlay";
		executor.executeEventEnd(evt);
		QCOMPARE(g_websocket_request_count, 3);
		QCOMPARE(g_websocket_last_request.value("requestData").toObject().value("scene").toString(),
			 QString("Next Overlay"));
		evt.endAction = "keep";
		executor.executeEventEnd(evt);
		QCOMPARE(g_websocket_request_count, 3);
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

	void testDataConditionalScene()
	{
		using namespace DataCondition;
		Condition c{"aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa", ">=", 0.5, 5000};
		const auto encoded = encode(c);
		QVERIFY(decode(encoded).has_value());
		QVERIFY(matches(c, "0.75", 100));
		QVERIFY(!matches(c, "0.49", 100));
		QVERIFY(!matches(c, "0.75", 5000));
		QVERIFY(!matches(c, "\"0.75\"", 100));
		QVERIFY(!matches(c, "true", 100));
		QVERIFY(!matches(c, "null", 100));
		QVERIFY(!matches(c, "[0.75]", 100));
		QVERIFY(!matches(c, "1,2", 100));
		c.op = "true";
		QVERIFY(matches(c, "true", 100));
		QVERIFY(!matches(c, "1", 100));
		QVERIFY(!decode("garbage"));
		c.op = "invalid";
		QVERIFY(!decode(encode(c)));
		c.op = ">=";
		TimelineEvent evt;
		evt.type = TimelineEventType::DataConditionalScene;
		evt.sourceName = "Scene 2";
		evt.data = encode(c);
		evt.hasDuration = true;
		evt.durationMs = 500;
		TimelineModel model, restored;
		model.addEvent(evt);
		restored.fromJson(model.toJson());
		QCOMPARE(restored.getEvents().size(), size_t(1));
		const auto saved = restored.getEvents().front();
		QCOMPARE(saved.type, TimelineEventType::DataConditionalScene);
		QVERIFY(!saved.hasDuration);
		QCOMPARE(saved.data.toString(), encode(c));
		EventEditor editor(saved);
		QCOMPARE(editor.findChild<QComboBox *>("dataConditionSource")->currentData().toString(), c.uuid);
		TimelineEvent revised;
		editor.getUpdatedEvent(revised);
		QCOMPARE(revised.data.toString(), encode(c));
		QCOMPARE(revised.sourceName, evt.sourceName);
		QVERIFY(!revised.hasDuration);
		if (scene2) {
			ObsExecutor executor;
			auto *before = obs_frontend_get_current_scene();
			executor.executeEventStart(saved);
			QCOMPARE(g_current_scene, before);
			if (before)
				obs_source_release(before);
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
			 9 + (PluginIntegrations::instance().actionAvailable(TimelineEventType::DownstreamKeyerScene)
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

	void testOwnerAwareHotkeysResolveExactlyAndSurviveRenames()
	{
		auto *first = obs_source_create("timeline_parameter_probe", "Hotkey Owner A", nullptr, nullptr);
		auto *second = obs_source_create("timeline_parameter_probe", "Hotkey Owner B", nullptr, nullptr);
		QVERIFY(first && second);
		auto releaseSources = qScopeGuard([&] {
			obs_source_remove(first);
			obs_source_release(first);
			obs_source_remove(second);
			obs_source_release(second);
		});
		// The OBS frontend enables routing; reproduce that runtime setting
		// in this libobs-only harness without changing production globals.
		obs_hotkey_enable_callback_rerouting(true);
		auto restoreRouting = qScopeGuard([] { obs_hotkey_enable_callback_rerouting(false); });
		std::atomic<int> a{0}, b{0};
		auto callback = [](void *data, obs_hotkey_id, obs_hotkey_t *, bool down) {
			if (down)
				++*static_cast<std::atomic<int> *>(data);
		};
		const auto idA =
			obs_hotkey_register_source(first, "timeline.tests.same", "Shared action", callback, &a);
		auto idB = obs_hotkey_register_source(second, "timeline.tests.same", "Shared action", callback, &b);
		auto unregister = qScopeGuard([&] {
			obs_hotkey_unregister(idA);
			if (idB != OBS_INVALID_HOTKEY_ID)
				obs_hotkey_unregister(idB);
		});
		QVERIFY(idA != OBS_INVALID_HOTKEY_ID && idB != OBS_INVALID_HOTKEY_ID);
		EventEditor editor(0.0);
		auto *types = editor.findChild<QComboBox *>("eventTypeCombo");
		auto *target = editor.findChild<QComboBox *>("hotkeyTargetCombo");
		auto *search = editor.findChild<QLineEdit *>("hotkeySearchEdit");
		QVERIFY(types && target && search);
		types->setCurrentIndex(types->findData(static_cast<int>(TimelineEventType::HotkeyTrigger)));
		search->setText("Hotkey Owner B");
		QCOMPARE(target->currentText(), QString("Shared action"));
		TimelineEvent cue;
		editor.getUpdatedEvent(cue);
		QCOMPARE(cue.sourceName, QString("timeline.tests.same"));
		QVERIFY(!cue.data.toString().isEmpty());
		ObsExecutor executor;
		executor.executeEventStart(cue);
		QCOMPARE(a.load(), 0);
		QCOMPARE(b.load(), 1);
		obs_source_set_name(second, "Hotkey Owner Renamed");
		obs_hotkey_unregister(idB);
		idB = obs_hotkey_register_source(second, "timeline.tests.same", "Shared action", callback, &b);
		QVERIFY(idB != OBS_INVALID_HOTKEY_ID);
		TimelineModel model;
		model.addEvent(cue);
		TimelineModel restored;
		restored.fromJson(model.toJson());
		QCOMPARE(restored.getEvents().size(), size_t(1));
		QCOMPARE(restored.getEvents()[0].data, cue.data);
		EventEditor reopened(restored.getEvents()[0]);
		auto *reopenedTarget = reopened.findChild<QComboBox *>("hotkeyTargetCombo");
		auto *reopenedSearch = reopened.findChild<QLineEdit *>("hotkeySearchEdit");
		QVERIFY(reopenedTarget && reopenedSearch);
		reopenedSearch->setText("Hotkey Owner A");
		// Filtering an already bound cue must not silently select a different owner.
		QCOMPARE(reopenedTarget->currentIndex(), -1);
		reopenedSearch->clear();
		QCOMPARE(reopenedTarget->currentText(), QString("Shared action"));
		TimelineEvent saved = cue;
		reopened.getUpdatedEvent(saved);
		QCOMPARE(saved.data, cue.data);
		executor.executeEventStart(saved);
		QCOMPARE(a.load(), 0);
		QCOMPARE(b.load(), 2);
		// If an entire identity collides, there is no safe persistent choice.
		const auto duplicate =
			obs_hotkey_register_source(second, "timeline.tests.same", "Duplicate action", callback, &b);
		QVERIFY(duplicate != OBS_INVALID_HOTKEY_ID);
		auto releaseDuplicate = qScopeGuard([&] { obs_hotkey_unregister(duplicate); });
		QCOMPARE(HotkeyCatalog::resolve(cue.sourceName, cue.data.toString()), OBS_INVALID_HOTKEY_ID);
		executor.executeEventStart(cue);
		QCOMPARE(b.load(), 2);
		obs_hotkey_unregister(duplicate);
		releaseDuplicate.dismiss();
		MacroButton button;
		button.type = cue.type;
		button.sourceName = cue.sourceName;
		button.data = cue.data.toString();
		MacroButtonModel buttons;
		buttons.addButton(button);
		MacroButtonModel loaded;
		QVERIFY(MacroSerialization::fromJsonDocument(QJsonDocument(MacroSerialization::toJsonObject(buttons)),
							     loaded));
		QCOMPARE(loaded.buttons().first().data, button.data);
		obs_hotkey_unregister(idB);
		idB = OBS_INVALID_HOTKEY_ID;
		EventEditor missing(cue);
		missing.getUpdatedEvent(saved);
		QCOMPARE(saved.data, cue.data);
		executor.executeEventStart(saved);
		QCOMPARE(a.load(), 0);
		QCOMPARE(b.load(), 2);
		cue.data = QString("hotkey:v1:invalid");
		executor.executeEventStart(cue);
		QCOMPARE(a.load(), 0);
		cue.data = QVariant(); // legacy name-only target still resolves
		executor.executeEventStart(cue);
		QCOMPARE(a.load(), 1);
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
		event.endAction = "stop";
		executor.executeEventEnd(event);
		QTRY_COMPARE(obs_source_media_get_state(clip.get()), OBS_MEDIA_STATE_STOPPED);
		event.endAction = "restart";
		executor.executeEventEnd(event);
		QTRY_COMPARE(obs_source_media_get_state(clip.get()), OBS_MEDIA_STATE_PLAYING);
		event.endAction = "keep";
		executor.executeEventEnd(event);
		QTRY_COMPARE(obs_source_media_get_state(clip.get()), OBS_MEDIA_STATE_PLAYING);
		event.endAction.clear();
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

	void testExplicitTimedEndingStates()
	{
		auto *filter = obs_source_create("test_filter", "Ending state filter", nullptr, nullptr);
		QVERIFY(filter);
		obs_source_filter_add(source_item, filter);
		auto cleanup = qScopeGuard([&] {
			obs_source_filter_remove(source_item, filter);
			obs_source_release(filter);
		});
		TimelineEvent cue;
		cue.type = TimelineEventType::FilterEnable;
		cue.sourceName = QString::fromUtf8(obs_source_get_name(source_item));
		cue.filterName = "Ending state filter";
		cue.hasDuration = true;
		cue.durationMs = 100;
		cue.endAction = "enable";
		ObsExecutor executor;
		executor.executeEventStart(cue);
		obs_source_set_enabled(filter, false);
		executor.executeEventEnd(cue);
		QVERIFY(obs_source_enabled(filter));
		cue.endAction = "keep";
		obs_source_set_enabled(filter, false);
		executor.executeEventEnd(cue);
		QVERIFY(!obs_source_enabled(filter));
		cue.endAction = "enable";
		TimelineModel model, restored;
		model.setHistoryEnabled(true);
		model.addEvent(cue);
		auto edited = cue;
		edited.endAction = "disable";
		QVERIFY(model.replaceEventById(cue.id, edited));
		QVERIFY(model.undo());
		QCOMPARE(model.getEvents().front().endAction, QString("enable"));
		restored.fromJson(model.toJson());
		QCOMPARE(restored.getEvents().front().endAction, cue.endAction);
		EventEditor editor(restored.getEvents().front());
		auto *ending = editor.findChild<QComboBox *>("eventEndActionCombo");
		QVERIFY(ending);
		QCOMPARE(ending->currentData().toString(), QString("enable"));
		ending->setCurrentIndex(ending->findData(QString("disable")));
		editor.getUpdatedEvent(edited);
		QCOMPARE(edited.endAction, QString("disable"));
		executor.executeEventEnd(edited);
		QVERIFY(!obs_source_enabled(filter));
		edited.endAction = "invalid";
		obs_source_set_enabled(filter, true);
		executor.executeEventEnd(edited);
		QVERIFY(obs_source_enabled(filter));
		edited.endAction.clear();
		executor.executeEventEnd(edited);
		QVERIFY(!obs_source_enabled(filter)); // historical inverse
	}

	void testFilterCueKeepsExactIdentityAcrossRenamesAndReplacement()
	{
		QVERIFY(source_item && scene1);
		auto *original = obs_source_create("test_filter", "Exact filter", nullptr, nullptr);
		auto *replacement = obs_source_create("test_filter", "Other filter", nullptr, nullptr);
		QVERIFY(original && replacement);
		obs_source_filter_add(source_item, original);
		obs_source_filter_add(source_item, replacement);
		auto cleanup = qScopeGuard([&] {
			if (obs_filter_get_parent(original))
				obs_source_filter_remove(source_item, original);
			obs_source_filter_remove(source_item, replacement);
			obs_source_release(original);
			obs_source_release(replacement);
		});
		TimelineEvent saved;
		saved.type = TimelineEventType::FilterDisable;
		saved.sourceName = "MySource";
		saved.filterName = "Exact filter";
		EventEditor editor(saved);
		TimelineEvent cue = saved;
		editor.getUpdatedEvent(cue);
		QVERIFY(cue.data.toString().startsWith("filter:v1:"));
		TimelineModel model, restored;
		model.addEvent(cue);
		restored.fromJson(model.toJson());
		cue = restored.getEvents().front();
		obs_source_set_name(original, "Renamed filter");
		obs_source_set_name(replacement, "Exact filter");
		obs_source_set_enabled(original, true);
		obs_source_set_enabled(replacement, true);
		ObsExecutor executor;
		executor.executeEventStart(cue);
		QVERIFY(!obs_source_enabled(original));
		QVERIFY(obs_source_enabled(replacement));
		EventEditor reopened(cue);
		TimelineEvent edited = cue;
		reopened.getUpdatedEvent(edited);
		QCOMPARE(edited.data, cue.data);
		QCOMPARE(edited.filterName, QString("Renamed filter"));
		obs_source_filter_remove(source_item, original);
		executor.executeEventStart(cue);
		QVERIFY(obs_source_enabled(replacement));
		EventEditor missing(cue);
		missing.getUpdatedEvent(edited);
		QCOMPARE(edited.data, cue.data);
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

	void testNestedTimelineEmitsIdentifiableVendorEvents()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto path = dir.filePath("nested.json");
		TimelineEvent child;
		child.type = TimelineEventType::SceneSwitch;
		child.sourceName = "Scene 1";
		child.label = "Nested cue";
		TimelineModel model;
		model.addEvent(child);
		QVERIFY(TimelineSerialization::saveToFile(
			path, QJsonObject{{"events", model.toJson()}, {"settings", QJsonObject{{"endTime", 1.0}}}}));
		ObsExecutor executor;
		executor.setObsWebSocketVendor(reinterpret_cast<void *>(0x12345678));
		TimelineEvent trigger;
		trigger.type = TimelineEventType::TriggerTimeline;
		trigger.data = path;
		executor.executeEventStart(trigger);
		QTRY_VERIFY_WITH_TIMEOUT(g_websocket_events.size() >= 2, 1000);
		bool nestedSeen = false;
		for (const auto &value : g_websocket_events) {
			const auto event = value.toObject();
			if (event["eventId"].toString() == child.id) {
				QCOMPARE(event["label"].toString(), child.label);
				QVERIFY(event["isNested"].toBool());
				nestedSeen = true;
			}
		}
		QVERIFY(nestedSeen);
	}

	void testNestedTimelineTreeHasDepthAndConcurrencyLimits()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		auto write = [&](int index, const QJsonArray &events) {
			return TimelineSerialization::saveToFile(
				dir.filePath(QString::number(index) + ".json"),
				QJsonObject{{"events", events}, {"settings", QJsonObject{{"endTime", 4.0}}}});
		};
		for (int i = 0; i < 10; ++i) {
			TimelineModel model;
			if (i < 9) {
				TimelineEvent trigger;
				trigger.type = TimelineEventType::TriggerTimeline;
				trigger.data = dir.filePath(QString::number(i + 1) + ".json");
				model.addEvent(trigger);
			}
			QVERIFY(write(i, model.toJson()));
		}
		{
			ObsExecutor executor;
			TimelineEvent trigger;
			trigger.type = TimelineEventType::TriggerTimeline;
			trigger.data = dir.filePath("0.json");
			executor.executeEventStart(trigger);
			QTRY_VERIFY_WITH_TIMEOUT(executor.findChildren<HeadlessTimeline *>().size() >= 8, 2000);
			QTest::qWait(150);
			QCOMPARE(executor.findChildren<HeadlessTimeline *>().size(), 8);
		}
		{
			ObsExecutor executor;
			TimelineEvent trigger;
			trigger.type = TimelineEventType::TriggerTimeline;
			for (int i = 10; i < 30; ++i) {
				QVERIFY(write(i, QJsonArray{}));
				trigger.data = dir.filePath(QString::number(i) + ".json");
				executor.executeEventStart(trigger);
			}
			QCOMPARE(executor.findChildren<HeadlessTimeline *>().size(), 16);
		}
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

	void testTimelineCenteredDefaultAndClockDivider()
	{
		QTemporaryDir dir;
		const QString path = dir.filePath("timeline.json");
		TimelineDock dock(nullptr, path);
		dock.resize(1400, 180);
		dock.show();
		QCoreApplication::processEvents();
		auto *center = dock.findChild<QCheckBox *>("timeline-center-controls");
		auto *line = dock.findChild<QFrame *>("timeline-clock-zoom-separator");
		auto *clock = dock.findChild<QWidget *>("OBSAudioMeter");
		auto *zoom = dock.findChild<QPushButton *>("timeline-zoom-out-btn");
		QVERIFY(center && center->isChecked());
		QVERIFY(line && clock && zoom);
		auto verifyDivider = [&] {
			QCoreApplication::processEvents();
			QVERIFY(line->isVisible());
			QVERIFY(clock->mapTo(&dock, QPoint(clock->width(), 0)).x() <= line->mapTo(&dock, QPoint()).x());
			QVERIFY(line->mapTo(&dock, QPoint(line->width(), 0)).x() <= zoom->mapTo(&dock, QPoint()).x());
		};
		verifyDivider();
		QCOMPARE(line->frameShape(), QFrame::VLine);
		QCOMPARE(line->frameShadow(), QFrame::Sunken);
		center->setChecked(false);
		verifyDivider();
		dock.save();
		dock.load();
		QVERIFY(!center->isChecked());
		center->setChecked(true);
		auto *compact = dock.findChild<QCheckBox *>("timeline-compact-mode");
		QVERIFY(compact);
		compact->setChecked(true);
		verifyDivider();
		TimelineDocument document;
		TimelineModel model;
		QVERIFY(TimelineSerialization::fromJsonDocument(QJsonDocument(QJsonObject{{"events", QJsonArray{}}}),
								model, document));
		QVERIFY(document.centerControls);
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
		auto *settings = dock.findChild<QPushButton *>("timeline-settings-btn");
		auto *zoomOut = dock.findChild<QPushButton *>("timeline-zoom-out-btn");
		auto *zoomIn = dock.findChild<QPushButton *>("timeline-zoom-in-btn");
		QVERIFY(model && view && play && settings && zoomOut && zoomIn);
		QVERIFY(!dock.findChild<QPushButton *>("timeline-undo-btn"));
		QVERIFY(!dock.findChild<QPushButton *>("timeline-redo-btn"));
		auto verifyJoinedZoom = [&] {
			QCOMPARE(zoomOut->parentWidget(), zoomIn->parentWidget());
			QCOMPARE(zoomOut->size(), zoomIn->size());
			QCOMPARE(zoomOut->geometry().right() + 1, zoomIn->geometry().left());
			QCOMPARE(zoomOut->y(), zoomIn->y());
			QVERIFY(zoomOut->isVisible() && zoomIn->isVisible());
		};
		verifyJoinedZoom();
		QCOMPARE(zoomOut->height(), 24);
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
		QCOMPARE(zoomOut->height(), 20);
		verifyJoinedZoom();
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
		verifyJoinedZoom();
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
		auto *undo = dock.findChild<QAction *>("timeline-undo-action");
		auto *redo = dock.findChild<QAction *>("timeline-redo-action");
		auto *menu = dock.findChild<QMenu *>("timeline-settings-menu");
		auto *settings = dock.findChild<QPushButton *>("timeline-settings-btn");
		QVERIFY(model && scheduler && view && undo && redo && menu && settings);
		QVERIFY(menu->actions().contains(undo) && menu->actions().contains(redo));
		QVERIFY(undo->text().contains(QKeySequence(QKeySequence::Undo).toString(QKeySequence::NativeText)));
		QVERIFY(redo->text().contains(QKeySequence(QKeySequence::Redo).toString(QKeySequence::NativeText)));
		QVERIFY(!undo->isEnabled() && !redo->isEnabled());
		auto clickHistory = [&](QAction *action) {
			QTest::mouseClick(settings, Qt::LeftButton);
			QTRY_VERIFY(menu->isVisible());
			QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(action).center());
			QTRY_VERIFY(!menu->isVisible());
		};
		TimelineEvent a, b;
		a.timeMs = 1500;
		a.type = TimelineEventType::MediaStart;
		a.sourceName = "Saved clip";
		b.timeMs = 2500;
		model->addEvents({a, b});
		QTRY_VERIFY(undo->isEnabled());
		clickHistory(undo);
		QVERIFY(model->getEvents().empty());
		QVERIFY(redo->isEnabled());
		clickHistory(redo);
		QCOMPARE(model->getEvents().size(), size_t(2));
		// Real widget shortcut, not a direct call to the model.
		dock.activateWindow();
		QVERIFY(QTest::qWaitForWindowActive(&dock));
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
		undo->trigger();
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
		undo->trigger();
		QCOMPARE(model->getEvents().size(), size_t(2));
		QCoreApplication::processEvents();
		scheduler->seek(0);
		for (auto *graphics : view->scene()->items())
			if (auto *item = dynamic_cast<TimelineEventItem *>(graphics))
				item->setSelected(true);
		view->setFocus();
		QTest::keyClick(view, Qt::Key_Delete);
		QTRY_VERIFY_WITH_TIMEOUT(model->getEvents().empty(), 1500);
		undo->trigger();
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
		undo->trigger();
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
		undo->trigger();
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

	void testMutedCuesSkipAllPlaybackEdgesAndRampWrites()
	{
		TimelineEvent span, hold, ramp, step, audible;
		span.hasDuration = true;
		span.durationMs = 30;
		hold.type = TimelineEventType::FreezeTime;
		hold.timeMs = 5;
		hold.data = 500.0;
		MacroParameterBinding binding{"source", {}, "gain", MacroParameterType::Float};
		ramp.type = TimelineEventType::SetParameterValue;
		ramp.hasDuration = true;
		ramp.durationMs = 50;
		ramp.data = MacroParameter::encodeRamp({{binding, 0.0}, 1.0, "linear"});
		step.type = TimelineEventType::SetParameterValue;
		step.timeMs = 10;
		step.data = MacroParameter::encodeValue({binding, 0.5});
		audible.timeMs = 20;
		TimelineModel initial, model;
		initial.addEvents({span, hold, ramp, step, audible});
		auto json = initial.toJson();
		for (int i = 0; i < json.size(); ++i) {
			auto obj = json[i].toObject();
			if (obj["id"].toString() != audible.id)
				obj["muted"] = true;
			json[i] = obj;
		}
		model.fromJson(json);
		TimelineScheduler scheduler(&model);
		QStringList starts, ends, samples;
		connect(&scheduler, &TimelineScheduler::eventStarted, this,
			[&](const TimelineEvent &e) { starts << e.id; });
		connect(&scheduler, &TimelineScheduler::eventEnded, this,
			[&](const TimelineEvent &e) { ends << e.id; });
		connect(&scheduler, &TimelineScheduler::parameterProgress, this,
			[&](const TimelineEvent &e, double) { samples << e.id; });
		scheduler.setEndTime(60);
		scheduler.start();
		QTRY_VERIFY_WITH_TIMEOUT(!scheduler.isPlaying(), 250);
		QCOMPARE(starts, QStringList{audible.id});
		QVERIFY(ends.isEmpty());
		QVERIFY(samples.isEmpty());
		scheduler.seek(15);
		scheduler.start();
		QTRY_VERIFY_WITH_TIMEOUT(!scheduler.isPlaying(), 250);
		QCOMPARE(starts, (QStringList{audible.id, audible.id}));
		QVERIFY(ends.isEmpty());
		QVERIFY(samples.isEmpty());
		const auto roundTrip = model.toJson();
		for (const auto &v : roundTrip)
			if (v.toObject()["id"].toString() != audible.id)
				QVERIFY(v.toObject()["muted"].toBool());
	}

	void testMutedSpanPreservesAnAlreadyStartedEndingButNeverCreatesOrphanEnd()
	{
		TimelineEvent span;
		span.type = TimelineEventType::FilterEnable;
		span.hasDuration = true;
		span.durationMs = 1000;
		TimelineModel model;
		model.setHistoryEnabled(true);
		model.addEvent(span);
		TimelineScheduler scheduler(&model);
		QStringList starts, ends;
		connect(&scheduler, &TimelineScheduler::eventStarted, this,
			[&](const TimelineEvent &e) { starts << e.id; });
		connect(&scheduler, &TimelineScheduler::eventEnded, this,
			[&](const TimelineEvent &e) { ends << e.id; });
		scheduler.start();
		QVERIFY(QMetaObject::invokeMethod(&scheduler, "onTimerTick", Qt::DirectConnection));
		scheduler.pause();
		QCOMPARE(starts, QStringList{span.id});
		auto muted = span;
		muted.muted = true;
		QVERIFY(model.replaceEventById(span.id, muted));
		scheduler.setEndTime(1000);
		scheduler.start();
		QTRY_VERIFY_WITH_TIMEOUT(!scheduler.isPlaying(), 1600);
		QCOMPARE(ends, QStringList{span.id});
		scheduler.restart();
		scheduler.start();
		QVERIFY(QMetaObject::invokeMethod(&scheduler, "onTimerTick", Qt::DirectConnection));
		QTest::qSleep(5);
		scheduler.pause();
		QVERIFY(model.replaceEventById(span.id, span));
		scheduler.start();
		QTRY_VERIFY_WITH_TIMEOUT(!scheduler.isPlaying(), 1600);
		QCOMPARE(starts, QStringList{span.id});
		QCOMPARE(ends, QStringList{span.id});
	}

	void testTimelineCueMenuMuteAndManualExecution()
	{
		auto *filter = obs_source_create("test_filter", "Manual cue filter", nullptr, nullptr);
		QVERIFY(filter && source_item);
		obs_source_filter_add(source_item, filter);
		auto cleanup = qScopeGuard([&] {
			obs_source_filter_remove(source_item, filter);
			obs_source_remove(filter);
			obs_source_release(filter);
		});
		obs_source_set_enabled(filter, false);
		TimelineEvent cue;
		cue.type = TimelineEventType::FilterEnable;
		cue.sourceName = QString::fromUtf8(obs_source_get_name(source_item));
		cue.filterName = "Manual cue filter";
		cue.timeMs = 2000;
		cue.hasDuration = true;
		cue.durationMs = 500;
		TimelineModel initial;
		initial.addEvent(cue);
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto path = dir.filePath("timeline.json");
		QVERIFY(TimelineSerialization::saveToFile(path, QJsonObject{{"events", initial.toJson()}}));
		TimelineDock dock(nullptr, path);
		dock.resize(800, 240);
		dock.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dock));
		auto *model = dock.findChild<TimelineModel *>();
		auto *scheduler = dock.findChild<TimelineScheduler *>();
		auto *view = dock.findChild<QGraphicsView *>();
		QVERIFY(model && scheduler && view);
		TimelineEventItem *marker = nullptr;
		for (auto *graphics : view->scene()->items())
			if (auto *item = dynamic_cast<TimelineEventItem *>(graphics))
				marker = item;
		QVERIFY(marker);
		auto choose = [&](const QString &name) {
			bool found = false;
			QTimer::singleShot(0, &dock, [&] {
				auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
				if (!menu)
					return;
				for (auto *action : menu->actions())
					if (action->objectName() == name && action->isEnabled()) {
						found = true;
						QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
								  menu->actionGeometry(action).center());
						return;
					}
				menu->close();
			});
			const auto center = marker->mapToScene(marker->rect().center());
			const auto pos = view->mapFromScene(center);
			QContextMenuEvent event(QContextMenuEvent::Mouse, pos, view->viewport()->mapToGlobal(pos));
			QCoreApplication::sendEvent(view->viewport(), &event);
			QCoreApplication::processEvents();
			return found;
		};
		QVERIFY(choose("timeline-cue-mute-action"));
		QVERIFY(model->toJson().first().toObject()["muted"].toBool());
		QVERIFY(marker->opacity() < 1.0);
		QVERIFY(model->undo());
		QCoreApplication::processEvents();
		QVERIFY(!model->toJson().first().toObject()["muted"].toBool());
		QCOMPARE(marker->opacity(), 1.0);
		QVERIFY(choose("timeline-cue-run-start-action"));
		QVERIFY(obs_source_enabled(filter));
		QVERIFY(choose("timeline-cue-run-end-action"));
		QVERIFY(!obs_source_enabled(filter));
		QCOMPARE(scheduler->getCurrentTime(), 0.0);
		QVERIFY(!model->canUndo());
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

	void testFreezeTimeDefersLaterCuesEvenOnLateTick()
	{
		TimelineModel model;
		TimelineEvent span, hold, same, later;
		span.hasDuration = true;
		span.durationMs = 60;
		hold.type = TimelineEventType::FreezeTime;
		hold.timeMs = same.timeMs = 20;
		hold.data = 100.0; // wall-clock milliseconds, not a timeline span
		later.timeMs = 80;
		model.addEvents({span, hold, same, later});
		TimelineScheduler scheduler(&model);
		scheduler.setEndTime(80);
		QStringList trace;
		connect(&scheduler, &TimelineScheduler::eventStarted, &scheduler,
			[&](const TimelineEvent &e) { trace << e.id; });
		connect(&scheduler, &TimelineScheduler::eventEnded, &scheduler,
			[&](const TimelineEvent &e) { trace << e.id + ":end"; });
		QSignalSpy finished(&scheduler, &TimelineScheduler::finished);
		QSignalSpy ticks(&scheduler, &TimelineScheduler::tick);
		scheduler.start();
		QTest::qSleep(95); // even an overdue end marker must not bypass a freeze
		QVERIFY(QMetaObject::invokeMethod(&scheduler, "onTimerTick", Qt::DirectConnection));
		QCOMPARE(trace, (QStringList{span.id, hold.id, same.id}));
		QVERIFY(scheduler.isPlaying());
		QCOMPARE(scheduler.getCurrentTime(), 20.0);
		QCOMPARE(ticks.last().at(0).toDouble(), 20.0);
		QCOMPARE(finished.count(), 0);
		QTest::qWait(30);
		QCOMPARE(scheduler.getCurrentTime(), 20.0);
		QCOMPARE(trace.size(), 3);
		scheduler.pause();
		QVERIFY(!scheduler.isPlaying());
		QTest::qWait(120);
		QCOMPARE(scheduler.getCurrentTime(), 20.0);
		scheduler.start();
		QTest::qWait(15);
		QCOMPARE(scheduler.getCurrentTime(), 20.0);
		QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1500);
		QCOMPARE(trace, (QStringList{span.id, hold.id, same.id, span.id + ":end", later.id}));
		QCOMPARE(scheduler.getCurrentTime(), 80.0);
	}

	void testFreezeTimeSeekRestartAndLoopBoundary()
	{
		TimelineModel model;
		TimelineEvent hold;
		hold.type = TimelineEventType::FreezeTime;
		hold.timeMs = 15;
		hold.data = 80.0;
		model.addEvent(hold);
		TimelineScheduler scheduler(&model);
		scheduler.setEndTime(15);
		scheduler.setLooping(true);
		QSignalSpy starts(&scheduler, &TimelineScheduler::eventStarted);
		QSignalSpy loops(&scheduler, &TimelineScheduler::looped);
		scheduler.start();
		QTest::qSleep(25);
		QVERIFY(QMetaObject::invokeMethod(&scheduler, "onTimerTick", Qt::DirectConnection));
		QCOMPARE(starts.count(), 1);
		QCOMPARE(loops.count(), 0);
		QCOMPARE(scheduler.getCurrentTime(), 15.0);
		QTRY_COMPARE_WITH_TIMEOUT(loops.count(), 1, 1000);
		QTRY_COMPARE_WITH_TIMEOUT(starts.count(), 2, 1000);
		QCOMPARE(loops.count(), 1);
		scheduler.restart();
		QCOMPARE(scheduler.getCurrentTime(), 0.0);
		QVERIFY(!scheduler.isPlaying());
		scheduler.setLooping(false);
		scheduler.setEndTime(100);
		scheduler.start();
		QTRY_COMPARE_WITH_TIMEOUT(starts.count(), 3, 1000);
		scheduler.seek(50);
		QVERIFY(scheduler.getCurrentTime() >= 50);
		QTRY_VERIFY_WITH_TIMEOUT(!scheduler.isPlaying(), 1000);
		QCOMPARE(starts.count(), 3);
	}

	void testFreezeTimeComposesAndDoesNotExtendVisualDuration()
	{
		TimelineModel model;
		TimelineEvent first, second, third, after, invalid;
		first.type = second.type = third.type = invalid.type = TimelineEventType::FreezeTime;
		first.data = second.data = 60.0;
		third.timeMs = 20;
		third.data = 80.0;
		invalid.timeMs = 1;
		invalid.data = -100.0;
		after.timeMs = 30;
		model.addEvents({first, second, invalid, third, after});
		TimelineScheduler scheduler(&model);
		scheduler.setEndTime(30);
		QStringList trace;
		connect(&scheduler, &TimelineScheduler::eventStarted, &scheduler,
			[&](const TimelineEvent &e) { trace << e.id; });
		QElapsedTimer elapsed;
		elapsed.start();
		scheduler.start();
		QVERIFY(QMetaObject::invokeMethod(&scheduler, "onTimerTick", Qt::DirectConnection));
		QCOMPARE(trace, (QStringList{first.id, second.id}));
		QVERIFY(scheduler.isFrozen());
		QCOMPARE(scheduler.getCurrentTime(), 0.0);
		QVERIFY(scheduler.freezeRemainingMs() > 60);
		QTRY_VERIFY_WITH_TIMEOUT(trace.contains(third.id), 1500);
		QVERIFY(scheduler.isPlaying());
		QCOMPARE(scheduler.getCurrentTime(), 20.0);
		QVERIFY(!trace.contains(after.id));
		QTRY_VERIFY_WITH_TIMEOUT(!scheduler.isPlaying(), 1500);
		QVERIFY(elapsed.elapsed() >= 230);
		QCOMPARE(trace, (QStringList{first.id, second.id, invalid.id, third.id, after.id}));

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath("hold.json");
		TimelineModel nestedModel;
		first.data = 160.0;
		nestedModel.addEvent(first);
		after.timeMs = 20;
		nestedModel.addEvent(after);
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		QVERIFY(file.write(QJsonDocument(nestedModel.toJson()).toJson()) > 0);
		file.close();
		HeadlessTimeline nested;
		QVERIFY(nested.loadFromFile(path));
		auto *nestedScheduler = nested.findChild<TimelineScheduler *>();
		QVERIFY(nestedScheduler);
		QCOMPARE(nestedScheduler->getEndTime(), 120.0);
		QSignalSpy finished(&nested, &HeadlessTimeline::finished);
		nested.start();
		QTRY_VERIFY_WITH_TIMEOUT(nestedScheduler->isFrozen(), 1000);
		QTest::qWait(40);
		QCOMPARE(finished.count(), 0);
		QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1500);
	}

	void testFreezeTimeEditorAndRoundTrip()
	{
		EventEditor editor(1000.0);
		auto *types = editor.findChild<QComboBox *>("eventTypeCombo");
		auto *duration = editor.findChild<QDoubleSpinBox *>("freezeDurationSpin");
		QVERIFY(types && duration);
		const int index = types->findData(static_cast<int>(TimelineEventType::FreezeTime));
		QVERIFY(index >= 0);
		types->setCurrentIndex(index);
		duration->setValue(2.75);
		TimelineEvent hold;
		editor.getUpdatedEvent(hold);
		QCOMPARE(hold.type, TimelineEventType::FreezeTime);
		QCOMPARE(hold.data.toDouble(), 2750.0);
		QVERIFY(!hold.hasDuration);
		TimelineModel model, restored;
		model.addEvent(hold);
		restored.fromJson(model.toJson());
		QCOMPARE(restored.getEvents().size(), size_t(1));
		QCOMPARE(restored.getEvents().front().data.toDouble(), 2750.0);
		EventEditor reopened(restored.getEvents().front());
		QCOMPARE(reopened.findChild<QDoubleSpinBox *>("freezeDurationSpin")->value(), 2.75);
		EventEditor macroEditor(0.0, nullptr, false);
		QCOMPARE(macroEditor.findChild<QComboBox *>("eventTypeCombo")
				 ->findData(static_cast<int>(TimelineEventType::FreezeTime)),
			 -1);
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		TimelineDock dock(nullptr, dir.filePath("timeline.json"));
		dock.resize(900, 180);
		dock.show();
		auto *dockModel = dock.findChild<TimelineModel *>();
		auto *view = dock.findChild<QGraphicsView *>();
		QVERIFY(dockModel && view);
		hold.data = 3600000.0;
		dockModel->addEvent(hold);
		QCoreApplication::processEvents();
		auto findMarker = [&]() -> TimelineEventItem * {
			for (auto *graphics : view->scene()->items())
				if (auto *marker = dynamic_cast<TimelineEventItem *>(graphics);
				    marker && marker->getEventId() == hold.id)
					return marker;
			return nullptr;
		};
		QTRY_VERIFY(findMarker());
		QVERIFY(findMarker()->toolTip().contains("3600"));
		QVERIFY(findMarker()->sceneBoundingRect().width() < 180);
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

	void testSchedulerRefreshesEndIndexAfterBlockedImport()
	{
		TimelineModel model;
		TimelineScheduler scheduler(&model);
		TimelineEvent span;
		span.timeMs = 0;
		span.hasDuration = true;
		span.durationMs = 10;
		// The real dock blocks model signals during document imports.
		model.blockSignals(true);
		model.addEvent(span);
		scheduler.seek(0);
		model.blockSignals(false);
		scheduler.setEndTime(10);
		QSignalSpy ends(&scheduler, &TimelineScheduler::eventEnded);
		scheduler.start();
		QTest::qSleep(20);
		QVERIFY(QMetaObject::invokeMethod(&scheduler, "onTimerTick", Qt::DirectConnection));
		QCOMPARE(ends.count(), 1);
	}

	void testDockPreservesMalformedStartupConfiguration()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto path = dir.filePath("timeline.json");
		const QByteArray malformed = "{ not valid json";
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		QCOMPARE(file.write(malformed), qint64(malformed.size()));
		file.close();
		{
			TimelineDock dock(nullptr, path);
			dock.save();
		}
		QVERIFY(file.open(QIODevice::ReadOnly));
		QCOMPARE(file.readAll(), malformed);
	}

	void testHeadlessLoadUsesValidatedDocumentAndResetsSettings()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto valid = dir.filePath("valid.json");
		const auto malformed = dir.filePath("malformed.json");
		const auto legacy = dir.filePath("legacy.json");
		QString error;
		QVERIFY(TimelineSerialization::saveToFile(
			valid,
			QJsonObject{{"events", QJsonArray{}},
				    {"settings", QJsonObject{{"loop", true}, {"endTime", 2.0}}}},
			&error));
		QVERIFY(TimelineSerialization::saveToFile(malformed, QJsonObject{{"settings", QJsonObject{}}}, &error));
		QFile file(legacy);
		QVERIFY(file.open(QIODevice::WriteOnly));
		QCOMPARE(file.write("[]"), qint64(2));
		file.close();
		HeadlessTimeline nested;
		QVERIFY(nested.loadFromFile(valid));
		auto *scheduler = nested.findChild<TimelineScheduler *>();
		QVERIFY(scheduler);
		QVERIFY(scheduler->isLooping());
		QCOMPARE(scheduler->getEndTime(), 2000.0);
		QVERIFY(!nested.loadFromFile(malformed));
		QCOMPARE(nested.getFilePath(), valid);
		QVERIFY(scheduler->isLooping());
		QCOMPARE(scheduler->getEndTime(), 2000.0);
		QVERIFY(nested.loadFromFile(legacy));
		QVERIFY(!scheduler->isLooping());
		QCOMPARE(scheduler->getEndTime(), 100.0);
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
		scheduler.start();
		QVERIFY(QMetaObject::invokeMethod(&scheduler, "onTimerTick", Qt::DirectConnection));
		QCOMPARE(finishedSpy.count(), 1);

		scheduler.setLooping(true);
		scheduler.seek(1500.0);
		scheduler.start();
		tickSpy.clear();
		QVERIFY(QMetaObject::invokeMethod(&scheduler, "onTimerTick", Qt::DirectConnection));
		QCOMPARE(loopedSpy.count(), 1);
		QCOMPARE(tickSpy.count(), 1);
		QCOMPARE(tickSpy.takeFirst().at(0).toDouble(), 0.0);
		QVERIFY(scheduler.getCurrentTime() < 100.0);
		scheduler.stop();
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

	void testParameterRampClockAndSupersession()
	{
		auto *source = obs_source_create("timeline_parameter_probe", "Ramp target", nullptr, nullptr);
		QVERIFY(source);
		auto cleanup = qScopeGuard([&] {
			obs_source_remove(source);
			obs_source_release(source);
		});
		MacroParameterBinding binding{QString::fromUtf8(obs_source_get_uuid(source)),
					      {},
					      "gain",
					      MacroParameterType::Float};
		TimelineEvent ramp;
		ramp.type = TimelineEventType::SetParameterValue;
		ramp.hasDuration = true;
		ramp.durationMs = 200;
		ramp.data = MacroParameter::encodeRamp({{binding, -1.0}, 1.0, "linear"});
		QVERIFY(MacroParameter::decodeRamp(ramp.data.toString()));
		QVERIFY(!MacroParameter::decodeValue(
			ramp.data.toString())); // older decoders must not apply a ramp as a step
		TimelineEvent freeze;
		freeze.type = TimelineEventType::FreezeTime;
		freeze.timeMs = 100;
		freeze.data = 150.0;
		TimelineEvent step;
		step.type = TimelineEventType::SetParameterValue;
		step.timeMs = 150;
		step.data = MacroParameter::encodeValue({binding, -0.5});
		TimelineModel model, restored;
		model.addEvents({ramp, freeze, step});
		restored.fromJson(model.toJson());
		QVERIFY(restored.getEvents().front().hasDuration);
		QCOMPARE(restored.getEvents().front().durationMs, 200.0);
		ObsExecutor executor;
		TimelineScheduler scheduler(&restored);
		connect(&scheduler, &TimelineScheduler::eventStarted, &executor, &ObsExecutor::executeEventStart);
		connect(&scheduler, &TimelineScheduler::eventEnded, &executor, &ObsExecutor::executeEventEnd);
		connect(&scheduler, &TimelineScheduler::parameterProgress, &executor,
			&ObsExecutor::executeParameterProgress);
		scheduler.setEndTime(240);
		scheduler.start();
		QTest::qSleep(120);
		QVERIFY(QMetaObject::invokeMethod(&scheduler, "onTimerTick", Qt::DirectConnection));
		QVERIFY(scheduler.isFrozen());
		QCOMPARE(MacroParameter::read(binding).value(), 0.0);
		QTest::qWait(40);
		QCOMPARE(MacroParameter::read(binding).value(), 0.0);
		scheduler.pause();
		QTest::qWait(170);
		QCOMPARE(MacroParameter::read(binding).value(), 0.0);
		scheduler.start();
		QTRY_VERIFY_WITH_TIMEOUT(!scheduler.isPlaying(), 1500);
		QCOMPARE(MacroParameter::read(binding).value(), -0.5);
		scheduler.seek(50);
		QCOMPARE(MacroParameter::read(binding).value(), -0.5);
		scheduler.start();
		QTest::qSleep(70);
		QVERIFY(QMetaObject::invokeMethod(&scheduler, "onTimerTick", Qt::DirectConnection));
		QCOMPARE(MacroParameter::read(binding).value(), 0.0);
		scheduler.restart();
		QVERIFY(!scheduler.isPlaying());

		ramp.data = MacroParameter::encodeRamp({{binding, -1.0}, 1.0, "ease-in"});
		executor.executeParameterProgress(ramp, 0.5);
		QCOMPARE(MacroParameter::read(binding).value(), -0.5);
		executor.executeParameterProgress(ramp, 1.0);
		QCOMPARE(MacroParameter::read(binding).value(), 1.0);
		EventEditor editor(ramp);
		auto *end = editor.findChild<QDoubleSpinBox *>("parameterRampEnd");
		auto *curve = editor.findChild<QComboBox *>("parameterRampEasing");
		QVERIFY(end && curve);
		QCOMPARE(end->value(), 1.0);
		QCOMPARE(curve->currentData().toString(), QString("ease-in"));
		end->setValue(0.5);
		TimelineEvent edited = ramp;
		editor.getUpdatedEvent(edited);
		auto decoded = MacroParameter::decodeRamp(edited.data.toString());
		QVERIFY(decoded && edited.hasDuration);
		QCOMPARE(decoded->end, 0.5);
		EventEditor macroEditor(ramp, nullptr, false);
		QVERIFY(macroEditor.findChild<QDoubleSpinBox *>("parameterRampEnd")->isHidden() ||
			macroEditor.findChild<QDoubleSpinBox *>("parameterRampEnd")->parentWidget()->isHidden());
		// Completion lands exactly at the endpoint even after a delayed tick.
		TimelineModel finalModel;
		ramp.timeMs = 0;
		ramp.durationMs = 40;
		finalModel.addEvent(ramp);
		TimelineScheduler finalScheduler(&finalModel);
		connect(&finalScheduler, &TimelineScheduler::eventStarted, &executor, &ObsExecutor::executeEventStart);
		connect(&finalScheduler, &TimelineScheduler::parameterProgress, &executor,
			&ObsExecutor::executeParameterProgress);
		finalScheduler.setEndTime(40);
		finalScheduler.start();
		QTest::qSleep(60);
		QVERIFY(QMetaObject::invokeMethod(&finalScheduler, "onTimerTick", Qt::DirectConnection));
		QVERIFY(!finalScheduler.isPlaying());
		QCOMPARE(MacroParameter::read(binding).value(), 1.0);
		MacroParameter::Ramp curveTest{{binding, 0.0}, 1.0, "ease-out"};
		QCOMPARE(MacroParameter::rampValue(curveTest, 0.5), 0.75);
		curveTest.easing = "ease-in-out";
		QCOMPARE(MacroParameter::rampValue(curveTest, 0.25), 0.125);
		QCOMPARE(MacroParameter::rampValue(curveTest, 0.75), 0.875);
		curveTest.easing = "bad";
		QVERIFY(!MacroParameter::decodeRamp(MacroParameter::encodeRamp(curveTest)));
		curveTest.easing = "linear";
		curveTest.start.binding.type = MacroParameterType::Boolean;
		QVERIFY(!MacroParameter::decodeRamp(MacroParameter::encodeRamp(curveTest)));

		obs_source_remove(source);
		executor.executeParameterProgress(ramp, 0.5); // safe on missing target
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
		QCOMPARE(payload->value.toDouble(), 0.65);
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
		std::optional<QVariant> value = 1;
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
		QCOMPARE(value->toDouble(), 0.0);
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

	void testMacroXYEditorSelectsIndependentNumericAxes()
	{
		auto *source = obs_source_create("timeline_parameter_probe", "XY editor target", nullptr, nullptr);
		auto *other = obs_source_create("timeline_parameter_probe", "XY other target", nullptr, nullptr);
		QVERIFY(source && other);
		auto cleanup = qScopeGuard([&] {
			obs_source_remove(source);
			obs_source_release(source);
			obs_source_remove(other);
			obs_source_release(other);
		});
		QWidget host;
		MacroButton initial;
		initial.kind = MacroControlKind::XY;
		bool configured = false;
		QTimer::singleShot(0, &host, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog)
				return;
			auto *x = dialog->findChild<QPushButton *>("macro-xy-bind-x");
			auto *y = dialog->findChild<QPushButton *>("macro-xy-bind-y");
			auto *label = dialog->findChild<QLineEdit *>("macro-xy-label");
			auto *buttons = dialog->findChild<QDialogButtonBox *>();
			if (!x || !y || !label || !buttons) {
				dialog->reject();
				return;
			}
			auto choose = [&](QPushButton *button, QString targetName, QString propertyName) {
				bool selected = false;
				QTimer::singleShot(0, dialog, [&] {
					auto *picker = qobject_cast<QDialog *>(QApplication::activeModalWidget());
					if (!picker || picker == dialog)
						return;
					auto *target = picker->findChild<QComboBox *>("macro-parameter-target");
					auto *property = picker->findChild<QComboBox *>("macro-parameter-property");
					auto *ok = picker->findChild<QDialogButtonBox *>();
					if (target && property && ok) {
						target->setCurrentIndex(target->findText(targetName));
						property->setCurrentIndex(-1);
						bool numericHint = false;
						for (auto *hint : picker->findChildren<QLabel *>())
							if (hint->text().contains(
								    "Choose an exposed integer or floating-point property."))
								numericHint = true;
						property->setCurrentIndex(property->findText(propertyName));
						selected = numericHint && property->findText("Enabled") < 0 &&
							   property->findText("Text") < 0 &&
							   property->findText("Caption") < 0 &&
							   ok->button(QDialogButtonBox::Ok)->isEnabled();
					}
					if (selected)
						picker->accept();
					else
						picker->reject();
				});
				button->click();
				return selected;
			};
			auto *ok = buttons->button(QDialogButtonBox::Ok);
			configured = !ok->isEnabled() && choose(x, "XY editor target", "Controls / Gain") &&
				     choose(y, "XY editor target", "Controls / Gain") && !ok->isEnabled() &&
				     choose(y, "XY other target", "Controls / Count") && ok->isEnabled();
			label->setText("Motion");
			if (configured)
				dialog->accept();
			else
				dialog->reject();
		});
		const auto selected = editMacroParameter(&host, initial);
		QVERIFY(configured && selected);
		QCOMPARE(selected->label, QString("Motion"));
		QCOMPARE(selected->binding.sourceUuid, QString::fromUtf8(obs_source_get_uuid(source)));
		QCOMPARE(selected->bindingY.sourceUuid, QString::fromUtf8(obs_source_get_uuid(other)));
		QCOMPARE(MacroParameter::read(selected->binding).value(), 0.25);
		QCOMPARE(MacroParameter::read(selected->bindingY).value(), 4.0);
		QVERIFY(MacroParameter::writePair(selected->binding, 0.5, selected->bindingY, 8));
		QCOMPARE(MacroParameter::read(selected->binding).value(), 0.5);
		QCOMPARE(MacroParameter::read(selected->bindingY).value(), 8.0);
	}

	void testMacroXYPadPairBindingsAndGeometry()
	{
		auto *source = obs_source_create("timeline_parameter_probe", "XY target", nullptr, nullptr);
		QVERIFY(source);
		auto cleanup = qScopeGuard([&] {
			obs_source_remove(source);
			obs_source_release(source);
		});
		MacroButton xy;
		xy.kind = MacroControlKind::XY;
		xy.label = "Position";
		xy.binding = {QString::fromUtf8(obs_source_get_uuid(source)), {}, "gain", MacroParameterType::Float};
		xy.bindingY = {xy.binding.sourceUuid, {}, "count", MacroParameterType::Integer};
		MacroButtonModel model, restored;
		model.addButton(xy);
		QVERIFY(MacroSerialization::fromJsonDocument(QJsonDocument(MacroSerialization::toJsonObject(model)),
							     restored));
		QCOMPARE(restored.buttons().first().kind, MacroControlKind::XY);
		QCOMPARE(restored.buttons().first().bindingY.key, QString("count"));
		auto duplicate = xy;
		duplicate.bindingY = duplicate.binding;
		MacroButtonModel invalid;
		invalid.addButton(duplicate);
		QVERIFY(!MacroSerialization::fromJsonDocument(QJsonDocument(MacroSerialization::toJsonObject(invalid)),
							      restored));
		QCOMPARE(restored.buttons().size(), 1);
		QTemporaryDir dir;
		MacroStripDock dock(nullptr, dir.filePath("macro.json"));
		dock.buttonModel()->addButton(xy);
		MacroButton action;
		action.label = "Next";
		dock.buttonModel()->addButton(action);
		dock.resize(600, dock.minimumHeight());
		dock.show();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QCoreApplication::processEvents();
		auto *pad = dock.findChild<QWidget *>("macro-parameter-xy");
		auto *caption = dock.findChild<QLabel *>("macro-parameter-caption");
		auto *next = dock.findChild<MacroButtonWidget *>();
		QVERIFY(pad && caption && next && pad->isEnabled());
		QCOMPARE(pad->size(), QSize(40, 40));
		QVERIFY(pad->parentWidget()->rect().contains(pad->geometry()));
		QCOMPARE(MacroParameter::read(xy.binding).value(), 0.25);
		QCOMPARE(MacroParameter::read(xy.bindingY).value(), 4.0);
		const QRect initial(next->mapTo(&dock, QPoint()), next->size());
		QTest::mousePress(pad, Qt::LeftButton, Qt::NoModifier, QPoint(pad->width() - 5, 4));
		QTest::mouseRelease(pad, Qt::LeftButton, Qt::NoModifier, QPoint(pad->width() - 5, 4));
		QTRY_COMPARE(MacroParameter::read(xy.binding).value(), 1.0);
		QTRY_COMPARE(MacroParameter::read(xy.bindingY).value(), 10.0);
		QVERIFY(MacroParameter::writePair(xy.binding, -1.0, xy.bindingY, 2.0));
		QTRY_VERIFY(caption->toolTip().contains("-1") && caption->toolTip().contains("2"));
		QCOMPARE(caption->text(), QString("Position"));
		QCOMPARE(QRect(next->mapTo(&dock, QPoint()), next->size()), initial);
		dock.activateWindow();
		QVERIFY(QTest::qWaitForWindowActive(&dock));
		pad->setFocus();
		QTRY_VERIFY(pad->hasFocus());
		QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
		QCoreApplication::sendEvent(pad, &right);
		QTRY_VERIFY(qAbs(MacroParameter::read(xy.binding).value() + 0.95) < 1e-9);
		QCOMPARE(MacroParameter::read(xy.bindingY).value(), 2.0);
		auto missing = xy.bindingY;
		missing.key = "missing";
		QVERIFY(!MacroParameter::writePair(xy.binding, 1.0, missing, 10.0));
		QVERIFY(qAbs(MacroParameter::read(xy.binding).value() + 0.95) < 1e-9);
		QVERIFY(MacroParameter::write({xy.binding.sourceUuid, {}, "enabled", MacroParameterType::Boolean}, 0));
		QTRY_VERIFY_WITH_TIMEOUT(!pad->isEnabled(), 3000);
		QCOMPARE(QRect(next->mapTo(&dock, QPoint()), next->size()), initial);
	}

	void testMacroArcKnobAndStableCaptionGeometry()
	{
		auto *source = obs_source_create("timeline_parameter_probe", "Stable caption target", nullptr, nullptr);
		QVERIFY(source);
		auto cleanup = qScopeGuard([&] {
			obs_source_remove(source);
			obs_source_release(source);
		});
		MacroButton knob, slider, toggle, action;
		knob.kind = MacroControlKind::Knob;
		knob.label = "Gain";
		knob.binding = {QString::fromUtf8(obs_source_get_uuid(source)), {}, "gain", MacroParameterType::Float};
		slider = knob;
		slider.id = QUuid::createUuid().toString();
		slider.kind = MacroControlKind::Slider;
		toggle.kind = MacroControlKind::Toggle;
		toggle.binding = {knob.binding.sourceUuid, {}, "enabled", MacroParameterType::Boolean};
		toggle.label = "Enabled";
		action.label = "Next";
		QTemporaryDir dir;
		MacroStripDock dock(nullptr, dir.filePath("macro.json"));
		for (const auto &b : {knob, slider, toggle, action})
			dock.buttonModel()->addButton(b);
		dock.resize(1000, dock.minimumHeight());
		dock.show();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QCoreApplication::processEvents();
		auto *dial = dock.findChild<QDial *>("macro-parameter-knob");
		auto *fader = dock.findChild<QSlider *>("macro-parameter-slider");
		auto *check = dock.findChild<QCheckBox *>("macro-parameter-toggle");
		auto *next = dock.findChild<MacroButtonWidget *>();
		QVERIFY(dial && fader && check && next);
		auto geometry = [&] {
			QList<QRect> rects;
			for (auto *w : {static_cast<QWidget *>(dial), static_cast<QWidget *>(fader),
					static_cast<QWidget *>(check), static_cast<QWidget *>(next)})
				rects.append(QRect(w->mapTo(&dock, QPoint()), w->size()));
			for (auto *w : dock.findChildren<QWidget *>("macro-parameter-control"))
				rects.append(QRect(w->mapTo(&dock, QPoint()), w->size()));
			return rects;
		};
		const auto initial = geometry();
		QVERIFY(MacroParameter::write(knob.binding, -0.95));
		QTRY_COMPARE(dial->value(), 1);
		QTRY_COMPARE(fader->value(), 1);
		QCOMPARE(geometry(), initial);
		QVERIFY(MacroParameter::write(knob.binding, 1.0));
		QTRY_COMPARE(dial->value(), 40);
		QTRY_COMPARE(fader->value(), 40);
		QCOMPARE(geometry(), initial);

		// Render the actual control under a distinctive theme accent.
		QPalette palette = dial->palette();
		const QColor accent("#e71993");
		palette.setColor(QPalette::Highlight, accent);
		palette.setColor(QPalette::Button, QColor("#e9e9e9"));
		palette.setColor(QPalette::ButtonText, QColor("#19232a"));
		dial->setPalette(palette);
		auto accentPixels = [&] {
			QImage image(dial->size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			dial->render(&image);
			int count = 0;
			for (int y = 0; y < image.height(); ++y)
				for (int x = 0; x < image.width(); ++x)
					if (image.pixelColor(x, y) == accent)
						++count;
			return count;
		};
		const auto full = accentPixels();
		QVERIFY(full > 20);
		{
			const QSignalBlocker block(dial);
			dial->setValue(dial->minimum());
		}
		QVERIFY(accentPixels() < full);
		QVERIFY(MacroParameter::write(toggle.binding, 0));
		QTRY_VERIFY(!check->isChecked());
		QTRY_VERIFY_WITH_TIMEOUT(!dial->isEnabled() && !fader->isEnabled(), 3000);
		QCOMPARE(geometry(), initial);
		obs_source_remove(source);
		QTRY_VERIFY_WITH_TIMEOUT(!dial->isEnabled() && !fader->isEnabled() && !check->isEnabled(), 3000);
		QCOMPARE(geometry(), initial);
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
		host.activateWindow();
		QVERIFY(QTest::qWaitForWindowActive(&host));
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

	void testMacroTogglePresetValues()
	{
		auto *source = obs_source_create("timeline_parameter_probe", "Preset toggle target", nullptr, nullptr);
		QVERIFY(source);
		auto cleanup = qScopeGuard([&] {
			obs_source_remove(source);
			obs_source_release(source);
		});
		MacroButton initial;
		initial.kind = MacroControlKind::Toggle;
		QWidget host;
		bool configured = false;
		QTimer::singleShot(0, &host, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			QVERIFY(dialog);
			auto guard = qScopeGuard([&] {
				if (!configured)
					dialog->reject();
			});
			auto *target = dialog->findChild<QComboBox *>("macro-parameter-target");
			auto *property = dialog->findChild<QComboBox *>("macro-parameter-property");
			auto *a = dialog->findChild<QDoubleSpinBox *>("macro-toggle-a");
			auto *b = dialog->findChild<QDoubleSpinBox *>("macro-toggle-b");
			auto *buttons = dialog->findChild<QDialogButtonBox *>();
			QVERIFY(target && property && a && b && buttons);
			target->setCurrentIndex(target->findText("Preset toggle target"));
			property->setCurrentIndex(property->findText("Controls / Gain"));
			QVERIFY(a->isVisible() && b->isVisible());
			a->setValue(-0.5);
			b->setValue(-0.5);
			QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
			b->setValue(0.75);
			QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
			configured = true;
			dialog->accept();
		});
		const auto selected = editMacroParameter(&host, initial);
		QVERIFY(configured && selected && selected->toggleValues);
		QCOMPARE(selected->toggleValues->a.toDouble(), -0.5);
		QCOMPARE(selected->toggleValues->b.toDouble(), 0.75);
		QCOMPARE(MacroParameter::read(selected->binding).value(), 0.25);
		// Reopen the editor for a real text-valued toggle; no numeric coercion.
		MacroButton textInitial = *selected;
		textInitial.id = QUuid::createUuid().toString();
		bool textConfigured = false;
		QTimer::singleShot(0, &host, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			QVERIFY(dialog);
			auto guard = qScopeGuard([&] {
				if (!textConfigured)
					dialog->reject();
			});
			auto *property = dialog->findChild<QComboBox *>("macro-parameter-property");
			auto *a = dialog->findChild<QPlainTextEdit *>("macro-toggle-text-a");
			auto *b = dialog->findChild<QPlainTextEdit *>("macro-toggle-text-b");
			auto *buttons = dialog->findChild<QDialogButtonBox *>();
			QVERIFY(property && a && b && buttons);
			property->setCurrentIndex(property->findText("Text"));
			QVERIFY(a->isVisible() && b->isVisible());
			a->setPlainText("keep");
			b->setPlainText("123");
			QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
			textConfigured = true;
			dialog->accept();
		});
		const auto textChoice = editMacroParameter(&host, textInitial);
		QVERIFY(textConfigured && textChoice && textChoice->toggleValues);
		const auto text = *textChoice;
		QCOMPARE(text.binding.type, MacroParameterType::Text);
		QCOMPARE(text.toggleValues->b.toString(), QString("123"));
		QVERIFY(!MacroParameter::validToggleValues(MacroParameterType::Integer, {1.5, 2.0}));
		QVERIFY(!MacroParameter::validToggleValues(MacroParameterType::Float, {QString("1"), 2.0}));
		QVERIFY(!MacroParameter::validToggleValues(MacroParameterType::Text, {QString("a"), QString("a")}));
		QVERIFY(MacroParameter::validToggleValues(MacroParameterType::Text, {QString("a"), QString("")}));
		auto reduced = MacroParameter::describe(selected->binding);
		QVERIFY(reduced);
		reduced->max = 0.0;
		QVERIFY(!MacroParameter::toggleValuesFit(*reduced, *selected->toggleValues));
		MacroButtonModel model, restored;
		model.addButton(*selected);
		model.addButton(text);
		QVERIFY(MacroSerialization::fromJsonDocument(QJsonDocument(MacroSerialization::toJsonObject(model)),
							     restored));
		QCOMPARE(restored.buttons().first().toggleValues->b.toDouble(), 0.75);
		QCOMPARE(restored.buttons().last().toggleValues->b.toString(), QString("123"));
		auto invalid = MacroSerialization::toJsonObject(model);
		auto records = invalid["buttons"].toArray();
		auto broken = records.first().toObject();
		broken["toggleB"] = "0.75";
		records[0] = broken;
		invalid["buttons"] = records;
		QVERIFY(!MacroSerialization::fromJsonDocument(QJsonDocument(invalid), restored));
		QCOMPARE(restored.buttons().size(), 2);
		auto *numeric = createMacroParameterWidget(restored.buttons().first(), &host);
		auto *strings = createMacroParameterWidget(restored.buttons().last(), &host);
		strings->move(160, 0);
		host.resize(400, 120);
		host.show();
		auto *check = numeric->findChild<QCheckBox *>("macro-parameter-toggle");
		auto *textCheck = strings->findChild<QCheckBox *>("macro-parameter-toggle");
		QVERIFY(check && textCheck);
		QCOMPARE(check->checkState(), Qt::PartiallyChecked);
		QTest::mouseClick(check, Qt::LeftButton);
		QCOMPARE(MacroParameter::read(selected->binding).value(), -0.5);
		QCOMPARE(check->checkState(), Qt::Unchecked);
		QTest::mouseClick(check, Qt::LeftButton);
		QCOMPARE(MacroParameter::read(selected->binding).value(), 0.75);
		QCOMPARE(check->checkState(), Qt::Checked);
		QVERIFY(MacroParameter::write(selected->binding, 0.0));
		QTRY_COMPARE(check->checkState(), Qt::PartiallyChecked);
		QCOMPARE(MacroParameter::readText(text.binding).value(), QString("keep"));
		QTest::mouseClick(textCheck, Qt::LeftButton);
		QCOMPARE(MacroParameter::readText(text.binding).value(), QString("123"));
		QCOMPARE(textCheck->checkState(), Qt::Checked);
		QTest::mouseClick(textCheck, Qt::LeftButton);
		QCOMPARE(MacroParameter::readText(text.binding).value(), QString("keep"));
		QCOMPARE(textCheck->checkState(), Qt::Unchecked);
		QVERIFY(MacroParameter::write({text.binding.sourceUuid, {}, "enabled", MacroParameterType::Boolean},
					      0));
		QTRY_VERIFY_WITH_TIMEOUT(!check->isEnabled(), 3000);
	}

private:
	// Called only when explicitly exporting documentation images. Uses the
	// production dock/widgets with synthetic source settings and a fixed theme.
	void captureDocumentationScreenshots()
	{
		const QString output = qEnvironmentVariable("TIMELINE_DOC_SCREENSHOTS");
		if (output.isEmpty())
			return;
		QVERIFY(QDir(output).isAbsolute());
		QVERIFY(QDir().mkpath(output));
		QDir dir(output);
		g_documentation_locale =
			text_lookup_create(QDir::current().filePath("data/locale/en-US.ini").toUtf8().constData());
		QVERIFY(g_documentation_locale);
		auto releaseLocale = qScopeGuard([&] {
			text_lookup_destroy(g_documentation_locale);
			g_documentation_locale = nullptr;
		});
		const auto originalPalette = QApplication::palette();
		auto restorePalette = qScopeGuard([&] { QApplication::setPalette(originalPalette); });
		QPalette palette;
		palette.setColor(QPalette::Window, QColor("#252931"));
		palette.setColor(QPalette::WindowText, QColor("#e2e7ef"));
		palette.setColor(QPalette::Base, QColor("#191d24"));
		palette.setColor(QPalette::AlternateBase, QColor("#303641"));
		palette.setColor(QPalette::Text, QColor("#e2e7ef"));
		palette.setColor(QPalette::Button, QColor("#353b46"));
		palette.setColor(QPalette::ButtonText, QColor("#e2e7ef"));
		palette.setColor(QPalette::Mid, QColor("#596273"));
		palette.setColor(QPalette::Dark, QColor("#1a1e25"));
		palette.setColor(QPalette::Highlight, QColor("#55c7b6"));
		palette.setColor(QPalette::HighlightedText, QColor("#121c21"));
		palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#89919e"));
		const QString nativeTheme = qEnvironmentVariable("TIMELINE_DOC_THEME");
		const auto originalStyleSheet = qApp->styleSheet();
		const auto originalFont = qApp->font();
		const auto originalThemePaths = QDir::searchPaths("theme");
		const auto originalObsPaths = QDir::searchPaths("obs");
		auto restoreTheme = qScopeGuard([&] {
			qApp->setStyleSheet(originalStyleSheet);
			qApp->setFont(originalFont);
			QDir::setSearchPaths("theme", originalThemePaths);
			QDir::setSearchPaths("obs", originalObsPaths);
		});
		if (!nativeTheme.isEmpty()) {
			QDir theme(nativeTheme);
			QFile data(theme.filePath("theme.qt"));
			QFile css(theme.filePath("theme.qss"));
			QVERIFY(data.open(QIODevice::ReadOnly));
			QVERIFY(css.open(QIODevice::ReadOnly));
			QDataStream stream(&data);
			QFont font;
			QStringList themePaths, obsPaths;
			stream >> palette >> font >> themePaths >> obsPaths;
			QCOMPARE(stream.status(), QDataStream::Ok);
			QDir::setSearchPaths("theme", themePaths);
			QDir::setSearchPaths("obs", obsPaths);
			qApp->setFont(font);
			qApp->setStyleSheet(QString::fromUtf8(css.readAll()));
		}
		QApplication::setPalette(palette);

		QTemporaryDir temp;
		QVERIFY(temp.isValid());
		auto *source = obs_source_create("timeline_parameter_probe", "Demo effect", nullptr, nullptr);
		auto *audio = obs_source_create("timeline_audio_probe", "Demo audio", nullptr, nullptr);
		QVERIFY(source && audio);
		auto cleanup = qScopeGuard([&] {
			obs_source_remove(source);
			obs_source_release(source);
			obs_source_remove(audio);
			obs_source_release(audio);
		});
		const auto uuid = QString::fromUtf8(obs_source_get_uuid(source));
		const auto audioUuid = QString::fromUtf8(obs_source_get_uuid(audio));
		const MacroParameterBinding gain{uuid, {}, "gain", MacroParameterType::Float};
		const MacroParameterBinding count{uuid, {}, "count", MacroParameterType::Integer};
		const MacroParameterBinding volume{audioUuid,
						   {},
						   "timeline:mixer:volume",
						   MacroParameterType::Float,
						   "mixer"};
		QVERIFY(MacroParameter::write(gain, 0.65));
		QVERIFY(MacroParameter::write(count, 8));
		QVERIFY(MacroParameter::write(volume, 75));
		MacroButton action, slider, knob, xy, toggle, text;
		action.label = "Scene";
		action.color = QColor("#55c7b6");
		slider.kind = MacroControlKind::Slider;
		slider.label = "Volume";
		slider.binding = volume;
		knob.kind = MacroControlKind::Knob;
		knob.label = "Intensity";
		knob.binding = gain;
		xy.kind = MacroControlKind::XY;
		xy.label = "XY";
		xy.binding = gain;
		xy.bindingY = count;
		toggle.kind = MacroControlKind::Toggle;
		toggle.label = "Preset";
		toggle.binding = gain;
		toggle.toggleValues = MacroParameter::ToggleValues{0.25, 0.65};
		text.kind = MacroControlKind::Text;
		text.label = "Caption";
		text.binding = {uuid, {}, "untouched", MacroParameterType::Text};
		QVERIFY(MacroParameter::writeText(text.binding, "Now playing"));
		MacroStripDock dock(nullptr, temp.filePath("macro.json"));
		dock.setPalette(palette);
		for (const auto &button : {action, slider, knob, xy, toggle, text})
			dock.buttonModel()->addButton(button);
		auto settle = [&](QWidget &widget) {
			widget.show();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			QApplication::processEvents();
			QTest::qWait(100);
		};
		dock.resize(690, dock.minimumHeight());
		settle(dock);
		QVERIFY(dock.grab().save(dir.filePath("macro-strip.png")));
		action.bottomLabel = "Intro";
		slider.showValue = true;
		knob.showValue = true;
		xy.showValue = true;
		toggle.showValue = true;
		text.bottomLabel = "Now playing text";
		for (const auto &button : {action, slider, knob, xy, toggle, text})
			QVERIFY(dock.buttonModel()->replaceButtonById(button.id, button));
		dock.resize(690, dock.minimumHeight());
		settle(dock);
		QVERIFY(dock.grab().save(dir.filePath("macro-strip-readouts.png")));
		bool captured = false;
		QTimer::singleShot(0, &dock, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog)
				return;
			dialog->adjustSize();
			QApplication::processEvents();
			captured = dialog->grab().save(dir.filePath("toggle-editor.png"));
			dialog->reject();
		});
		editMacroParameter(&dock, toggle);
		QVERIFY(captured);
		captured = false;
		QTimer::singleShot(0, &dock, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog)
				return;
			dialog->adjustSize();
			QApplication::processEvents();
			captured = dialog->grab().save(dir.filePath("appearance-editor.png"));
			dialog->reject();
		});
		dock.editDisplay(xy.id);
		QVERIFY(captured);

		TimelineDock timeline(nullptr, temp.filePath("timeline.json"));
		timeline.setPalette(palette);
		auto *model = timeline.findChild<TimelineModel *>();
		QVERIFY(model);
		TimelineEvent intro, ramp, hold, cut;
		intro.type = TimelineEventType::SceneSwitch;
		intro.timeMs = 500;
		intro.label = "Intro";
		intro.color = QColor("#55c7b6");
		ramp.type = TimelineEventType::SetParameterValue;
		ramp.timeMs = 2000;
		ramp.label = "Fade in";
		ramp.durationMs = 2500;
		ramp.hasDuration = true;
		ramp.data = MacroParameter::encodeRamp({{gain, 0.0}, 1.0, "ease-in-out"});
		ramp.color = QColor("#dfaa70");
		hold.type = TimelineEventType::FreezeTime;
		hold.timeMs = 5000;
		hold.label = "Hold";
		hold.data = "3000";
		hold.color = QColor("#6FABC8");
		cut.type = TimelineEventType::SceneSwitch;
		cut.timeMs = 7000;
		cut.label = "Next scene";
		cut.color = QColor("#c9a2ee");
		intro.showLabel = ramp.showLabel = hold.showLabel = cut.showLabel = true;
		model->addEvents({intro, ramp, hold, cut});
		timeline.resize(1040, 180);
		settle(timeline);
		QVERIFY(timeline.grab().save(dir.filePath("timeline.png")));
		auto *compact = timeline.findChild<QCheckBox *>("timeline-compact-mode");
		QVERIFY(compact);
		compact->setChecked(true);
		timeline.resize(1040, timeline.minimumHeight());
		settle(timeline);
		QVERIFY(timeline.grab().save(dir.filePath("timeline-compact.png")));
	}

private slots:
	void testMacroOptionalBottomCaptionAndCompactXY()
	{
		auto *source = obs_source_create("timeline_parameter_probe", "Caption layout target", nullptr, nullptr);
		QVERIFY(source);
		auto cleanup = qScopeGuard([&] {
			obs_source_remove(source);
			obs_source_release(source);
		});
		MacroButton knob;
		knob.kind = MacroControlKind::Knob;
		knob.label = "Gain";
		knob.binding = {QString::fromUtf8(obs_source_get_uuid(source)), {}, "gain", MacroParameterType::Float};
		MacroButton xy = knob;
		xy.id = QUuid::createUuid().toString();
		xy.kind = MacroControlKind::XY;
		xy.label = "Position";
		xy.bindingY = {xy.binding.sourceUuid, {}, "count", MacroParameterType::Integer};
		MacroButton action;
		action.label = "Next";
		QTemporaryDir dir;
		MacroStripDock dock(nullptr, dir.filePath("macro.json"));
		for (const auto &b : {knob, xy, action})
			dock.buttonModel()->addButton(b);
		dock.resize(700, dock.minimumHeight());
		dock.show();
		auto settle = [&] {
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			QCoreApplication::processEvents();
		};
		settle();
		auto *pad = dock.findChild<QWidget *>("macro-parameter-xy");
		QVERIFY(pad);
		QCOMPARE(pad->size(), QSize(40, 40));
		auto *dial = dock.findChild<QDial *>("macro-parameter-knob");
		QVERIFY(dial);
		auto *column = dial->parentWidget()->parentWidget();
		auto *top = column->findChild<QLabel *>("macro-parameter-caption");
		QVERIFY(top);
		QCOMPARE(top->text(), QString("Gain"));
		const int compactHeight = dock.minimumHeight();
		bool appearanceEdited = false;
		QTimer::singleShot(0, &dock, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog)
				return;
			auto *topEdit = dialog->findChild<QLineEdit *>("macro-display-top");
			auto *mode = dialog->findChild<QComboBox *>("macro-display-bottom-mode");
			if (!topEdit || !mode) {
				dialog->reject();
				return;
			}
			topEdit->setText("Intensity");
			mode->setCurrentIndex(mode->findData(2));
			appearanceEdited = true;
			dialog->accept();
		});
		dock.editDisplay(knob.id);
		QVERIFY(appearanceEdited);
		const auto renamed = dock.buttonModel()->findById(knob.id);
		QVERIFY(renamed);
		QCOMPARE(renamed->binding.key, knob.binding.key);
		QCOMPARE(renamed->binding.sourceUuid, knob.binding.sourceUuid);
		QCOMPARE(renamed->label, QString("Intensity"));
		QVERIFY(renamed->showValue);
		knob = *renamed;
		action.bottomLabel = "Next scene";
		xy.bottomLabel = "Motion";
		xy.xySize = 48;
		QVERIFY(dock.buttonModel()->replaceButtonById(knob.id, knob));
		QVERIFY(dock.buttonModel()->replaceButtonById(xy.id, xy));
		QVERIFY(dock.buttonModel()->replaceButtonById(action.id, action));
		dock.resize(700, dock.minimumHeight());
		settle();
		dial = dock.findChild<QDial *>("macro-parameter-knob");
		pad = dock.findChild<QWidget *>("macro-parameter-xy");
		auto *next = dock.findChild<MacroButtonWidget *>();
		QVERIFY(dial && pad && next);
		column = dial->parentWidget()->parentWidget();
		top = column->findChild<QLabel *>("macro-parameter-caption");
		auto *bottom = column->findChild<QLabel *>("macro-parameter-bottom-caption");
		QVERIFY(top && bottom && bottom->isVisible());
		QCOMPARE(pad->size(), QSize(48, 48));
		QCOMPARE(top->text(), QString("Intensity"));
		QCOMPARE(bottom->text(), QString("0.25"));
		QVERIFY(bottom->mapTo(&dock, QPoint()).y() > dial->mapTo(&dock, QPoint()).y());
		QVERIFY(dock.minimumHeight() > compactHeight);
		QVERIFY(qAbs(dial->mapTo(&dock, dial->rect().center()).y() -
			     pad->mapTo(&dock, pad->rect().center()).y()) <= 1);
		QCOMPARE(next->height(), column->height());
		const QRect initial(next->mapTo(&dock, QPoint()), next->size());
		QVERIFY(MacroParameter::write(knob.binding, -0.95));
		QTRY_COMPARE(bottom->text(), QString("-0.95"));
		QCOMPARE(top->text(), QString("Intensity"));
		QCOMPARE(QRect(next->mapTo(&dock, QPoint()), next->size()), initial);
		MacroButtonModel restored;
		QVERIFY(MacroSerialization::fromJsonDocument(QJsonDocument(dock.toJson()), restored));
		QVERIFY(restored.findById(knob.id)->showValue);
		QCOMPARE(restored.findById(action.id)->bottomLabel, QString("Next scene"));
		QCOMPARE(restored.findById(xy.id)->xySize, 48);
		captureDocumentationScreenshots();
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

	void testTextParameterControlsAndScheduledValues()
	{
		auto *source = obs_source_create("timeline_parameter_probe", "Text target", nullptr, nullptr);
		QVERIFY(source);
		auto release = qScopeGuard([&] {
			obs_source_remove(source);
			obs_source_release(source);
		});
		const QString uuid = QString::fromUtf8(obs_source_get_uuid(source));
		MacroParameterBinding text{uuid, {}, "untouched", MacroParameterType::Text};
		MacroParameterBinding multi{uuid, {}, "caption", MacroParameterType::Text};
		QVERIFY(MacroParameter::describe(text));
		const auto multilineSpec = MacroParameter::describe(multi);
		QVERIFY(multilineSpec && multilineSpec->multiline);
		auto secret = text;
		secret.key = "secret";
		QVERIFY(!MacroParameter::describe(secret));
		secret.key = "notice";
		QVERIFY(!MacroParameter::describe(secret));
		QCOMPARE(MacroParameter::readText(text).value(), QString("keep"));
		MacroButton button;
		button.kind = MacroControlKind::Text;
		button.binding = text;
		button.label = "Text";
		MacroButtonModel model;
		model.addButton(button);
		MacroButtonModel restored;
		QVERIFY(MacroSerialization::fromJsonDocument(QJsonDocument(MacroSerialization::toJsonObject(model)),
							     restored));
		QCOMPARE(restored.buttons().first().kind, MacroControlKind::Text);
		QCOMPARE(restored.buttons().first().binding.type, MacroParameterType::Text);
		// Discover both control types through the actual assignment UI.
		QWidget editorHost;
		auto choose = [&](const QString &propertyName,
				  MacroControlKind controlKind) -> std::optional<MacroButton> {
			QTimer::singleShot(0, &editorHost, [&, propertyName] {
				auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
				if (!dialog)
					return;
				auto *target = dialog->findChild<QComboBox *>("macro-parameter-target");
				auto *property = dialog->findChild<QComboBox *>("macro-parameter-property");
				auto *buttons = dialog->findChild<QDialogButtonBox *>();
				if (target && property && buttons) {
					target->setCurrentIndex(target->findText("Text target"));
					property->setCurrentIndex(property->findText(propertyName));
					if (buttons->button(QDialogButtonBox::Ok)->isEnabled()) {
						dialog->accept();
						return;
					}
				}
				dialog->reject();
			});
			MacroButton initial;
			initial.kind = controlKind;
			return editMacroParameter(&editorHost, initial);
		};
		const auto textChoice = choose("Text", MacroControlKind::Text);
		QVERIFY(textChoice);
		QCOMPARE(textChoice->kind, MacroControlKind::Text);
		QCOMPARE(textChoice->binding.key, text.key);
		const auto boolChoice = choose("Enabled", MacroControlKind::Toggle);
		QVERIFY(boolChoice);
		QCOMPARE(boolChoice->kind, MacroControlKind::Toggle);
		QCOMPARE(boolChoice->binding.type, MacroParameterType::Boolean);

		QWidget host;
		auto *control = createMacroParameterWidget(button, &host);
		host.resize(250, 100);
		control->resize(control->sizeHint());
		host.show();
		auto *entry = control->findChild<QLineEdit *>("macro-parameter-text");
		QVERIFY(entry && entry->isEnabled());
		QCOMPARE(entry->text(), QString("keep"));
		entry->setFocus();
		QTest::keyClick(entry, Qt::Key_A, Qt::ControlModifier);
		QTest::keyClicks(entry, "new caption");
		QCOMPARE(MacroParameter::readText(text).value(), QString("keep"));
		QTest::qWait(300);
		QCOMPARE(entry->text(), QString("new caption"));
		QVERIFY(entry->isModified());
		auto *apply = control->findChild<QPushButton *>("macro-parameter-text-apply");
		QVERIFY(apply && apply->isVisible() && control->isVisible());
		QTest::mouseClick(apply, Qt::LeftButton);
		QCOMPARE(MacroParameter::readText(text).value(), QString("new caption"));
		QVERIFY(MacroParameter::writeText(text, "external"));
		QTRY_COMPARE(entry->text(), QString("external"));
		TimelineEvent cue;
		cue.type = TimelineEventType::SetParameterValue;
		const QString caption = QStringLiteral("First line\nSecond line — 日本語");
		cue.data = MacroParameter::encodeValue({multi, caption});
		auto decoded = MacroParameter::decodeValue(cue.data.toString());
		QVERIFY(decoded);
		QCOMPARE(decoded->value.toString(), caption);
		ObsExecutor executor;
		executor.executeEventStart(cue);
		QCOMPARE(MacroParameter::readText(multi).value(), caption);
		MacroButton multilineButton = button;
		multilineButton.binding = multi;
		auto *multilineControl = createMacroParameterWidget(multilineButton, &host);
		multilineControl->move(0, 55);
		multilineControl->resize(multilineControl->sizeHint());
		host.resize(300, 120);
		multilineControl->show();
		auto *edit = multilineControl->findChild<QPushButton *>("macro-parameter-multiline");
		QVERIFY(edit && edit->isVisible());
		const QString editedCaption = QStringLiteral("Live\nEdit");
		bool modalEdited = false;
		QTimer::singleShot(0, &host, [&] {
			auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
			if (!dialog)
				return;
			auto *area = dialog->findChild<QPlainTextEdit *>();
			if (area) {
				area->setPlainText(editedCaption);
				modalEdited = true;
				dialog->accept();
			} else {
				dialog->reject();
			}
		});
		edit->click();
		QVERIFY(modalEdited);
		QCOMPARE(MacroParameter::readText(multi).value(), editedCaption);
		// A defined value is configured without applying it.
		std::optional<QVariant> value;
		MacroButton initial;
		initial.binding = multi;
		bool defined = false;
		QTimer::singleShot(0, &editorHost, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog)
				return;
			auto *area = dialog->findChild<QPlainTextEdit *>("parameter-defined-text");
			if (area && area->toPlainText() == editedCaption) {
				area->setPlainText(caption);
				defined = true;
				dialog->accept();
			} else {
				dialog->reject();
			}
		});
		const auto selection = editDefinedParameter(&editorHost, initial, value);
		QVERIFY(defined && selection && value);
		QCOMPARE(selection->binding.key, multi.key);
		QCOMPARE(value->toString(), caption);
		QCOMPARE(MacroParameter::readText(multi).value(), editedCaption);
		executor.executeEventStart(cue);
		QCOMPARE(MacroParameter::readText(multi).value(), caption);

		QVERIFY(MacroParameter::writeText(text, QString()));
		QTRY_COMPARE(entry->text(), QString());
		QVERIFY(!MacroParameter::writeText(secret, "bad"));
		obs_source_remove(source);
		QVERIFY(!MacroParameter::writeText(text, "bad"));
		QTRY_VERIFY(!entry->isEnabled());
	}

	void testNativeMixerBindingsUseObsFaderCurve()
	{
		auto *source = obs_source_create("timeline_audio_probe", "Mixer test", nullptr, nullptr);
		QVERIFY(source);
		auto release = qScopeGuard([&] {
			obs_source_remove(source);
			obs_source_release(source);
		});
		const QString uuid = QString::fromUtf8(obs_source_get_uuid(source));
		MacroParameterBinding volume{uuid, {}, "timeline:mixer:volume", MacroParameterType::Float, "mixer"};
		MacroParameterBinding mute{uuid, {}, "timeline:mixer:mute", MacroParameterType::Boolean, "mixer"};
		QVERIFY(MacroParameter::describe(volume));
		QVERIFY(MacroParameter::describe(mute));
		QWidget editorHost;
		bool selected = false;
		QTimer::singleShot(0, &editorHost, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog)
				return;
			auto *target = dialog->findChild<QComboBox *>("macro-parameter-target");
			auto *property = dialog->findChild<QComboBox *>("macro-parameter-property");
			auto *buttons = dialog->findChild<QDialogButtonBox *>();
			if (target && property && buttons) {
				target->setCurrentIndex(target->findText("Mixer test"));
				property->setCurrentIndex(property->findText("Mixer / Volume (fader position)"));
				selected = buttons->button(QDialogButtonBox::Ok)->isEnabled();
			}
			if (selected)
				dialog->accept();
			else
				dialog->reject();
		});
		const auto edited = editMacroParameter(&editorHost, MacroButton{});
		QVERIFY(selected && edited);
		QCOMPARE(edited->binding.domain, QString("mixer"));
		QCOMPARE(edited->binding.key, volume.key);

		auto *fader = obs_fader_create(OBS_FADER_LOG);
		QVERIFY(fader);
		auto destroyFader = qScopeGuard([&] { obs_fader_destroy(fader); });
		obs_fader_set_deflection(fader, 0.5f);
		const float expected = obs_fader_get_mul(fader);
		QVERIFY(MacroParameter::write(volume, 50));
		QVERIFY(qAbs(obs_source_get_volume(source) - expected) < 1e-6);
		QVERIFY(qAbs(MacroParameter::read(volume).value() - 50) < 1e-3);
		QVERIFY(MacroParameter::write(mute, 1));
		QVERIFY(obs_source_muted(source));
		QVERIFY(qAbs(obs_source_get_volume(source) - expected) < 1e-6);
		MacroButton slider;
		slider.kind = MacroControlKind::Slider;
		slider.binding = volume;
		slider.label = "Mixer";
		MacroButtonModel model;
		model.addButton(slider);
		MacroButtonModel restored;
		QVERIFY(MacroSerialization::fromJsonDocument(QJsonDocument(MacroSerialization::toJsonObject(model)),
							     restored));
		QCOMPARE(restored.buttons().first().binding.domain, QString("mixer"));
		QWidget host;
		auto *control = createMacroParameterWidget(slider, &host);
		host.resize(200, 100);
		control->resize(control->sizeHint());
		host.show();
		auto *input = control->findChild<QSlider *>("macro-parameter-slider");
		QVERIFY(input && input->isEnabled());
		QCOMPARE(input->value(), 500);
		QVERIFY(control->findChild<QLabel *>("macro-parameter-caption")->toolTip().contains("dB"));
		obs_source_set_volume(source, 1.0f);
		QTRY_COMPARE(input->value(), 1000);
		input->setValue(500);
		QTRY_VERIFY(qAbs(obs_source_get_volume(source) - expected) < 1e-6);
		TimelineEvent cue;
		cue.type = TimelineEventType::SetParameterValue;
		cue.data = MacroParameter::encodeValue({volume, 0});
		auto decoded = MacroParameter::decodeValue(cue.data.toString());
		QVERIFY(decoded);
		QCOMPARE(decoded->binding.domain, QString("mixer"));
		ObsExecutor executor;
		executor.executeEventStart(cue);
		QCOMPARE(obs_source_get_volume(source), 0.0f);
		QTRY_COMPARE(input->value(), 0);
		cue.data = MacroParameter::encodeValue({mute, 0});
		executor.executeEventStart(cue);
		QVERIFY(!obs_source_muted(source));
		auto wrong = volume;
		wrong.domain.clear();
		QVERIFY(!MacroParameter::write(wrong, 100));
		obs_source_remove(source);
		QVERIFY(!MacroParameter::write(volume, 100));
		QTRY_VERIFY(!input->isEnabled());
	}

	void testMacroAutomationRecorderBuildsEditableRamps()
	{
		MacroAutomationRecorder recorder;
		MacroParameterBinding gain{"src-uuid", "filter-uuid", "gain", MacroParameterType::Float};
		MacroParameterBinding mute{"src-uuid", {}, "enabled", MacroParameterType::Boolean};
		QVERIFY(recorder.start(1250));
		QVERIFY(!recorder.start(1500));
		QVERIFY(recorder.record(gain, 0.2, 0));
		QVERIFY(!recorder.record(gain, 0.2, 50)); // unchanged
		QVERIFY(recorder.record(gain, 0.4, 120));
		QVERIFY(recorder.record(mute, 1.0, 122));
		QVERIFY(!recorder.record(gain, std::numeric_limits<double>::infinity(), 140));
		QVERIFY(!recorder.record({"", {}, "invalid", MacroParameterType::Float}, 0.5, 140));
		QVERIFY(recorder.record(gain, 0.8, 240));
		QVERIFY(recorder.record(gain, 0.9, 230) == false); // backwards clock
		const auto events = recorder.finish();
		QCOMPARE(events.size(), size_t(3));
		QVERIFY(!recorder.active());
		QCOMPARE(events.at(0).timeMs, 1250.0);
		QCOMPARE(events.at(0).type, TimelineEventType::SetParameterValue);
		QVERIFY(events.at(0).hasDuration);
		QCOMPARE(events.at(0).durationMs, 120.0);
		const auto first = MacroParameter::decodeRamp(events.at(0).data.toString());
		QVERIFY(first);
		QCOMPARE(first->start.value.toDouble(), 0.2);
		QCOMPARE(first->end, 0.4);
		QCOMPARE(first->easing, QString("linear"));
		const auto second = MacroParameter::decodeRamp(events.at(1).data.toString());
		QVERIFY(second);
		QCOMPARE(second->end, 0.8);
		QVERIFY(!events.at(2).hasDuration);
		const auto boolean = MacroParameter::decodeValue(events.at(2).data.toString());
		QVERIFY(boolean);
		QCOMPARE(boolean->value.toDouble(), 1.0);
		TimelineModel model;
		model.setHistoryEnabled(true);
		const size_t baseline = model.getEvents().size();
		model.addEvents(events);
		QCOMPARE(model.getEvents().size(), baseline + events.size());
		QVERIFY(model.undo());
		QCOMPARE(model.getEvents().size(), baseline); // one undoable take
		QVERIFY(recorder.start(900));
		QVERIFY(recorder.finish().empty());
		QVERIFY(recorder.start(0));
		for (int i = 0; i < 1200; ++i)
			QVERIFY(recorder.record(gain, i * 0.01, i * 100));
		QCOMPARE(recorder.sampleCount(), 1200);
		QVERIFY(!recorder.record(gain, 12.0, 120000)); // bounded even on a long live take
		QCOMPARE(recorder.finish().size(), size_t(1199));
	}

	void testMacroStripRecordingCapturesOnlyCommittedUserAdjustments()
	{
		auto *source = obs_source_create("timeline_parameter_probe", "Record source", nullptr, nullptr);
		QVERIFY(source);
		auto cleanup = qScopeGuard([&] {
			obs_source_remove(source);
			obs_source_release(source);
		});
		MacroButton fader;
		fader.kind = MacroControlKind::Slider;
		fader.label = "Record gain";
		fader.binding = {QString::fromUtf8(obs_source_get_uuid(source)), {}, "gain", MacroParameterType::Float};
		QTemporaryDir dir;
		TimelineDock timeline(nullptr, dir.filePath("record-timeline.json"));
		MacroStripDock strip(nullptr, dir.filePath("record-macros.json"));
		strip.buttonModel()->addButton(fader);
		strip.show();
		timeline.show();
		QCoreApplication::processEvents();
		QObject::connect(&strip, &MacroStripDock::parameterCommitted, &timeline,
				 &TimelineDock::recordParameterChange);
		auto *slider = strip.findChild<QSlider *>("macro-parameter-slider");
		auto *rec = timeline.findChild<QPushButton *>("timeline-record-automation-btn");
		QVERIFY(slider && slider->isEnabled() && rec);
		auto *model = timeline.findChild<TimelineModel *>();
		QVERIFY(model);
		auto *patch = obs_data_create();
		obs_data_set_double(patch, "gain", 0.45);
		obs_source_update(source, patch);
		obs_data_release(patch);
		QTest::qWait(330);
		QCOMPARE(model->getEvents().size(), size_t(0)); // external readback is never a take
		QVERIFY(rec->isEnabled());
		rec->click();
		QVERIFY(rec->isChecked());
		slider->setValue(4);
		QTRY_VERIFY_WITH_TIMEOUT(qAbs(MacroParameter::read(fader.binding).value() + 0.8) < 0.001, 2000);
		QTest::qWait(130);
		slider->setValue(10);
		QTRY_VERIFY_WITH_TIMEOUT(qAbs(MacroParameter::read(fader.binding).value() + 0.5) < 0.001, 2000);
		rec->click();
		QVERIFY(!rec->isChecked());
		QCOMPARE(model->getEvents().size(), size_t(1));
		QVERIFY(model->getEvents().front().hasDuration);
		QVERIFY(MacroParameter::decodeRamp(model->getEvents().front().data.toString()));
		QVERIFY(model->undo());
		QCOMPARE(model->getEvents().size(), size_t(0));
		slider->setValue(12);
		QTest::qWait(150);
		QCOMPARE(model->getEvents().size(), size_t(0)); // stopped recording
		rec->click();
		auto *deadline = timeline.findChild<QTimer *>("timeline-record-automation-timeout");
		QVERIFY(deadline && deadline->isActive());
		deadline->start(70); // exercise auto-stop without waiting two minutes
		QTRY_VERIFY_WITH_TIMEOUT(!rec->isChecked(), 2000);
		QCOMPARE(model->getEvents().size(), size_t(0)); // no synthetic parameter cue
		auto *play = timeline.findChild<QPushButton *>("timeline-play-btn");
		auto *stop = timeline.findChild<QPushButton *>("timeline-stop-btn");
		QVERIFY(play && stop);
		play->click();
		QVERIFY(!rec->isEnabled()); // avoid recording playback-driven writes
		stop->click();
		QVERIFY(rec->isEnabled()); // user can record again after Stop
	}

	void testMacroMonitorDeduplicatesVisibleBindingsAndStopsHiddenReads()
	{
		auto *source = obs_source_create("timeline_parameter_probe", "Shared monitor target", nullptr, nullptr);
		QVERIFY(source);
		auto cleanup = qScopeGuard([&] {
			obs_source_remove(source);
			obs_source_release(source);
		});
		auto *probe = static_cast<ParameterProbe *>(obs_obj_get_data(source));
		QVERIFY(probe);
		MacroButton control;
		control.kind = MacroControlKind::Text;
		control.binding = {QString::fromUtf8(obs_source_get_uuid(source)),
				   {},
				   "untouched",
				   MacroParameterType::Text};
		QWidget host;
		auto *layout = new QHBoxLayout(&host);
		for (int i = 0; i < 8; ++i)
			layout->addWidget(createMacroParameterWidget(control, &host));
		MacroButton preset = control;
		preset.kind = MacroControlKind::Toggle;
		preset.toggleValues = MacroParameter::ToggleValues{QString("keep"), QString("external")};
		for (int i = 0; i < 4; ++i)
			layout->addWidget(createMacroParameterWidget(preset, &host));
		host.show();
		QVERIFY(QTest::qWaitForWindowExposed(&host));
		const auto entries = host.findChildren<QLineEdit *>();
		const auto toggles = host.findChildren<QCheckBox *>();
		QCOMPARE(entries.size(), 8);
		QCOMPARE(toggles.size(), 4);
		probe->propertyReads = 0;
		QVERIFY(MacroParameter::writeText(control.binding, "external"));
		probe->propertyReads = 0;
		QTRY_VERIFY_WITH_TIMEOUT(std::all_of(entries.cbegin(), entries.cend(),
						     [](auto *entry) { return entry->text() == "external"; }),
					 1500);
		QTRY_VERIFY_WITH_TIMEOUT(std::all_of(toggles.cbegin(), toggles.cend(),
						     [](auto *toggle) { return toggle->checkState() == Qt::Checked; }),
					 1500);
		QVERIFY2(probe->propertyReads.load() <= 3, "One poll must share readback across duplicate bindings");
		host.hide();
		probe->propertyReads = 0;
		QTest::qWait(550);
		QCOMPARE(probe->propertyReads.load(), 0);
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
		QCOMPARE(properties.size(), 5);
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

	void testMacroControlTypePrecedesAndFiltersProperties()
	{
		auto *source = obs_source_create("timeline_parameter_probe", "Type first target", nullptr, nullptr);
		QVERIFY(source);
		auto cleanup = qScopeGuard([&] {
			obs_source_remove(source);
			obs_source_release(source);
		});
		// A numeric-looking string remains a string property.
		QVERIFY(MacroParameter::writeText(
			{QString::fromUtf8(obs_source_get_uuid(source)), {}, "untouched", MacroParameterType::Text},
			"123"));
		QWidget host;
		bool configured = false;
		QTimer::singleShot(0, &host, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			QVERIFY(dialog);
			auto close = qScopeGuard([&] {
				if (!configured)
					dialog->reject();
			});
			auto *kind = dialog->findChild<QComboBox *>("macro-parameter-kind");
			auto *target = dialog->findChild<QComboBox *>("macro-parameter-target");
			auto *property = dialog->findChild<QComboBox *>("macro-parameter-property");
			auto *buttons = dialog->findChild<QDialogButtonBox *>();
			QVERIFY(kind && target && property && buttons);
			QCOMPARE(kind->count(), 4);
			QVERIFY(kind->geometry().top() < property->geometry().top());
			auto select = [&](MacroControlKind k) {
				kind->setCurrentIndex(kind->findData(static_cast<int>(k)));
			};
			select(MacroControlKind::Toggle);
			target->setCurrentIndex(target->findText("Type first target"));
			QVERIFY(property->findText("Enabled") >= 0);
			QVERIFY(property->findText("Controls / Gain") >= 0);
			QVERIFY(property->findText("Text") >= 0);
			property->setCurrentIndex(property->findText("Enabled"));
			QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
			select(MacroControlKind::Knob);
			QCOMPARE(property->currentIndex(), -1);
			QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
			QVERIFY(property->findText("Controls / Gain") >= 0);
			QCOMPARE(property->findText("Enabled"), -1);
			QCOMPARE(property->findText("Text"), -1);
			QCOMPARE(property->findText("Caption"), -1);
			property->setCurrentIndex(property->findText("Controls / Gain"));
			select(MacroControlKind::Slider);
			QCOMPARE(property->currentText(), QString("Controls / Gain"));
			QCOMPARE(property->findText("Text"), -1);
			QCOMPARE(property->findText("Caption"), -1);
			select(MacroControlKind::Text);
			QCOMPARE(property->currentIndex(), -1);
			QVERIFY(property->findText("Text") >= 0);
			QCOMPARE(property->findText("Controls / Gain"), -1);
			property->setCurrentIndex(property->findText("Text"));
			QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
			configured = true;
			dialog->accept();
		});
		MacroButton initial;
		initial.kind = MacroControlKind::Slider;
		const auto edited = editMacroParameter(&host, initial);
		QVERIFY(configured && edited);
		QCOMPARE(edited->kind, MacroControlKind::Text);
		QCOMPARE(edited->binding.key, QString("untouched"));
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

	void testTimelineMarqueeAndGroupDragPreserveOffsets()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		TimelineModel initial;
		TimelineEvent a, b, c;
		a.timeMs = 1000;
		b.timeMs = 2750;
		c.timeMs = 5000;
		initial.addEvents({a, b, c});
		const auto path = dir.filePath("timeline.json");
		QVERIFY(TimelineSerialization::saveToFile(
			path, QJsonObject{{"events", initial.toJson()},
					  {"settings", QJsonObject{{"snap", true},
								   {"snapIntervalMs", 500.0},
								   {"zoomPixelsPerSecond", 100.0},
								   {"endTime", 6.0}}}}));
		TimelineDock dock(nullptr, path);
		dock.resize(850, 260);
		dock.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dock));
		auto *view = dock.findChild<QGraphicsView *>();
		auto *model = dock.findChild<TimelineModel *>();
		auto *scheduler = dock.findChild<TimelineScheduler *>();
		QVERIFY(view && model && scheduler);
		QHash<QString, TimelineEventItem *> markers;
		for (auto *graphics : view->scene()->items())
			if (auto *item = dynamic_cast<TimelineEventItem *>(graphics))
				markers.insert(item->getEventId(), item);
		QCOMPARE(markers.size(), 3);
		auto center = [&](const QString &id) {
			return view->mapFromScene(markers[id]->mapToScene(markers[id]->rect().center()));
		};
		auto move = [&](QPoint point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
			QMouseEvent event(QEvent::MouseMove, QPointF(point),
					  QPointF(view->viewport()->mapToGlobal(point)), Qt::NoButton, Qt::LeftButton,
					  modifiers);
			QCoreApplication::sendEvent(view->viewport(), &event);
		};
		const QPoint start = center(a.id) - QPoint(25, 18);
		const QPoint end = center(b.id) + QPoint(25, 18);
		QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, start);
		move(end);
		QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, end);
		QVERIFY(markers[a.id]->isSelected());
		QVERIFY(markers[b.id]->isSelected());
		QVERIFY(!markers[c.id]->isSelected());
		QCOMPARE(scheduler->getCurrentTime(), 0.0);
		const QPoint press = center(a.id);
		QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, press);
		move(press + QPoint(76, 0));
		QCOMPARE(model->findEventById(a.id)->timeMs, 1000.0);
		QCOMPARE(model->findEventById(b.id)->timeMs, 2750.0);
		QCOMPARE(markers[a.id]->getTimeMs(), 2000.0);
		QCOMPARE(markers[b.id]->getTimeMs(), 3750.0);
		QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, press + QPoint(76, 0));
		QCOMPARE(model->findEventById(a.id)->timeMs, 2000.0);
		QCOMPARE(model->findEventById(b.id)->timeMs, 3750.0);
		QCOMPARE(model->findEventById(c.id)->timeMs, 5000.0);
		QVERIFY(model->undo());
		QCOMPARE(model->findEventById(a.id)->timeMs, 1000.0);
		QCOMPARE(model->findEventById(b.id)->timeMs, 2750.0);
		QVERIFY(!model->canUndo());
		QCoreApplication::processEvents();
		// Ctrl click toggles membership, Shift click extends it.
		QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ControlModifier, center(b.id));
		QVERIFY(!markers[b.id]->isSelected());
		QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::ShiftModifier, center(b.id));
		QVERIFY(markers[b.id]->isSelected());
		const QPoint second = center(a.id);
		QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, second);
		move(second - QPoint(200, 0));
		QCOMPARE(markers[a.id]->getTimeMs(), 0.0);
		QCOMPARE(markers[b.id]->getTimeMs(), 1750.0);
		QTest::keyClick(view, Qt::Key_Escape);
		QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, second - QPoint(200, 0));
		QCOMPARE(markers[a.id]->getTimeMs(), 1000.0);
		QCOMPARE(markers[b.id]->getTimeMs(), 2750.0);
		QVERIFY(!model->canUndo());
		QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, second);
		move(second + QPoint(450, 0));
		QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, second + QPoint(450, 0));
		QCOMPARE(model->findEventById(a.id)->timeMs, 4250.0);
		QCOMPARE(model->findEventById(b.id)->timeMs, 6000.0);
		QVERIFY(model->undo());
		QVERIFY(!model->canUndo());
	}

	void testTimelineDurationHandleResizesWithOneUndo()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto path = dir.filePath("timeline.json");
		TimelineModel initial;
		TimelineEvent event;
		event.type = TimelineEventType::FilterEnable;
		event.timeMs = 1000;
		event.hasDuration = true;
		event.durationMs = 1000;
		initial.addEvent(event);
		QVERIFY(TimelineSerialization::saveToFile(
			path, QJsonObject{{"events", initial.toJson()},
					  {"settings", QJsonObject{{"snap", true},
								   {"snapIntervalMs", 500.0},
								   {"zoomPixelsPerSecond", 100.0}}}}));
		TimelineDock dock(nullptr, path);
		dock.resize(800, 240);
		dock.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dock));
		auto *view = dock.findChild<QGraphicsView *>();
		auto *model = dock.findChild<TimelineModel *>();
		QVERIFY(view && model);
		TimelineEventItem *marker = nullptr;
		for (auto *graphics : view->scene()->items())
			if (auto *item = dynamic_cast<TimelineEventItem *>(graphics);
			    item && item->getEventId() == event.id)
				marker = item;
		QVERIFY(marker);
		const QPoint begin = view->mapFromScene(QPointF(200, marker->mapToScene(marker->rect().center()).y()));
		QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, begin);
		for (int offset : {20, 50, 76}) {
			const QPoint next = begin + QPoint(offset, 0);
			QMouseEvent move(QEvent::MouseMove, QPointF(next), QPointF(view->viewport()->mapToGlobal(next)),
					 Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
			QCoreApplication::sendEvent(view->viewport(), &move);
			QCOMPARE(model->getEvents().front().durationMs, 1000.0);
		}
		QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, begin + QPoint(76, 0));
		QCOMPARE(model->getEvents().front().durationMs, 2000.0);
		QCOMPARE(model->getEvents().front().timeMs, 1000.0);
		QVERIFY(model->undo());
		QCOMPARE(model->getEvents().front().durationMs, 1000.0);
		QVERIFY(!model->canUndo());
		QVERIFY(model->redo());
		QCOMPARE(model->getEvents().front().durationMs, 2000.0);
		QCoreApplication::processEvents();
		const QPoint second = view->mapFromScene(QPointF(300, marker->mapToScene(marker->rect().center()).y()));
		QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, second);
		const QPoint moved = second + QPoint(50, 0);
		QMouseEvent move(QEvent::MouseMove, QPointF(moved), QPointF(view->viewport()->mapToGlobal(moved)),
				 Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(view->viewport(), &move);
		QTest::keyClick(view, Qt::Key_Escape);
		QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, moved);
		QCOMPARE(model->getEvents().front().durationMs, 2000.0);
		QVERIFY(model->undo());
		QCOMPARE(model->getEvents().front().durationMs, 1000.0);
		QVERIFY(!model->canUndo());
	}

	void testTimelineZoomFitIncludesLongDurationAndEndMarker()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		TimelineModel initial;
		TimelineEvent cue;
		cue.timeMs = 800000;
		cue.hasDuration = true;
		cue.durationMs = 200000;
		cue.type = TimelineEventType::FilterEnable;
		initial.addEvent(cue);
		const auto path = dir.filePath("timeline.json");
		QVERIFY(TimelineSerialization::saveToFile(
			path,
			QJsonObject{{"events", initial.toJson()},
				    {"settings", QJsonObject{{"endTime", 900.0}, {"zoomPixelsPerSecond", 100.0}}}}));
		TimelineDock dock(nullptr, path);
		dock.resize(800, 240);
		dock.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dock));
		auto *view = dock.findChild<QGraphicsView *>();
		QVERIFY(view);
		QVERIFY2(view->sceneRect().right() >= 100000, "Long cues must remain reachable before zoom fit");
		auto *fit = dock.findChild<QAction *>("timeline-zoom-fit-action");
		QVERIFY(fit);
		fit->trigger();
		QCoreApplication::processEvents();
		TimelineEventItem *item = nullptr;
		for (auto *graphics : view->scene()->items())
			if (auto *candidate = dynamic_cast<TimelineEventItem *>(graphics))
				item = candidate;
		QVERIFY(item);
		const QRectF visible = view->mapToScene(view->viewport()->rect()).boundingRect();
		QVERIFY(visible.left() <= 0.0);
		QVERIFY(visible.right() >= item->sceneBoundingRect().right());
		QVERIFY(visible.right() - item->sceneBoundingRect().right() < 150.0);
		QCOMPARE(item->getTimeMs(), cue.timeMs);
	}

	void testTimelineCursorAnchoredZoom()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto path = dir.filePath("timeline.json");
		QVERIFY(TimelineSerialization::saveToFile(
			path, QJsonObject{{"events", QJsonArray{}},
					  {"settings", QJsonObject{{"zoomPixelsPerSecond", 100.0}}}}));
		TimelineDock dock(nullptr, path);
		dock.resize(800, 240);
		dock.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dock));
		auto *view = dock.findChild<QGraphicsView *>();
		QVERIFY(view);
		view->horizontalScrollBar()->setValue(500);
		const QPoint cursor(300, 12);
		const double before = view->mapToScene(cursor).x() / 100.0;
		QWheelEvent wheel(QPointF(cursor), QPointF(view->viewport()->mapToGlobal(cursor)), QPoint(),
				  QPoint(0, 120), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
		QCoreApplication::sendEvent(view->viewport(), &wheel);
		const double after = view->mapToScene(cursor).x() / 150.0;
		QVERIFY2(std::abs(before - after) < 0.02, "Zoom must preserve the time under the cursor");
	}

	void testTimelineVisualTicksAreCoalescedWithoutLosingFinalPosition()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		TimelineDock dock(nullptr, dir.filePath("timeline.json"));
		dock.resize(800, 240);
		dock.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dock));
		auto *view = dock.findChild<QGraphicsView *>();
		auto *scheduler = dock.findChild<TimelineScheduler *>();
		QVERIFY(view && scheduler);
		QGraphicsLineItem *playhead = nullptr;
		for (auto *item : view->scene()->items())
			if (auto *line = dynamic_cast<QGraphicsLineItem *>(item); line && line->zValue() == 100)
				playhead = line;
		QVERIFY(playhead);
		scheduler->seek(0);
		scheduler->start();
		int changes = 0;
		double previous = playhead->line().x1();
		for (int i = 1; i <= 40; ++i) {
			QVERIFY(QMetaObject::invokeMethod(&dock, "onTick", Qt::DirectConnection,
							  Q_ARG(double, i * 10.0)));
			const double next = playhead->line().x1();
			if (next != previous)
				++changes;
			previous = next;
		}
		QVERIFY2(changes < 40, "Visual updates must coalesce rapid scheduler ticks");
		scheduler->pause();
		scheduler->seek(0);
		QCOMPARE(playhead->line().x1(), 0.0);
	}

	void testTimelineRulerStaysPinnedAndScrubsWhileVerticallyScrolled()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		TimelineModel initial;
		for (int i = 0; i < 15; ++i) {
			TimelineEvent event;
			event.timeMs = 2000;
			initial.addEvent(event);
		}
		const auto path = dir.filePath("timeline.json");
		QVERIFY(TimelineSerialization::saveToFile(
			path, QJsonObject{{"events", initial.toJson()},
					  {"settings", QJsonObject{{"zoomPixelsPerSecond", 100.0}}}}));
		TimelineDock dock(nullptr, path);
		dock.resize(800, 180);
		dock.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dock));
		auto *view = dock.findChild<QGraphicsView *>();
		auto *scheduler = dock.findChild<TimelineScheduler *>();
		QVERIFY(view && scheduler);
		auto *ruler = view->viewport()->findChild<QWidget *>("timeline-pinned-ruler");
		QVERIFY(ruler && ruler->isVisible());
		QCOMPARE(ruler->geometry().top(), 0);
		QCOMPARE(ruler->height(), 20);
		const QImage before = ruler->grab().toImage();
		QVERIFY(view->verticalScrollBar()->maximum() > 100);
		view->verticalScrollBar()->setValue(100);
		QCoreApplication::processEvents();
		QCOMPARE(ruler->geometry().top(), 0);
		QCOMPARE(ruler->grab().toImage(), before);
		const QPoint seek = view->mapFromScene(QPointF(350, 0));
		QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(seek.x(), 10));
		QCOMPARE(scheduler->getCurrentTime(), 3500.0);
		QCOMPARE(view->verticalScrollBar()->value(), 100);
		view->horizontalScrollBar()->setValue(100);
		QCoreApplication::processEvents();
		QVERIFY(ruler->grab().toImage() != before);
	}

	void testTimelineRampRenderingShowsDirectionAndEasing()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		TimelineModel initial;
		TimelineEvent cue;
		cue.type = TimelineEventType::SetParameterValue;
		cue.timeMs = 1000;
		cue.hasDuration = true;
		cue.durationMs = 3000;
		cue.color = Qt::red;
		MacroParameterBinding binding{"source", {}, "gain", MacroParameterType::Float};
		auto payload = [&](double start, double end, const QString &ease) {
			return MacroParameter::encodeRamp({{binding, start}, end, ease});
		};
		cue.data = payload(0, 1, "linear");
		initial.addEvent(cue);
		const auto path = dir.filePath("timeline.json");
		QVERIFY(TimelineSerialization::saveToFile(
			path, QJsonObject{{"events", initial.toJson()},
					  {"settings", QJsonObject{{"zoomPixelsPerSecond", 100.0}}}}));
		TimelineDock dock(nullptr, path);
		dock.resize(800, 240);
		dock.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dock));
		auto *view = dock.findChild<QGraphicsView *>();
		auto *model = dock.findChild<TimelineModel *>();
		QVERIFY(view && model);
		TimelineEventItem *marker = nullptr;
		for (auto *item : view->scene()->items())
			if (auto *candidate = dynamic_cast<TimelineEventItem *>(item))
				marker = candidate;
		QVERIFY(marker);
		auto render = [&] {
			QImage image(280, 20, QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::white);
			QPainter painter(&image);
			const auto center = marker->mapToScene(marker->rect().center());
			view->scene()->render(&painter, QRectF(0, 0, 280, 20),
					      QRectF(center.x() + 20, center.y() - 10, 280, 20));
			return image;
		};
		const QImage ascending = render();
		cue.data = payload(1, 0, "linear");
		QVERIFY(model->replaceEventById(cue.id, cue));
		QCoreApplication::processEvents();
		const QImage descending = render();
		QVERIFY(ascending != descending);
		cue.data = payload(0, 1, "ease-in");
		QVERIFY(model->replaceEventById(cue.id, cue));
		QCoreApplication::processEvents();
		QVERIFY(ascending != render());
	}

	void testTimelineGridDoesNotAllocateSceneItemsAtHighZoom()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto path = dir.filePath("timeline.json");
		QVERIFY(TimelineSerialization::saveToFile(
			path, QJsonObject{{"events", QJsonArray{}},
					  {"settings", QJsonObject{{"zoomPixelsPerSecond", 1000.0}}}}));
		TimelineDock dock(nullptr, path);
		dock.resize(800, 200);
		dock.show();
		QCoreApplication::processEvents();
		auto *view = dock.findChild<QGraphicsView *>();
		QVERIFY(view && view->scene());
		QVERIFY(view->sceneRect().width() >= 300000);
		QVERIFY2(view->scene()->items().size() < 20, "Grid and ruler must not allocate one item per tick");
	}

	void testThemePalettePreservesDarkForegroundsAndRefreshesGrid()
	{
		QPalette light;
		light.setColor(QPalette::Window, Qt::white);
		light.setColor(QPalette::WindowText, Qt::black);
		light.setColor(QPalette::ButtonText, QColor("#15232e"));
		light.setColor(QPalette::Text, QColor("#102030"));
		light.setColor(QPalette::HighlightedText, Qt::black);
		light.setColor(QPalette::Highlight, QColor("#ffdd99"));
		light.setColor(QPalette::Link, QColor("#234567"));
		QCOMPARE(ThemeIcons::iconColorFor(light), QColor(Qt::black));
		QCOMPARE(ThemeIcons::iconColorFor(light, true), QColor(Qt::black));
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		TimelineDock dock(nullptr, dir.filePath("timeline.json"));
		dock.resize(800, 180);
		dock.show();
		QCoreApplication::processEvents();
		dock.setPalette(light);
		QCoreApplication::processEvents();
		auto *view = dock.findChild<QGraphicsView *>();
		QVERIFY(view && view->scene());
		// Render a region away from the playhead and empty-state hint.
		// The grid must remain visible and react to palette changes even
		// though it is no longer a collection of line items.
		view->scene()->setBackgroundBrush(Qt::white);
		auto renderGrid = [&]() {
			QImage image(80, 40, QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::white);
			QPainter painter(&image);
			view->scene()->render(&painter, QRectF(0, 0, 80, 40), QRectF(980, 22, 80, 40));
			return image;
		};
		const QImage darkGrid = renderGrid();
		int darkPixels = 0;
		for (int y = 0; y < darkGrid.height(); ++y)
			for (int x = 0; x < darkGrid.width(); ++x)
				if (darkGrid.pixelColor(x, y).red() < 240)
					++darkPixels;
		QVERIFY(darkPixels > 0);
		light.setColor(QPalette::Text, Qt::red);
		dock.setPalette(light);
		QCoreApplication::processEvents();
		const QImage redGrid = renderGrid();
		int redPixels = 0;
		for (int y = 0; y < redGrid.height(); ++y)
			for (int x = 0; x < redGrid.width(); ++x) {
				const auto color = redGrid.pixelColor(x, y);
				if (color.red() - color.blue() > 25)
					++redPixels;
			}
		QVERIFY(redPixels > 0);
		QVERIFY(darkGrid != redGrid);
		for (auto *item : view->scene()->items()) {
			auto *line = qgraphicsitem_cast<QGraphicsLineItem *>(item);
			if (line && line->zValue() == 100)
				QCOMPARE(line->pen().color(), light.color(QPalette::Link));
		}
	}

	void testMacroThemeChangePreservesTextDraftAndDoesNotSave()
	{
		auto *source = obs_source_create("timeline_parameter_probe", "Theme text target", nullptr, nullptr);
		QVERIFY(source);
		auto cleanup = qScopeGuard([&] {
			obs_source_remove(source);
			obs_source_release(source);
		});
		MacroStripDock dock;
		MacroButton text;
		text.kind = MacroControlKind::Text;
		text.binding = {QString::fromUtf8(obs_source_get_uuid(source)),
				{},
				"untouched",
				MacroParameterType::Text};
		dock.buttonModel()->addButton(text);
		dock.show();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		auto *entry = dock.findChild<QLineEdit *>("macro-parameter-text");
		QVERIFY(entry);
		entry->setText("draft");
		entry->setModified(true);
		QPointer<QLineEdit> original(entry);
		QSignalSpy saves(&dock, &MacroStripDock::saveRequested);
		dock.onFrontendEvent(OBS_FRONTEND_EVENT_THEME_CHANGED);
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QVERIFY(original);
		QCOMPARE(original->text(), QString("draft"));
		QVERIFY(original->isModified());
		QCOMPARE(saves.count(), 0);
		QCOMPARE(MacroParameter::readText(text.binding).value(), QString("keep"));
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
