#ifndef STATION_WEB_SERVER_H
#define STATION_WEB_SERVER_H

#include <string>
#include <functional>

class StationWebServer {
public:
    static StationWebServer& GetInstance();

    void Start();
    void Stop();
    bool IsRunning();
    std::string GetUrl();

private:
    StationWebServer() = default;
    void* server_ = nullptr;
    bool running_ = false;
};

#endif
