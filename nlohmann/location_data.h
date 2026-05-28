#pragma once
#include "config.h"
#include <nlohmann/json.hpp>
#include <mutex>
#include <deque>
#include <map>
#include <vector>
#include <string>
#include <atomic>

using json = nlohmann::json;

struct CellData {
    int pci = 0;
    int rsrp = -110;
    int rsrq = -20;
    int rssi = -100;
    int earfcn = 0;
    long long timestamp = 0;
    std::string time_str;
};

struct TrackPoint {
    float lat;
    float lon;
    int rsrp;
    int pci;
};

class LocationData {
public:
    static LocationData& instance();
    
    void updateFromJson(const json& j);
    void loadFromFile(const std::string& filename = LOG_FILE);
    void saveToFile(const std::string& filename = LOG_FILE);
    
    float lat = 0.0f;
    float lon = 0.0f;
    float alt = 0.0f;
    long long timestamp = 0;
    std::string time_str;
    int rsrp = -110;
    int rsrq = -20;
    int rssi = -100;
    int frequency = 0;
    std::string ip_address = "0.0.0.0";
    std::string network_type = "Unknown";
    std::string operator_name = "Unknown";
    std::string device_id = "Unknown";
    
    std::deque<float> lat_history;
    std::deque<float> lon_history;
    std::deque<float> alt_history;
    std::deque<long long> timestamp_history;
    std::deque<int> rsrp_history;
    std::deque<int> rsrq_history;
    std::deque<int> rssi_history;
    std::deque<std::string> time_history;
    std::deque<std::string> network_type_history;
    std::deque<int> frequency_history;
    
    std::vector<CellData> current_cells;
    std::map<int, std::deque<int>> pci_history;
    std::map<int, std::deque<TrackPoint>> pci_track;
    std::map<int, std::string> pci_names;
    
    std::atomic<bool> is_running{true};
    std::mutex mtx;
    
    static constexpr size_t max_history = MAX_HISTORY;

private:
    LocationData() = default;
};