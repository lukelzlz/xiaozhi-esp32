#include "station_web_server.h"
#include "board.h"
#include "settings.h"
#include "system_info.h"
#include "audio/audio_codec.h"
#include "display.h"
#include "led/led.h"
#include "backlight.h"

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_app_desc.h>
#include <cJSON.h>
#include <cstring>
#include <wifi_station.h>

static const char *TAG = "StationWebServer";

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
    return httpd_resp_send(req, json.c_str(), json.length());
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

    auto json_str = cJSON_PrintUnformatted(root);
    std::string json(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), json.length());
}

static esp_err_t api_settings_get_handler(httpd_req_t *req) {
    auto root = cJSON_CreateObject();

    Settings wifi_settings("wifi", false);
    cJSON_AddStringToObject(root, "ota_url", wifi_settings.GetString("ota_url", "").c_str());

    Settings audio_settings("audio", false);
    cJSON_AddNumberToObject(root, "volume", audio_settings.GetInt("output_volume", 70));

    auto json_str = cJSON_PrintUnformatted(root);
    std::string json(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), json.length());
}

static esp_err_t api_settings_post_handler(httpd_req_t *req) {
    char buf[1024] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *ota_url = cJSON_GetObjectItem(root, "ota_url");
    if (cJSON_IsString(ota_url) && strlen(ota_url->valuestring) > 0) {
        Settings settings("wifi", true);
        settings.SetString("ota_url", ota_url->valuestring);
        ESP_LOGI(TAG, "OTA URL updated to: %s", ota_url->valuestring);
    }

    cJSON *volume = cJSON_GetObjectItem(root, "volume");
    if (cJSON_IsNumber(volume)) {
        Settings settings("audio", true);
        settings.SetInt("output_volume", volume->valueint);

        auto codec = Board::GetInstance().GetAudioCodec();
        if (codec) {
            codec->SetOutputVolume(volume->valueint);
        }
        ESP_LOGI(TAG, "Volume updated to: %d", volume->valueint);
    }

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"success\":true}", 15);
}

static esp_err_t api_reboot_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", 15);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t api_wifi_reset_handler(httpd_req_t *req) {
    {
        Settings settings("wifi", true);
        settings.SetInt("force_ap", 1);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", 15);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

void StationWebServer::Start() {
    if (running_) {
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;

    if (httpd_start((httpd_handle_t*)&server_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server");
        return;
    }

    httpd_uri_t index_uri = {.uri = "/", .method = HTTP_GET, .handler = index_handler};
    httpd_uri_t status_uri = {.uri = "/api/status", .method = HTTP_GET, .handler = api_status_handler};
    httpd_uri_t info_uri = {.uri = "/api/info", .method = HTTP_GET, .handler = api_info_handler};
    httpd_uri_t settings_get_uri = {.uri = "/api/settings", .method = HTTP_GET, .handler = api_settings_get_handler};
    httpd_uri_t settings_post_uri = {.uri = "/api/settings", .method = HTTP_POST, .handler = api_settings_post_handler};
    httpd_uri_t reboot_uri = {.uri = "/api/reboot", .method = HTTP_POST, .handler = api_reboot_handler};
    httpd_uri_t wifi_reset_uri = {.uri = "/api/wifi-reset", .method = HTTP_POST, .handler = api_wifi_reset_handler};

    httpd_register_uri_handler((httpd_handle_t)server_, &index_uri);
    httpd_register_uri_handler((httpd_handle_t)server_, &status_uri);
    httpd_register_uri_handler((httpd_handle_t)server_, &info_uri);
    httpd_register_uri_handler((httpd_handle_t)server_, &settings_get_uri);
    httpd_register_uri_handler((httpd_handle_t)server_, &settings_post_uri);
    httpd_register_uri_handler((httpd_handle_t)server_, &reboot_uri);
    httpd_register_uri_handler((httpd_handle_t)server_, &wifi_reset_uri);

    running_ = true;
    ESP_LOGI(TAG, "Web server started on port 80");
}

void StationWebServer::Stop() {
    if (!running_) return;
    httpd_stop((httpd_handle_t)server_);
    server_ = nullptr;
    running_ = false;
}

bool StationWebServer::IsRunning() { return running_; }

std::string StationWebServer::GetUrl() {
    return "http://" + WifiStation::GetInstance().GetIpAddress();
}

StationWebServer& StationWebServer::GetInstance() {
    static StationWebServer instance;
    return instance;
}
