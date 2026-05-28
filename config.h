#pragma once
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <imgui.h>
#include <implot.h>
#include <cmath>
#include <string>
#include <chrono>
#include <sstream>
#include <iomanip>

constexpr float NOVOSIBIRSK_LAT = 55.0304f;
constexpr float NOVOSIBIRSK_LON = 82.9202f;
constexpr int TILE_SIZE = 256;
constexpr int DEFAULT_ZOOM = 14;
constexpr const char* CACHE_DIR = "osm_cache";
constexpr const char* LOG_FILE = "location.json";
constexpr int MAX_HISTORY = 200;

constexpr float RSRP_MIN = -110.0f;
constexpr float RSRP_MAX = -80.0f;

inline int lonToTileX(double lon, int zoom) {
    double n = 1 << zoom;
    return static_cast<int>(floor((lon + 180.0) / 360.0 * n));
}

inline int latToTileY(double lat, int zoom) {
    double n = 1 << zoom;
    double lat_rad = lat * M_PI / 180.0;
    double y = log(tan(lat_rad) + 1.0 / cos(lat_rad));
    return static_cast<int>(floor((1.0 - y / M_PI) / 2.0 * n));
}

inline double lonToPixelX(double lon, int zoom) {
    return lonToTileX(lon, zoom) * TILE_SIZE;
}

inline double latToPixelY(double lat, int zoom) {
    return latToTileY(lat, zoom) * TILE_SIZE;
}

inline double lonToX(double lon, int zoom) {
    return (lon + 180.0) / 360.0 * (1 << zoom) * TILE_SIZE;
}

inline double latToY(double lat, int zoom) {
    double lat_rad = lat * M_PI / 180.0;
    double y = log(tan(lat_rad) + 1.0 / cos(lat_rad));
    return (1.0 - y / M_PI) / 2.0 * (1 << zoom) * TILE_SIZE;
}

inline ImU32 rsrpToColor(int rsrp) {
    float t = (std::max(RSRP_MIN, std::min(RSRP_MAX, static_cast<float>(rsrp))) - RSRP_MIN) 
              / (RSRP_MAX - RSRP_MIN);
    float r, g, b;
    
    if (t < 0.25f) {
        float s = t / 0.25f;
        r = 0; g = s * 0.5f; b = 1.0f;
    } else if (t < 0.5f) {
        float s = (t - 0.25f) / 0.25f;
        r = 0; g = 0.5f + s * 0.5f; b = 1.0f - s;
    } else if (t < 0.75f) {
        float s = (t - 0.5f) / 0.25f;
        r = s; g = 1.0f; b = 0;
    } else {
        float s = (t - 0.75f) / 0.25f;
        r = 1.0f; g = 1.0f - s; b = 0;
    }
    
    return IM_COL32(static_cast<int>(r*255), static_cast<int>(g*255), 
                    static_cast<int>(b*255), 50);
}

inline std::string getSignalQuality(int rsrp) {
    if (rsrp > -80) return "Excellent";
    if (rsrp > -90) return "Good";
    if (rsrp > -100) return "Fair";
    if (rsrp > -110) return "Poor";
    return "Very Poor";
}

inline std::string formatTimestamp(long long ms_timestamp) {
    if (ms_timestamp <= 0) return "";
    auto tp = std::chrono::system_clock::from_time_t(ms_timestamp / 1000);
    auto tt = std::chrono::system_clock::to_time_t(tp);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&tt), "%H:%M:%S");
    return ss.str();
}

static const ImVec4 signal_colors[] = {
    ImVec4(0.0f, 0.0f, 1.0f, 1.0f),
    ImVec4(0.0f, 0.5f, 1.0f, 1.0f),
    ImVec4(0.0f, 1.0f, 0.5f, 1.0f),
    ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
    ImVec4(0.6f, 1.0f, 0.0f, 1.0f),
    ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
    ImVec4(1.0f, 0.4f, 0.0f, 1.0f),
    ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
};

struct TrackPoint {
    float lat;
    float lon;
    int rsrp;
    int pci;
};