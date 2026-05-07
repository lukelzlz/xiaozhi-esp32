#include "station_web_server.h"
#include "board.h"
#include "settings.h"
#include "system_info.h"
#include "audio/audio_codec.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_app_desc.h>
#include <esp_random.h>
#include <cJSON.h>
#include <cstring>
#include <algorithm>
#include <wifi_station.h>

static const char *TAG = "StationWebServer";

static constexpr size_t kMaxBodyLen = 2048;
static constexpr int kMaxOtaUrlLen = 256;

static std::string GetOrGenerateToken() {
    Settings s("web_auth", true);
    std::string token = s.GetString("token", "");
    if (token.empty()) {
        uint32_t rnd;
        esp_fill_random(&rnd, sizeof(rnd));
        char buf[16];
        snprintf(buf, sizeof(buf), "%08lx", (unsigned long)rnd);
        token = buf;
        s.SetString("token", token);
        ESP_LOGI(TAG, "Generated web auth token: %s", token.c_str());
    }
    return token;
}

static bool CheckAuth(httpd_req_t *req) {
    std::string expected = GetOrGenerateToken();
    char provided[64] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-Auth-Token", provided, sizeof(provided)) != ESP_OK ||
        provided[0] == '\0') {
        char query[64] = {0};
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
            char val[64] = {0};
            if (httpd_query_key_value(query, "token", val, sizeof(val)) == ESP_OK) {
                strncpy(provided, val, sizeof(provided) - 1);
            }
        }
    }
    if (provided[0] == '\0' || expected != provided) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"unauthorized\"}");
        return false;
    }
    return true;
}

static int ReadBody(httpd_req_t *req, char *buf, size_t buf_size) {
    size_t content_len = req->content_len;
    if (content_len > kMaxBodyLen) {
        return -1;
    }
    int total = 0;
    size_t remaining = content_len;
    while (remaining > 0 && (size_t)total < buf_size - 1) {
        size_t to_read = std::min(remaining, buf_size - 1 - (size_t)total);
        int ret = httpd_req_recv(req, buf + total, to_read);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            break;
        }
        total += ret;
        remaining -= ret;
    }
    buf[total] = '\0';
    return total;
}

static esp_err_t SendJson(httpd_req_t *req, cJSON *root) {
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json_str);
    cJSON_free(json_str);
    return err;
}

static esp_err_t index_handler(httpd_req_t *req) {
    extern const char index_html_start[] asm("_binary_station_web_html_start");
    extern const char index_html_end[] asm("_binary_station_web_html_end");
    size_t len = index_html_end - index_html_start;
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html_start, len);
}

static esp_err_t api_status_handler(httpd_req_t *req) {
    auto& board = Board::GetInstance();
    std::string json = board.GetDeviceStatusJson();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json.c_str());
}

static esp_err_t api_info_handler(httpd_req_t *req) {
    auto& board = Board::GetInstance();
    auto app_desc = esp_app_get_description();

    auto root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version", app_desc->version);
    cJSON_AddStringToObject(root, "project", app_desc->project_name);
    cJSON_AddStringToObject(root, "idf_version", app_desc->idf_ver);
    cJSON_AddStringToObject(root, "board_type", BOARD_TYPE);
    cJSON_AddStringToObject(root, "mac_address", SystemInfo::GetMacAddress().c_str());
    cJSON_AddStringToObject(root, "uuid", board.GetUuid().c_str());
    cJSON_AddStringToObject(root, "auth_token", GetOrGenerateToken().c_str());

    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        cJSON_AddStringToObject(root, "ssid", (const char*)ap.ssid);
        cJSON_AddNumberToObject(root, "rssi", ap.rssi);
        cJSON_AddNumberToObject(root, "channel", ap.primary);
    }

    Settings settings("wifi", false);
    std::string ota_url = settings.GetString("ota_url");
    if (ota_url.empty()) {
        ota_url = CONFIG_OTA_URL;
    }
    cJSON_AddStringToObject(root, "ota_url", ota_url.c_str());

    return SendJson(req, root);
}

static esp_err_t api_settings_get_handler(httpd_req_t *req) {
    if (!CheckAuth(req)) return ESP_FAIL;

    auto root = cJSON_CreateObject();
    Settings wifi_settings("wifi", false);
    cJSON_AddStringToObject(root, "ota_url", wifi_settings.GetString("ota_url", "").c_str());

    Settings audio_settings("audio", false);
    cJSON_AddNumberToObject(root, "volume", audio_settings.GetInt("output_volume", 70));

    return SendJson(req, root);
}

static bool IsValidHttpUrl(const char *url) {
    if (!url || url[0] == '\0') return false;
    return (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
}

static esp_err_t api_settings_post_handler(httpd_req_t *req) {
    if (!CheckAuth(req)) return ESP_FAIL;

    char *buf = (char *)malloc(kMaxBodyLen + 1);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    int body_len = ReadBody(req, buf, kMaxBodyLen + 1);
    if (body_len < 0) {
        free(buf);
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"payload too large\"}");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"invalid json\"}");
        return ESP_FAIL;
    }

    cJSON *ota_url = cJSON_GetObjectItem(root, "ota_url");
    if (cJSON_IsString(ota_url) && strlen(ota_url->valuestring) > 0) {
        if (!IsValidHttpUrl(ota_url->valuestring) || strlen(ota_url->valuestring) > kMaxOtaUrlLen) {
            cJSON_Delete(root);
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"error\":\"invalid ota_url\"}");
            return ESP_FAIL;
        }
        Settings settings("wifi", true);
        settings.SetString("ota_url", ota_url->valuestring);
        ESP_LOGI(TAG, "OTA URL updated to: %s", ota_url->valuestring);
    }

    cJSON *volume = cJSON_GetObjectItem(root, "volume");
    if (cJSON_IsNumber(volume)) {
        int vol = std::clamp(volume->valueint, 0, 100);
        Settings settings("audio", true);
        settings.SetInt("output_volume", vol);

        auto codec = Board::GetInstance().GetAudioCodec();
        if (codec) {
            codec->SetOutputVolume(vol);
        }
        ESP_LOGI(TAG, "Volume updated to: %d", vol);
    }

    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"success\":true}");
}

static esp_err_t api_reboot_handler(httpd_req_t *req) {
    if (!CheckAuth(req)) return ESP_FAIL;

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static esp_err_t api_wifi_reset_handler(httpd_req_t *req) {
    if (!CheckAuth(req)) return ESP_FAIL;

    {
        Settings settings("wifi", true);
        settings.SetInt("force_ap", 1);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static bool RegisterUri(httpd_handle_t server, const char *uri, httpd_method_t method,
                        esp_err_t (*handler)(httpd_req_t *)) {
    httpd_uri_t uri_def = {};
    uri_def.uri = uri;
    uri_def.method = method;
    uri_def.handler = handler;
    esp_err_t ret = httpd_register_uri_handler(server, &uri_def);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register %s %s", http_method_str(method), uri);
    }
    return ret == ESP_OK;
}

void StationWebServer::Start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.max_open_sockets = 5;
    config.stack_size = 12288;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;

    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server");
        running_ = false;
        return;
    }

    bool ok = true;
    ok &= RegisterUri(server_, "/",               HTTP_GET,  index_handler);
    ok &= RegisterUri(server_, "/api/status",     HTTP_GET,  api_status_handler);
    ok &= RegisterUri(server_, "/api/info",       HTTP_GET,  api_info_handler);
    ok &= RegisterUri(server_, "/api/settings",   HTTP_GET,  api_settings_get_handler);
    ok &= RegisterUri(server_, "/api/settings",   HTTP_POST, api_settings_post_handler);
    ok &= RegisterUri(server_, "/api/reboot",     HTTP_POST, api_reboot_handler);
    ok &= RegisterUri(server_, "/api/wifi-reset", HTTP_POST, api_wifi_reset_handler);

    if (!ok) {
        ESP_LOGE(TAG, "Some URI handlers failed to register, stopping server");
        httpd_stop(server_);
        server_ = nullptr;
        running_ = false;
        return;
    }

    ESP_LOGI(TAG, "Web server started on port 80");
}

void StationWebServer::Stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }
    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
    }
}

bool StationWebServer::IsRunning() { return running_.load(); }

std::string StationWebServer::GetUrl() {
    std::string ip = WifiStation::GetInstance().GetIpAddress();
    if (ip.empty()) return "";
    return "http://" + ip;
}

StationWebServer& StationWebServer::GetInstance() {
    static StationWebServer instance;
    return instance;
}
