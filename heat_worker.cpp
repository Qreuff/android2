#include "heat_worker.h"
#include <iostream>

HeatWorker& HeatWorker::instance() {
    static HeatWorker instance;
    return instance;
}

HeatWorker::HeatWorker() {
    std::cout << "Initializing heat worker..." << std::endl;
    worker_thread_ = std::thread(&HeatWorker::workerThread, this);
    std::cout << "Heat worker started" << std::endl;
}

HeatWorker::~HeatWorker() {
    std::cout << "Shutting down heat worker..." << std::endl;
    running_ = false;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    std::cout << "Heat worker shut down complete" << std::endl;
}

void HeatWorker::addPoints(const std::vector<HeatPoint>& pts) {
    if (pts.empty()) return;
    
    std::lock_guard<std::mutex> lk(heat_mtx_);
    heat_queue_.insert(heat_queue_.end(), pts.begin(), pts.end());
}

void HeatWorker::getTracks(std::map<int, std::deque<TrackPoint>>& tracks) {
    std::lock_guard<std::mutex> lk(heat_track_mtx_);
    tracks = heat_tracks_;
}

void HeatWorker::clearTracks() {
    std::lock_guard<std::mutex> lk(heat_track_mtx_);
    heat_tracks_.clear();
    std::cout << "Heat tracks cleared" << std::endl;
}

std::vector<int> HeatWorker::getPCIList() {
    std::lock_guard<std::mutex> lk(heat_track_mtx_);
    std::vector<int> pci_list;
    for (const auto& [pci, _] : heat_tracks_) {
        pci_list.push_back(pci);
    }
    std::sort(pci_list.begin(), pci_list.end());
    pci_list.erase(std::unique(pci_list.begin(), pci_list.end()), pci_list.end());
    return pci_list;
}

size_t HeatWorker::getTotalPoints() {
    std::lock_guard<std::mutex> lk(heat_track_mtx_);
    size_t total = 0;
    for (const auto& [pci, points] : heat_tracks_) {
        total += points.size();
    }
    return total;
}

void HeatWorker::workerThread() {
    while (running_) {
        std::vector<HeatPoint> batch;
        {
            std::lock_guard<std::mutex> lk(heat_mtx_);
            batch.swap(heat_queue_);
        }
        
        if (!batch.empty()) {
            std::lock_guard<std::mutex> lk(heat_track_mtx_);
            for (const auto& p : batch) {
                auto& dq = heat_tracks_[p.pci];
                dq.push_back({p.lat, p.lon, p.rsrp, p.pci});
                while (dq.size() > HEAT_MAX) {
                    dq.pop_front();
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}