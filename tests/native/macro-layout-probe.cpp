#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QApplication>
#include <QMainWindow>
#include <QDockWidget>
#include <QPushButton>
#include <QSlider>
#include <QMenu>
#include <QTimer>
#include <QTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cstring>
#include <QScrollArea>
#include <QPointer>
#include <QMouseEvent>

OBS_DECLARE_MODULE()

static int failures = 0;
static const char *audioName(void *)
{
	return "Macro layout probe audio";
}
static void *audioCreate(obs_data_t *, obs_source_t *)
{
	return new int(0);
}
static void audioDestroy(void *data)
{
	delete static_cast<int *>(data);
}
static obs_source_info audioInfo = {};

static void check(const char *name, bool ok)
{
	blog(ok ? LOG_INFO : LOG_ERROR, "MACROLAYOUT %s=%d", name, ok);
	if (!ok)
		++failures;
}

static QDockWidget *findDock(QMainWindow *main, const QString &title)
{
	for (auto *dock : main->findChildren<QDockWidget *>())
		if (dock->windowTitle() == title)
			return dock;
	return nullptr;
}

static bool choose(QWidget *strip, const QString &name)
{
	auto *add = strip->findChild<QPushButton *>("macro-strip-add");
	if (!add)
		return false;
	bool found = false;
	QTimer::singleShot(0, add, [&] {
		auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
		if (!menu)
			return;
		for (auto *action : menu->actions()) {
			if (action->objectName() == name && action->isEnabled()) {
				found = true;
				QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
						  menu->actionGeometry(action).center());
				return;
			}
		}
		menu->close();
	});
	QTest::mouseClick(add, Qt::LeftButton);
	QCoreApplication::processEvents();
	return found;
}

static bool current(const char *name)
{
	auto *source = obs_frontend_get_current_scene();
	const bool ok = source && !std::strcmp(obs_source_get_name(source), name);
	if (source)
		obs_source_release(source);
	return ok;
}

static void probe()
{
	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	auto *audio = findDock(main, "Macro Strip: Audio");
	auto *scenes = findDock(main, "Macro Strip: Scenes");
	const bool restart = qgetenv("MACROLAYOUT_RESTART") == "1";
	check("two_native_docks", audio && scenes && audio != scenes);
	if (!audio || !scenes) {
		main->close();
		return;
	}
	check("stable_native_ids", audio->objectName().startsWith("timeline_macro_strip_") &&
					   scenes->objectName().startsWith("timeline_macro_strip_") &&
					   audio->objectName() != scenes->objectName());
	if (restart) {
		check("restored_audio_area", main->dockWidgetArea(audio) == Qt::RightDockWidgetArea);
		check("restored_scenes_area", main->dockWidgetArea(scenes) == Qt::BottomDockWidgetArea);
		check("restored_visible", audio->isVisible() && scenes->isVisible());
		main->close();
		return;
	}
	// Other system-installed docks can constrain the available space.
	// Hide them in this disposable profile; never touch normal OBS layout.
	for (auto *dock : main->findChildren<QDockWidget *>())
		if (dock != audio && dock != scenes)
			dock->hide();
	main->resize(1400, 900);
	main->addDockWidget(Qt::RightDockWidgetArea, audio);
	main->addDockWidget(Qt::BottomDockWidgetArea, scenes);
	audio->setFloating(false);
	scenes->setFloating(false);
	audio->show();
	scenes->show();
	main->resizeDocks({audio}, {235}, Qt::Horizontal);
	main->resizeDocks({scenes}, {160}, Qt::Vertical);
	main->resizeDocks({audio}, {235}, Qt::Horizontal);
	QTest::qWait(250);
	check("both_visible", audio->isVisible() && scenes->isVisible());
	check("vertical_fader",
	      audio->findChild<QSlider *>("macro-parameter-slider") &&
		      audio->findChild<QSlider *>("macro-parameter-slider")->orientation() == Qt::Vertical);
	auto *grid = scenes->findChild<QWidget *>("macro-button-grid");
	auto *cell0 = grid ? grid->findChild<QWidget *>("macro-grid-cell-0") : nullptr;
	auto *cell1 = grid ? grid->findChild<QWidget *>("macro-grid-cell-1") : nullptr;
	check("native_grid", cell0 && cell1 && cell0->isVisible() && cell1->isVisible() && cell0->x() < cell1->x());
	auto *first = obs_scene_create("Macro probe first");
	auto *second = obs_scene_create("Macro probe second");
	auto *entry = obs_data_create();
	obs_data_set_string(entry, "id", "macro_layout_probe_audio");
	obs_data_set_string(entry, "name", "Probe volume");
	obs_data_set_string(entry, "uuid", "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa");
	auto *audioSource = obs_load_source(entry);
	obs_data_release(entry);
	if (audioSource) {
		obs_scene_add(second, audioSource);
		obs_source_set_volume(audioSource, .4f);
	}
	auto *slider = audio->findChild<QSlider *>("macro-parameter-slider");
	for (int i = 0; slider && !slider->isEnabled() && i < 30; ++i)
		QTest::qWait(100);
	check("live_fader", audioSource && slider && slider->isVisible() && slider->isEnabled());
	if (slider && slider->isEnabled()) {
		auto *viewport = audio->findChild<QScrollArea *>()->viewport();
		check("fader_fits",
		      viewport->rect().contains(QRect(slider->mapTo(viewport, QPoint()), slider->size())));
		slider->setFocus();
		QTest::keyClick(slider, Qt::Key_End);
		QTest::qWait(100);
		check("fader_writes_volume", audioSource && obs_source_get_volume(audioSource) > .9f);
		QTest::keyClick(slider, Qt::Key_PageDown);
		QTest::qWait(100);
	} else {
		check("fader_fits", false);
		check("fader_writes_volume", false);
	}
	obs_frontend_set_current_scene(obs_scene_get_source(first));
	if (cell0)
		QTest::mouseClick(cell0, Qt::LeftButton, Qt::NoModifier, cell0->rect().center());
	QTest::qWait(200);
	check("grid_scene_switch", current("Macro probe second"));
	const auto base = QString::fromUtf8(qgetenv("MACROLAYOUT_ROOT"));
	main->grab().save(base + "/native-live.png");
	audio->grab().save(base + "/native-audio.png");
	scenes->grab().save(base + "/native-scenes.png");
	check("enter_layout_edit", choose(scenes->widget(), "macro-strip-edit-layout"));
	auto *grip = scenes->findChild<QWidget *>("macro-layout-grip-other");
	check("edit_grip_and_safety", grip && grip->isVisible() && cell0 && !cell0->isEnabled());
	if (grip) {
		const QPoint p = grip->rect().center();
		const QPoint global = grip->mapToGlobal(p);
		QTest::mousePress(grip, Qt::LeftButton, Qt::ShiftModifier, p);
		QMouseEvent move(QEvent::MouseMove, QPointF(p + QPoint(72, 0)), QPointF(global + QPoint(72, 0)),
				 Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
		QCoreApplication::sendEvent(grip, &move);
		QMouseEvent release(QEvent::MouseButtonRelease, QPointF(p + QPoint(72, 0)),
				    QPointF(global + QPoint(72, 0)), Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
		QCoreApplication::sendEvent(grip, &release);
	}
	QTest::qWait(100);
	main->grab().save(base + "/native-edit.png");
	scenes->grab().save(base + "/native-scenes-edit.png");
	check("layout_undo", choose(scenes->widget(), "macro-layout-undo"));
	QTest::qWait(160);
	check("layout_redo", choose(scenes->widget(), "macro-layout-redo"));
	QTest::qWait(160);
	check("finish_edit", choose(scenes->widget(), "macro-strip-edit-layout"));
	QTest::qWait(450);
	QFile f(QString::fromUtf8(qgetenv("XDG_CONFIG_HOME")) +
		"/obs-studio/plugin_config/timeline/macro_buttons.json");
	bool persisted = false;
	if (f.open(QIODevice::ReadOnly)) {
		const auto root = QJsonDocument::fromJson(f.readAll()).object();
		for (const auto &strip : root["individualStrips"].toArray())
			if (strip.toObject()["id"].toString() == "scenes")
				for (const auto &button : strip.toObject()["buttons"].toArray())
					if (button.toObject()["id"].toString() == "other")
						persisted = button.toObject()["gapBefore"].toInt() == 72;
	}
	check("gap_saved", persisted);
	if (audioSource)
		obs_source_release(audioSource);
	obs_scene_release(first);
	obs_scene_release(second);
	blog(LOG_INFO, "MACROLAYOUT failures=%d", failures);
	main->close();
}

static void frontend(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		QTimer::singleShot(1500, qApp, probe);
}
bool obs_module_load(void)
{
	audioInfo.id = "macro_layout_probe_audio";
	audioInfo.type = OBS_SOURCE_TYPE_INPUT;
	audioInfo.output_flags = OBS_SOURCE_AUDIO;
	audioInfo.get_name = audioName;
	audioInfo.create = audioCreate;
	audioInfo.destroy = audioDestroy;
	obs_register_source(&audioInfo);
	obs_frontend_add_event_callback(frontend, nullptr);
	return true;
}
void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(frontend, nullptr);
}
