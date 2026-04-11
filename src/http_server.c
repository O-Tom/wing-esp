#include "esp_http_server.h"
#include "esp_log.h"

#include "http_server.h"
#include "wing_esp.h"

// =====================================================================
// HTTP endpoint handlers
// =====================================================================

static esp_err_t uri_generate_204(httpd_req_t *req)
{
	ESP_LOGI(LOG_TAG, "HTTP endpoint accessed: %s", req->uri);
	httpd_resp_set_status(req, "204 No Content");
	httpd_resp_send(req, NULL, 0);
	return ESP_OK;
}

static esp_err_t uri_hotspot_detect(httpd_req_t *req)
{
	ESP_LOGI(LOG_TAG, "HTTP endpoint accessed: %s", req->uri);
	static const char body[] = "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>";
	httpd_resp_set_type(req, "text/html");
	httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
	return ESP_OK;
}

static esp_err_t uri_ncsi(httpd_req_t *req)
{
	ESP_LOGI(LOG_TAG, "HTTP endpoint accessed: %s", req->uri);
	static const char body[] = "Microsoft NCSI";
	httpd_resp_set_type(req, "text/plain");
	httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
	return ESP_OK;
}

static esp_err_t uri_index(httpd_req_t *req)
{
	ESP_LOGI(LOG_TAG, "HTTP endpoint accessed: %s", req->uri);
	static const char body[] =
		"<html><head><title>WingESP</title></head><body>"
		"<h1>WingESP captive LAN</h1>"
		"<p>DNS interception and connectivity check endpoints are active.</p>"
		"</body></html>";
	httpd_resp_set_type(req, "text/html");
	httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
	return ESP_OK;
}

// =====================================================================
// Public API
// =====================================================================

esp_err_t http_server_start(void)
{
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	httpd_handle_t server = NULL;

	if (httpd_start(&server, &config) != ESP_OK) {
		ESP_LOGE(LOG_TAG, "HTTP server start failed");
		return ESP_FAIL;
	}

	httpd_uri_t u1 = {.uri = "/generate_204", .method = HTTP_GET, .handler = uri_generate_204, .user_ctx = NULL};
	httpd_uri_t u2 = {.uri = "/gen_204", .method = HTTP_GET, .handler = uri_generate_204, .user_ctx = NULL};
	httpd_uri_t u3 = {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = uri_hotspot_detect, .user_ctx = NULL};
	httpd_uri_t u4 = {.uri = "/ncsi.txt", .method = HTTP_GET, .handler = uri_ncsi, .user_ctx = NULL};
	httpd_uri_t u5 = {.uri = "/", .method = HTTP_GET, .handler = uri_index, .user_ctx = NULL};

	httpd_register_uri_handler(server, &u1);
	httpd_register_uri_handler(server, &u2);
	httpd_register_uri_handler(server, &u3);
	httpd_register_uri_handler(server, &u4);
	httpd_register_uri_handler(server, &u5);

	ESP_LOGI(LOG_TAG, "HTTP server started on port %d", config.server_port);
	return ESP_OK;
}
