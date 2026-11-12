#include <obs-websocket-api.h>
#include <util/bmem.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

QJsonObject g_websocket_last_request;
int g_websocket_request_count = 0;
bool g_websocket_available = true;
bool g_websocket_dsk_success = true;
unsigned int g_websocket_status = 100;

extern "C" {

struct obs_websocket_request_response *obs_websocket_call_request(const char *request_type, obs_data_t *request_data)
{
	++g_websocket_request_count;
	if (!g_websocket_available)
		return nullptr;
	g_websocket_last_request = QJsonDocument::fromJson(obs_data_get_json(request_data)).object();
	g_websocket_last_request["internalRequest"] = QString::fromUtf8(request_type);
	auto *response = static_cast<obs_websocket_request_response *>(bzalloc(sizeof(obs_websocket_request_response)));
	response->status_code = g_websocket_status;
	const QJsonObject body{
		{"responseData", QJsonObject{{"success", g_websocket_dsk_success}, {"error", "Missing DSK scene"}}}};
	const QByteArray serialized = QJsonDocument(body).toJson(QJsonDocument::Compact);
	response->response_data = bstrdup(serialized.constData());
	return response;
}

void obs_websocket_request_response_free(struct obs_websocket_request_response *response)
{
	if (!response)
		return;
	bfree(response->comment);
	bfree(response->response_data);
	bfree(response);
}

obs_websocket_vendor obs_websocket_register_vendor(const char *vendor_name)
{
	(void)vendor_name;
	return (void *)0x12345678;
}

void obs_websocket_vendor_register_request(obs_websocket_vendor vendor, const char *request_type,
					   obs_websocket_vendor_request_callback callback, void *priv_data)
{
	(void)vendor;
	(void)request_type;
	(void)callback;
	(void)priv_data;
}

void obs_websocket_vendor_emit_event(obs_websocket_vendor vendor, const char *event_name, obs_data_t *event_data)
{
	(void)vendor;
	(void)event_name;
	(void)event_data;
}
}
