#ifndef STATION_WEB_SERVER_H
#define STATION_WEB_SERVER_H

#include <string>
#include <atomic>
#include <esp_http_server.h>

class StationWebServer {
public:
    static StationWebServer& GetInstance();

    void Start();
    void Stop();
    bool IsRunning();
    std::string GetUrl();

private:
    StationWebServer() = default;
    httpd_handle_t server_ = nullptr;
    std::atomic<bool> running_{false};
};

#endif
