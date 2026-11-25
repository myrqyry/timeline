#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QApplication>
#include <QMainWindow>
#include <QPushButton>
#include <QTimer>
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <util/platform.h>
#include <cstring>

OBS_DECLARE_MODULE()
static obs_source_t *provider = nullptr;
static obs_scene_t *first = nullptr, *second = nullptr, *third = nullptr;
static QPushButton *play = nullptr;
static QString base;
static const char *uuid = "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa";
static void check(const char *label, bool pass)
{
	blog(pass ? LOG_INFO : LOG_ERROR, "DATACUE %s=%d", label, pass);
}
static bool current(const char *name)
{
	auto *s = obs_frontend_get_current_scene();
	bool result = s && !strcmp(obs_source_get_name(s), name);
	if (s)
		obs_source_release(s);
	return result;
}
static obs_data_t *settings(const QString &path, bool share)
{
	QJsonObject request{{"url", base + path},        {"url_or_file", "url"},
			    {"method", "GET"},           {"body", ""},
			    {"inputs", QJsonArray{}},    {"headers", QJsonObject{}},
			    {"output_type", "JSON"},     {"output_json_pointer", "/value"},
			    {"output_xpath", ""},        {"output_xquery", ""},
			    {"output_regex", ""},        {"output_regex_flags", ""},
			    {"output_regex_group", "0"}, {"fail_on_http_error", true}};
	auto *s = obs_data_create();
	obs_data_set_string(s, "request_data", QJsonDocument(request).toJson(QJsonDocument::Compact).constData());
	obs_data_set_string(s, "output_mapping_data", "[]");
	obs_data_set_bool(s, "share_parsed_data", share);
	obs_data_set_bool(s, "run_while_not_visible", true);
	obs_data_set_int(s, "update_timer", 10000);
	return s;
}
static void update(const QString &path, bool share)
{
	if (!provider)
		return;
	auto *s = settings(path, share);
	obs_source_update(provider, s);
	obs_data_release(s);
}
static void probe()
{
	const bool installed = qgetenv("DATACUE_PROVIDER") == "yes";
	QFile f(QString::fromUtf8(qgetenv("DATACUE_ROOT")) + "/port");
	f.open(QIODevice::ReadOnly);
	base = "http://127.0.0.1:" + QString::fromUtf8(f.readAll());
	first = obs_scene_create("Cue first");
	second = obs_scene_create("Cue second");
	third = obs_scene_create("Cue third");
	obs_frontend_set_current_scene(obs_scene_get_source(first));
	if (installed) {
		auto *s = settings("/high", true);
		auto *entry = obs_data_create();
		obs_data_set_string(entry, "id", "url_source");
		obs_data_set_string(entry, "name", "Cue provider");
		obs_data_set_string(entry, "uuid", uuid);
		obs_data_set_obj(entry, "settings", s);
		provider = obs_load_source(entry);
		obs_data_release(entry);
		obs_data_release(s);
		check("provider_uuid", provider && !strcmp(obs_source_get_uuid(provider), uuid));
	} else
		check("provider_missing", !obs_get_module("obs-urlsource"));
	auto *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	play = main->findChild<QPushButton *>("timeline-play-btn");
	check("play_button", play != nullptr);
	if (!play) {
		main->close();
		return;
	}
	QTimer::singleShot(1000, qApp, [] { play->click(); });
	QTimer::singleShot(2100, qApp,
			   [installed] { check("true_cue", current(installed ? "Cue second" : "Cue first")); });
	QTimer::singleShot(3100, qApp, [installed] {
		check("false_cue", current(installed ? "Cue second" : "Cue first"));
		update("/high", false);
	});
	QTimer::singleShot(4200, qApp, [installed] {
		check("optout_cue", current(installed ? "Cue second" : "Cue first"));
		update("/high", true);
	});
	QTimer::singleShot(5300, qApp, [installed] {
		check("stale_cue", current(installed ? "Cue second" : "Cue first"));
		update("/bool", true);
	});
	QTimer::singleShot(6400, qApp, [installed] {
		check("boolean_cue", current(installed ? "Cue third" : "Cue first"));
		if (provider)
			obs_source_remove(provider);
	});
	QTimer::singleShot(7500, qApp,
			   [installed] { check("removed_cue", current(installed ? "Cue third" : "Cue first")); });
	QTimer::singleShot(8500, qApp, [] {
		check("ordinary_cue", current("Cue second"));
		if (provider)
			obs_source_release(provider);
		obs_scene_release(first);
		obs_scene_release(second);
		obs_scene_release(third);
		static_cast<QMainWindow *>(obs_frontend_get_main_window())->close();
	});
}
static void frontend(enum obs_frontend_event e, void *)
{
	if (e == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		QTimer::singleShot(1800, qApp, probe);
}
bool obs_module_load(void)
{
	obs_frontend_add_event_callback(frontend, nullptr);
	return true;
}
void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(frontend, nullptr);
}
