#pragma once
#include "config.h"
#include <mutex>
#include <deque>
#include <map>
#include <vector>
#include <atomic>
#include <thread>
#include <algorithm>

struct HeatPoint {
    int pci;
    float lat;
    float lon;
    int rsrp;
};

struct TrackPoint {
    float lat;
    float lon;
    int rsrp;
    int pci;
};

class HeatWorker {
public:
    static HeatWorker& instance();
    
    void addPoints(const std::vector<HeatPoint>& pts);
    void getTracks(std::map<int, std::deque<TrackPoint>>& tracks);
    void clearTracks();
    std::vector<int> getPCIList();
    size_t getTotalPoints();

private:
    HeatWorker();
    ~HeatWorker();
    HeatWorker(const HeatWorker&) = delete;
    HeatWorker& operator=(const HeatWorker&) = delete;
    
    void workerThread();

    std::mutex heat_mtx_;
    std::vector<HeatPoint> heat_queue_;
    std::mutex heat_track_mtx_;
    std::map<int, std::deque<TrackPoint>> heat_tracks_;
    std::atomic<bool> running_{true};
    std::thread worker_thread_;
    
    static constexpr size_t HEAT_MAX = 5000;
};