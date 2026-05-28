#include "location_data.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>

LocationData& LocationData::instance() {
    static LocationData instance;
    return instance;
}

void LocationData::updateFromJson(const json& j) {
    std::lock_guard<std::mutex> lock(mtx);
    
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream time_ss;
    time_ss << std::put_time(std::localtime(&now_time_t), "%H:%M:%S");
    time_str = time_ss.str();
    
    if (j.contains("latitude")) lat = j["latitude"].get<double>();
    else if (j.contains("lat")) lat = j["lat"].get<double>();
    
    if (j.contains("longitude")) lon = j["longitude"].get<double>();
    else if (j.contains("lon")) lon = j["lon"].get<double>();
    
    if (j.contains("altitude")) alt = j["altitude"].get<double>();
    else if (j.contains("alt")) alt = j["alt"].get<double>();
    
    if (j.contains("time")) timestamp = j["time"].get<long long>();
    else if (j.contains("timestamp")) timestamp = j["timestamp"].get<long long>();
    
    if (j.contains("rsrp")) rsrp = j["rsrp"].get<int>();
    if (j.contains("rsrq")) rsrq = j["rsrq"].get<int>();
    if (j.contains("rssi")) rssi = j["rssi"].get<int>();
    if (j.contains("frequency")) frequency = j["frequency"].get<int>();
    if (j.contains("ip_address")) ip_address = j["ip_address"].get<std::string>();
    if (j.contains("network_type")) network_type = j["network_type"].get<std::string>();
    if (j.contains("provider")) operator_name = j["provider"].get<std::string>();
    if (j.contains("device_id")) device_id = j["device_id"].get<std::string>();
    
    if (j.contains("cells") && j["cells"].is_array()) {
        current_cells.clear();
        
        for (const auto& cell_json : j["cells"]) {
            CellData cell;
            cell.pci = cell_json.value("pci", 0);
            cell.rsrp = cell_json.value("rsrp", -110);
            cell.rsrq = cell_json.value("rsrq", -20);
            cell.rssi = cell_json.value("rssi", -100);
            cell.earfcn = cell_json.value("earfcn", 0);
            cell.timestamp = timestamp;
            cell.time_str = time_str;
            current_cells.push_back(cell);
            
            pci_history[cell.pci].push_back(cell.rsrp);
            pci_names[cell.pci] = "PCI " + std::to_string(cell.pci);
            pci_track[cell.pci].push_back({lat, lon, cell.rsrp, cell.pci});
            
            while (pci_history[cell.pci].size() > max_history)
                pci_history[cell.pci].pop_front();
            while (pci_track[cell.pci].size() > max_history)
                pci_track[cell.pci].pop_front();
        }
    }
    
    lat_history.push_back(lat);
    lon_history.push_back(lon);
    alt_history.push_back(alt);
    timestamp_history.push_back(timestamp);
    rsrp_history.push_back(rsrp);
    rsrq_history.push_back(rsrq);
    rssi_history.push_back(rssi);
    time_history.push_back(time_str);
    network_type_history.push_back(network_type);
    frequency_history.push_back(frequency);
    
    while (lat_history.size() > max_history) {
        lat_history.pop_front();
        lon_history.pop_front();
        alt_history.pop_front();
        timestamp_history.pop_front();
        rsrp_history.pop_front();
        rsrq_history.pop_front();
        rssi_history.pop_front();
        time_history.pop_front();
        network_type_history.pop_front();
        frequency_history.pop_front();
    }
}

void LocationData::loadFromFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mtx);
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cout << "File not found: " << filename << std::endl;
        return;
    }
    
    std::string line;
    std::vector<json> all_data;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        try {
            all_data.push_back(json::parse(line));
        } catch (const std::exception& e) {
            std::cerr << "Error parsing line: " << e.what() << std::endl;
        }
    }
    file.close();
    
    std::cout << "Loaded " << all_data.size() << " records from " << filename << std::endl;
    if (all_data.empty()) return;
    
    lat_history.clear();
    lon_history.clear();
    alt_history.clear();
    timestamp_history.clear();
    rsrp_history.clear();
    rsrq_history.clear();
    rssi_history.clear();
    time_history.clear();
    network_type_history.clear();
    frequency_history.clear();
    pci_history.clear();
    pci_names.clear();
    pci_track.clear();
    current_cells.clear();
    
    for (const auto& j : all_data) {
        const json* src = &j;
        if (j.contains("location") && j["location"].is_object())
            src = &j["location"];
        
        float lat_val = src->value("latitude", src->value("lat", 0.0));
        float lon_val = src->value("longitude", src->value("lon", 0.0));
        float alt_val = src->value("altitude", src->value("alt", 0.0));
        long long ts = src->value("time", j.value("timestamp", 0LL));
        
        lat_history.push_back(lat_val);
        lon_history.push_back(lon_val);
        alt_history.push_back(alt_val);
        timestamp_history.push_back(ts);
        time_history.push_back(formatTimestamp(ts));
        
        int best_rsrp = -110, best_rsrq = -20, best_rssi = -100, best_freq = 0;
        std::string best_net = "Unknown";
        
        if (j.contains("telephony") && j["telephony"].is_array()) {
            bool first = true;
            for (const auto& cell_j : j["telephony"]) {
                if (cell_j.value("type", "") != "LTE") continue;
                
                const auto& id = cell_j.value("CellIdentityLte", json::object());
                const auto& sig = cell_j.value("CellSignalStrengthLte", json::object());
                
                int pci = id.value("PCI", -1);
                int cell_rsrp = sig.value("RSRP", -110);
                int cell_rsrq = sig.value("RSRQ", -20);
                int cell_rssi = sig.value("RSSI", -100);
                int earfcn = id.value("EARFCN", 0);
                
                if (pci < 0) continue;
                
                if (first || cell_rsrp > best_rsrp) {
                    best_rsrp = cell_rsrp;
                    best_rsrq = cell_rsrq;
                    best_rssi = cell_rssi;
                    best_freq = earfcn;
                    best_net = "LTE";
                    first = false;
                }
                
                pci_history[pci].push_back(cell_rsrp);
                pci_names[pci] = "PCI " + std::to_string(pci);
                pci_track[pci].push_back({lat_val, lon_val, cell_rsrp, pci});
            }
        } else if (j.contains("cells") && j["cells"].is_array()) {
            for (const auto& cell_j : j["cells"]) {
                int pci = cell_j.value("pci", -1);
                int cell_rsrp = cell_j.value("rsrp", -110);
                int cell_rsrq = cell_j.value("rsrq", -20);
                int cell_rssi = cell_j.value("rssi", -100);
                int earfcn = cell_j.value("earfcn", 0);
                
                if (pci < 0) continue;
                if (cell_rsrp > best_rsrp) {
                    best_rsrp = cell_rsrp;
                    best_rsrq = cell_rsrq;
                    best_rssi = cell_rssi;
                    best_freq = earfcn;
                    best_net = j.value("network_type", "Unknown");
                }
                
                pci_history[pci].push_back(cell_rsrp);
                pci_names[pci] = "PCI " + std::to_string(pci);
                pci_track[pci].push_back({lat_val, lon_val, cell_rsrp, pci});
            }
        } else {
            best_rsrp = j.value("rsrp", -110);
            best_rsrq = j.value("rsrq", -20);
            best_rssi = j.value("rssi", -100);
            best_freq = j.value("frequency", 0);
            best_net = j.value("network_type", "Unknown");
        }
        
        rsrp_history.push_back(best_rsrp);
        rsrq_history.push_back(best_rsrq);
        rssi_history.push_back(best_rssi);
        frequency_history.push_back(best_freq);
        network_type_history.push_back(best_net);
    }
    
    while (lat_history.size() > max_history) {
        lat_history.pop_front();
        lon_history.pop_front();
        alt_history.pop_front();
        timestamp_history.pop_front();
        rsrp_history.pop_front();
        rsrq_history.pop_front();
        rssi_history.pop_front();
        time_history.pop_front();
        network_type_history.pop_front();
        frequency_history.pop_front();
    }
    
    for (auto& [pci, hist] : pci_history) {
        while (hist.size() > max_history) hist.pop_front();
    }
    for (auto& [pci, track] : pci_track) {
        while (track.size() > max_history) track.pop_front();
    }
    
    if (!lat_history.empty()) {
        lat = lat_history.back();
        lon = lon_history.back();
        alt = alt_history.back();
        timestamp = timestamp_history.back();
        rsrp = rsrp_history.back();
        rsrq = rsrq_history.back();
        rssi = rssi_history.back();
        frequency = frequency_history.back();
        network_type = network_type_history.back();
        time_str = time_history.back();
    }
    
    const auto& last = all_data.back();
    ip_address = last.value("ip_address", "0.0.0.0");
    operator_name = last.value("provider", "Unknown");
    device_id = last.value("device_id", "Unknown");
    
    if (last.contains("cells") && last["cells"].is_array()) {
        for (const auto& cell_j : last["cells"]) {
            CellData cd;
            cd.pci = cell_j.value("pci", -1);
            cd.rsrp = cell_j.value("rsrp", -110);
            cd.rsrq = cell_j.value("rsrq", -20);
            cd.rssi = cell_j.value("rssi", -100);
            cd.earfcn = cell_j.value("earfcn", 0);
            cd.timestamp = timestamp;
            cd.time_str = time_str;
            if (cd.pci >= 0) current_cells.push_back(cd);
        }
    }
    
    std::cout << "Position: " << lat << ", " << lon << std::endl;
    std::cout << "History: " << lat_history.size() << " points" << std::endl;
    std::cout << "PCI count: " << pci_history.size() << std::endl;
    std::cout << "Last RSRP: " << rsrp << " dBm" << std::endl;
}

void LocationData::saveToFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mtx);
    
    json j;
    j["latitude"] = lat;
    j["longitude"] = lon;
    j["altitude"] = alt;
    j["timestamp"] = timestamp;
    j["rsrp"] = rsrp;
    j["rsrq"] = rsrq;
    j["rssi"] = rssi;
    j["frequency"] = frequency;
    j["network_type"] = network_type;
    j["ip_address"] = ip_address;
    j["provider"] = operator_name;
    j["device_id"] = device_id;
    j["cells"] = json::array();
    
    for (const auto& cell : current_cells) {
        json cj;
        cj["pci"] = cell.pci;
        cj["rsrp"] = cell.rsrp;
        cj["rsrq"] = cell.rsrq;
        cj["rssi"] = cell.rssi;
        cj["earfcn"] = cell.earfcn;
        j["cells"].push_back(cj);
    }
    
    std::ofstream file(filename, std::ios::app);
    if (file.is_open()) {
        file << j.dump() << std::endl;
        file.close();
    }
}