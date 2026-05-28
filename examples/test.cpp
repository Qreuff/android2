#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <iostream>
#include <cmath>
#include <mutex>
#include <string>
#include <fstream>
#include <vector>
#include <deque>
#include <thread>
#include <atomic>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <curl/curl.h>
#include <png.h>
#include <map>
#include <set>
#include <algorithm>
#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

using json = nlohmann::json;

double lonToX(double lon, int zoom) {
    return (lon + 180.0) / 360.0 * (1 << zoom) * 256;
}

double latToY(double lat, int zoom) {
    double lat_rad = lat * M_PI / 180.0;
    double y = log(tan(M_PI / 4.0 + lat_rad / 2.0));
    return (1.0 - y / M_PI) / 2.0 * (1 << zoom) * 256;
}

struct TileTexture {
    GLuint texture_id = 0;
    int width = 0;
    int height = 0;
    bool loaded = false;
    std::vector<unsigned char> pixel_data;
};

struct CellData {
    int pci = 0;
    int rsrp = -110;
    int rsrq = -20;
    int rssi = -100;
    int earfcn = 0;
    long long timestamp = 0;
    std::string time_str;
};

struct Tile {
    int x = 0;
    int y = 0;
    int zoom = 0;
    TileTexture texture;
    bool loaded = false;
    bool is_loading = false;
    bool needs_upload = false;
    std::chrono::steady_clock::time_point last_used;
};

class OSMTileManager {
private:
    std::string cache_dir;
    std::map<std::string, Tile> tiles;
    std::mutex tiles_mutex;
    CURL* curl;
    std::atomic<bool> running{true};
    std::thread download_thread;
    
    std::string getTileKey(int x, int y, int zoom) {
        return std::to_string(zoom) + "_" + std::to_string(x) + "_" + std::to_string(y);
    }
    
    std::string getTilePath(int x, int y, int zoom) {
        return cache_dir + "/" + std::to_string(zoom) + "/" + std::to_string(x) + "_" + std::to_string(y) + ".png";
    }
    
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* data) {
        size_t totalSize = size * nmemb;
        data->append((char*)contents, totalSize);
        return totalSize;
    }
    
    bool downloadTile(int x, int y, int zoom, const std::string& save_path) {
        std::string url = "https://tile.openstreetmap.org/" + 
                         std::to_string(zoom) + "/" + 
                         std::to_string(x) + "/" + 
                         std::to_string(y) + ".png";
        
        std::string response_data;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &OSMTileManager::WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "GPS-Network-Monitor/1.0");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        
        CURLcode res = curl_easy_perform(curl);
        
        if (res == CURLE_OK && !response_data.empty()) {
            try {
                std::filesystem::create_directories(std::filesystem::path(save_path).parent_path());
                std::ofstream file(save_path, std::ios::binary);
                if (file.is_open()) {
                    file.write(response_data.c_str(), response_data.size());
                    file.close();
                    return true;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error saving tile: " << e.what() << std::endl;
            }
        }
        return false;
    }
    
    bool loadPNGToMemory(const std::string& file_path, std::vector<unsigned char>& pixel_data, int& width, int& height) {
        FILE* fp = fopen(file_path.c_str(), "rb");
        if (!fp) {
            return false;
        }
        
        png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        if (!png) {
            fclose(fp);
            return false;
        }
        
        png_infop info = png_create_info_struct(png);
        if (!info) {
            png_destroy_read_struct(&png, NULL, NULL);
            fclose(fp);
            return false;
        }
        
        if (setjmp(png_jmpbuf(png))) {
            png_destroy_read_struct(&png, &info, NULL);
            fclose(fp);
            return false;
        }
        
        png_init_io(png, fp);
        png_read_info(png, info);
        
        width = png_get_image_width(png, info);
        height = png_get_image_height(png, info);
        png_byte color_type = png_get_color_type(png, info);
        png_byte bit_depth = png_get_bit_depth(png, info);
        
        if (bit_depth == 16) png_set_strip_16(png);
        if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
        if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
        if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
        
        if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE) {
            png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
        }
        
        png_read_update_info(png, info);
        
        size_t row_bytes = png_get_rowbytes(png, info);
        pixel_data.resize(row_bytes * height);
        
        png_bytep* row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * height);
        if (!row_pointers) {
            png_destroy_read_struct(&png, &info, NULL);
            fclose(fp);
            return false;
        }
        
        for (int y = 0; y < height; y++) {
            row_pointers[y] = pixel_data.data() + (y * row_bytes);
        }
        
        png_read_image(png, row_pointers);
        fclose(fp);
        free(row_pointers);
        png_destroy_read_struct(&png, &info, NULL);
        
        return true;
    }
    
public:
    OSMTileManager() {
        cache_dir = "osm_cache";
        try {
            std::filesystem::create_directories(cache_dir);
        } catch (const std::exception& e) {
            std::cerr << "Error creating cache dir: " << e.what() << std::endl;
        }
        
        curl = curl_easy_init();
        if (!curl) {
            std::cerr << "Failed to initialize CURL" << std::endl;
        }
        
        loadCacheFromDisk();
        
        download_thread = std::thread([this]() {
            while (running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                processDownloadQueue();
            }
        });
    }
    
    ~OSMTileManager() {
        running = false;
        if (download_thread.joinable()) download_thread.join();
        if (curl) curl_easy_cleanup(curl);
        
        std::lock_guard<std::mutex> lock(tiles_mutex);
        for (auto& [key, tile] : tiles) {
            if (tile.texture.texture_id != 0) {
                glDeleteTextures(1, &tile.texture.texture_id);
                tile.texture.texture_id = 0;
            }
        }
    }
    
    void loadCacheFromDisk() {
        std::lock_guard<std::mutex> lock(tiles_mutex);
        
        if (!std::filesystem::exists(cache_dir)) return;
        
        try {
            for (const auto& zoom_dir : std::filesystem::directory_iterator(cache_dir)) {
                if (!zoom_dir.is_directory()) continue;
                
                int zoom = 0;
                try {
                    zoom = std::stoi(zoom_dir.path().filename().string());
                } catch (...) {
                    continue;
                }
                
                for (const auto& tile_file : std::filesystem::directory_iterator(zoom_dir.path())) {
                    if (tile_file.path().extension() != ".png") continue;
                    
                    std::string filename = tile_file.path().stem().string();
                    size_t underscore_pos = filename.find('_');
                    if (underscore_pos == std::string::npos) continue;
                    
                    int x = 0, y = 0;
                    try {
                        x = std::stoi(filename.substr(0, underscore_pos));
                        y = std::stoi(filename.substr(underscore_pos + 1));
                    } catch (...) {
                        continue;
                    }
                    
                    std::string key = getTileKey(x, y, zoom);
                    Tile tile;
                    tile.x = x;
                    tile.y = y;
                    tile.zoom = zoom;
                    tile.loaded = false;
                    tile.is_loading = false;
                    tile.needs_upload = true;
                    
                    int width, height;
                    if (loadPNGToMemory(tile_file.path().string(), tile.texture.pixel_data, width, height)) {
                        tile.texture.width = width;
                        tile.texture.height = height;
                        tile.loaded = true;
                        std::cout << "Loaded cached tile: " << zoom << "/" << x << "_" << y << std::endl;
                    }
                    
                    tiles[key] = tile;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error loading cache: " << e.what() << std::endl;
        }
        
        std::cout << "Loaded " << tiles.size() << " cached tiles" << std::endl;
    }
    
    void processDownloadQueue() {
        std::lock_guard<std::mutex> lock(tiles_mutex);
        
        for (auto& [key, tile] : tiles) {
            if (!tile.loaded && !tile.is_loading) {
                tile.is_loading = true;
                
                try {
                    std::string save_path = getTilePath(tile.x, tile.y, tile.zoom);
                    
                    if (downloadTile(tile.x, tile.y, tile.zoom, save_path)) {
                        int width, height;
                        std::vector<unsigned char> pixel_data;
                        if (loadPNGToMemory(save_path, pixel_data, width, height)) {
                            tile.texture.pixel_data = std::move(pixel_data);
                            tile.texture.width = width;
                            tile.texture.height = height;
                            tile.loaded = true;
                            tile.needs_upload = true;
                            std::cout << "Downloaded tile: " << tile.zoom << "/" << tile.x << "_" << tile.y << std::endl;
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error downloading tile: " << e.what() << std::endl;
                }
                
                tile.is_loading = false;
                break;
            }
        }
    }
    
    void uploadTexturesToGPU() {
        std::lock_guard<std::mutex> lock(tiles_mutex);
        
        for (auto& [key, tile] : tiles) {
            if (tile.loaded && tile.needs_upload && !tile.texture.pixel_data.empty()) {
                if (tile.texture.texture_id == 0) {
                    glGenTextures(1, &tile.texture.texture_id);
                }
                
                if (tile.texture.texture_id != 0) {
                    glBindTexture(GL_TEXTURE_2D, tile.texture.texture_id);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 
                                tile.texture.width, tile.texture.height, 0, 
                                GL_RGBA, GL_UNSIGNED_BYTE, tile.texture.pixel_data.data());
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    
                    tile.texture.pixel_data.clear();
                    tile.texture.pixel_data.shrink_to_fit();
                    tile.needs_upload = false;
                    tile.texture.loaded = true;
                }
            }
        }
    }
    
    void requestTile(int x, int y, int zoom) {
        if (x < 0 || y < 0 || zoom < 0) return;
        
        std::string key = getTileKey(x, y, zoom);
        std::lock_guard<std::mutex> lock(tiles_mutex);
        
        if (tiles.find(key) == tiles.end()) {
            Tile tile;
            tile.x = x;
            tile.y = y;
            tile.zoom = zoom;
            tile.loaded = false;
            tile.is_loading = false;
            tile.needs_upload = false;
            tiles[key] = tile;
        }
        
        tiles[key].last_used = std::chrono::steady_clock::now();
    }
    
    Tile* getTile(int x, int y, int zoom) {
        std::string key = getTileKey(x, y, zoom);
        std::lock_guard<std::mutex> lock(tiles_mutex);
        
        auto it = tiles.find(key);
        if (it != tiles.end() && it->second.loaded && it->second.texture.texture_id != 0) {
            return &it->second;
        }
        return nullptr;
    }
};

struct TrackPoint {
    float lat, lon;
    int rsrp;
    int pci;
};

struct HeatPoint {
    int   pci;
    float lat, lon;
    int   rsrp;
};

static std::mutex              heat_mtx;
static std::vector<HeatPoint>  heat_queue;
static std::mutex              heat_track_mtx;
static std::map<int, std::deque<TrackPoint>> heat_tracks;
static std::atomic<bool>       heat_running{true};
static const size_t            HEAT_MAX = 5000;

void heat_add_points(const std::vector<HeatPoint>& pts) {
    std::lock_guard<std::mutex> lk(heat_mtx);
    heat_queue.insert(heat_queue.end(), pts.begin(), pts.end());
}

void heat_worker_thread() {
    while (heat_running) {
        std::vector<HeatPoint> batch;
        {
            std::lock_guard<std::mutex> lk(heat_mtx);
            batch.swap(heat_queue);
        }
        if (!batch.empty()) {
            std::lock_guard<std::mutex> lk(heat_track_mtx);
            for (const auto& p : batch) {
                auto& dq = heat_tracks[p.pci];
                dq.push_back({p.lat, p.lon, p.rsrp, p.pci});
                while (dq.size() > HEAT_MAX) dq.pop_front();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

struct LocationData {
    float lat = 0.0f;
    float lon = 0.0f;
    float alt = 0.0f;
    long long timestamp = 0;
    std::string time_str;
    
    std::vector<CellData> current_cells;
    std::map<int, std::deque<int>> pci_history;
    std::map<int, std::deque<TrackPoint>> pci_track;
    std::map<int, std::string> pci_names;

    int rsrp = -110;
    int rsrq = -20;
    int rssi = -100;
    int frequency = 0;
    std::string ip_address = "0.0.0.0";
    std::string network_type = "Unknown";
    std::string operator_name = "Unknown";
    std::string device_id = "Unknown";
    
    std::mutex mtx;        
    std::atomic<bool> is_running{true};
    
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
    
    const size_t max_history = 200;
};

void run_server(LocationData* loc) {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);
    
    try {
        socket.bind("tcp://*:5566");
        std::cout << "Socket bound successfully on port 5566" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Bind Error: " << e.what() << std::endl;
        return;
    }

    socket.set(zmq::sockopt::rcvtimeo, 1000);
    std::cout << "Waiting for connections... (Ctrl+C to stop)" << std::endl;
    
    while (loc->is_running) {
        zmq::message_t request;
        auto res = socket.recv(request, zmq::recv_flags::none);
        
        if (res.has_value()) {
            std::string msg_str(static_cast<char*>(request.data()), request.size());
            
            try {
                auto j = json::parse(msg_str);
                
                auto now = std::chrono::system_clock::now();
                auto now_time_t = std::chrono::system_clock::to_time_t(now);
                std::stringstream time_ss;
                time_ss << std::put_time(std::localtime(&now_time_t), "%H:%M:%S");
                
                {
                    std::lock_guard<std::mutex> lock(loc->mtx);
                    
                    if (j.contains("latitude")) loc->lat = j["latitude"].get<double>();
                    else if (j.contains("lat")) loc->lat = j["lat"].get<double>();
                    
                    if (j.contains("longitude")) loc->lon = j["longitude"].get<double>();
                    else if (j.contains("lon")) loc->lon = j["lon"].get<double>();
                    
                    if (j.contains("altitude")) loc->alt = j["altitude"].get<double>();
                    else if (j.contains("alt")) loc->alt = j["alt"].get<double>();
                    
                    if (j.contains("time")) loc->timestamp = j["time"].get<long long>();
                    else if (j.contains("timestamp")) loc->timestamp = j["timestamp"].get<long long>();
                    
                    loc->time_str = time_ss.str();
                    
                    if (j.contains("rsrp")) loc->rsrp = j["rsrp"].get<int>();
                    if (j.contains("rsrq")) loc->rsrq = j["rsrq"].get<int>();
                    if (j.contains("rssi")) loc->rssi = j["rssi"].get<int>();
                    if (j.contains("frequency")) loc->frequency = j["frequency"].get<int>();
                    if (j.contains("ip_address")) loc->ip_address = j["ip_address"].get<std::string>();
                    if (j.contains("network_type")) loc->network_type = j["network_type"].get<std::string>();
                    if (j.contains("provider")) loc->operator_name = j["provider"].get<std::string>();
                    if (j.contains("device_id")) loc->device_id = j["device_id"].get<std::string>();
                    
                    if (j.contains("cells") && j["cells"].is_array()) {
                        loc->current_cells.clear();
                        std::vector<CellData> new_cells;
                        
                        for (const auto& cell_json : j["cells"]) {
                            CellData cell;
                            if (cell_json.contains("pci")) cell.pci = cell_json["pci"].get<int>();
                            if (cell_json.contains("rsrp")) cell.rsrp = cell_json["rsrp"].get<int>();
                            if (cell_json.contains("rsrq")) cell.rsrq = cell_json["rsrq"].get<int>();
                            if (cell_json.contains("rssi")) cell.rssi = cell_json["rssi"].get<int>();
                            if (cell_json.contains("earfcn")) cell.earfcn = cell_json["earfcn"].get<int>();
                            cell.timestamp = loc->timestamp;
                            cell.time_str = loc->time_str;
                            new_cells.push_back(cell);
                            loc->current_cells.push_back(cell);
                            
                            loc->pci_history[cell.pci].push_back(cell.rsrp);
                            loc->pci_names[cell.pci] = "PCI " + std::to_string(cell.pci);
                            loc->pci_track[cell.pci].push_back({loc->lat, loc->lon, cell.rsrp, cell.pci});
                            
                            while (loc->pci_history[cell.pci].size() > loc->max_history) {
                                loc->pci_history[cell.pci].pop_front();
                            }
                            while (loc->pci_track[cell.pci].size() > loc->max_history) {
                                loc->pci_track[cell.pci].pop_front();
                            }
                            
                            std::cout << "  Cell - PCI: " << cell.pci << ", RSRP: " << cell.rsrp << " dBm" << std::endl;
                        }
                        std::cout << "Received " << new_cells.size() << " cells" << std::endl;
                    }
                    
                    loc->lat_history.push_back(loc->lat);
                    loc->lon_history.push_back(loc->lon);
                    loc->alt_history.push_back(loc->alt);
                    loc->timestamp_history.push_back(loc->timestamp);
                    loc->rsrp_history.push_back(loc->rsrp);
                    loc->rsrq_history.push_back(loc->rsrq);
                    loc->rssi_history.push_back(loc->rssi);
                    loc->time_history.push_back(loc->time_str);
                    loc->network_type_history.push_back(loc->network_type);
                    loc->frequency_history.push_back(loc->frequency);
                    
                    while (loc->lat_history.size() > loc->max_history) {
                        loc->lat_history.pop_front();
                        loc->lon_history.pop_front();
                        loc->alt_history.pop_front();
                        loc->timestamp_history.pop_front();
                        loc->rsrp_history.pop_front();
                        loc->rsrq_history.pop_front();
                        loc->rssi_history.pop_front();
                        loc->time_history.pop_front();
                        loc->network_type_history.pop_front();
                        loc->frequency_history.pop_front();
                    }
                    
                    std::cout << "Received data - Lat: " << loc->lat << ", Lon: " << loc->lon 
                              << ", RSRP: " << loc->rsrp << ", Network: " << loc->network_type 
                              << ", Cells: " << loc->current_cells.size() << std::endl;
                }

                std::ofstream file("location.json", std::ios::app);
                if (file.is_open()) {
                    file << j.dump() << std::endl;
                    file.close();
                }

                socket.send(zmq::str_buffer("OK"), zmq::send_flags::none);
                
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << std::endl;
                socket.send(zmq::str_buffer("Error"), zmq::send_flags::none);
            }
        }
    }
}

void loadHistoryFromFile(LocationData* loc, const std::string& filename = "location.json") {
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

    loc->lat_history.clear();
    loc->lon_history.clear();
    loc->alt_history.clear();
    loc->timestamp_history.clear();
    loc->rsrp_history.clear();
    loc->rsrq_history.clear();
    loc->rssi_history.clear();
    loc->time_history.clear();
    loc->network_type_history.clear();
    loc->frequency_history.clear();
    loc->pci_history.clear();
    loc->pci_names.clear();
    loc->pci_track.clear();
    loc->current_cells.clear();

    for (const auto& j : all_data) {
        float lat = 0.0f, lon = 0.0f, alt = 0.0f;
        long long timestamp = 0;

        const json* loc_src = nullptr;
        if (j.contains("location") && j["location"].is_object()) {
            loc_src = &j["location"];
        } else {
            loc_src = &j;
        }

        if (loc_src->contains("latitude"))  lat = (*loc_src)["latitude"].get<double>();
        if (loc_src->contains("longitude")) lon = (*loc_src)["longitude"].get<double>();
        if (loc_src->contains("altitude"))  alt = (*loc_src)["altitude"].get<double>();
        if (loc_src->contains("time"))      timestamp = (*loc_src)["time"].get<long long>();
        if (timestamp == 0 && j.contains("timestamp")) timestamp = j["timestamp"].get<long long>();

        loc->lat_history.push_back(lat);
        loc->lon_history.push_back(lon);
        loc->alt_history.push_back(alt);
        loc->timestamp_history.push_back(timestamp);

        if (timestamp > 0) {
            auto tp = std::chrono::system_clock::from_time_t(timestamp / 1000);
            auto tt = std::chrono::system_clock::to_time_t(tp);
            std::stringstream ss;
            ss << std::put_time(std::localtime(&tt), "%H:%M:%S");
            loc->time_history.push_back(ss.str());
        } else {
            loc->time_history.push_back("");
        }

        int best_rsrp = -110, best_rsrq = -20, best_rssi = -100, best_earfcn = 0;
        std::string best_type = "Unknown";

        if (j.contains("telephony") && j["telephony"].is_array()) {
            bool first = true;
            for (const auto& cell_j : j["telephony"]) {
                if (!cell_j.contains("type")) continue;
                std::string ctype = cell_j["type"].get<std::string>();
                if (ctype != "LTE") continue;

                const auto* id  = cell_j.contains("CellIdentityLte")   ? &cell_j["CellIdentityLte"]   : nullptr;
                const auto* sig = cell_j.contains("CellSignalStrengthLte") ? &cell_j["CellSignalStrengthLte"] : nullptr;
                if (!id || !sig) continue;

                int pci    = id->value("PCI",   -1);
                int rsrp   = sig->value("RSRP",  -110);
                int rsrq   = sig->value("RSRQ",  -20);
                int rssi   = sig->value("RSSI",  -100);
                int earfcn = id->value("EARFCN", 0);

                if (pci < 0) continue;

                if (first || rsrp > best_rsrp) {
                    best_rsrp  = rsrp;
                    best_rsrq  = rsrq;
                    best_rssi  = rssi;
                    best_earfcn = earfcn;
                    best_type  = "LTE";
                    first = false;
                }

                loc->pci_history[pci].push_back(rsrp);
                loc->pci_names[pci] = "PCI " + std::to_string(pci);
                while (loc->pci_history[pci].size() > loc->max_history)
                    loc->pci_history[pci].pop_front();

                loc->pci_track[pci].push_back({lat, lon, rsrp, pci});
                while (loc->pci_track[pci].size() > loc->max_history)
                    loc->pci_track[pci].pop_front();
            }
        } else {
            if (j.contains("rsrp")) best_rsrp  = j["rsrp"].get<int>();
            if (j.contains("rsrq")) best_rsrq  = j["rsrq"].get<int>();
            if (j.contains("rssi")) best_rssi  = j["rssi"].get<int>();
            if (j.contains("frequency")) best_earfcn = j["frequency"].get<int>();
            if (j.contains("network_type")) best_type = j["network_type"].get<std::string>();

            if (j.contains("cells") && j["cells"].is_array()) {
                for (const auto& cell_j : j["cells"]) {
                    int pci  = cell_j.value("pci",  -1);
                    int rsrp = cell_j.value("rsrp", -110);
                    int rsrq = cell_j.value("rsrq", -20);
                    int rssi = cell_j.value("rssi", -100);
                    int earfcn = cell_j.value("earfcn", 0);
                    if (pci < 0) continue;

                    if (rsrp > best_rsrp) {
                        best_rsrp   = rsrp;
                        best_rsrq   = rsrq;
                        best_rssi   = rssi;
                        best_earfcn = earfcn;
                    }
                    loc->pci_history[pci].push_back(rsrp);
                    loc->pci_names[pci] = "PCI " + std::to_string(pci);
                    while (loc->pci_history[pci].size() > loc->max_history)
                        loc->pci_history[pci].pop_front();

                    loc->pci_track[pci].push_back({lat, lon, rsrp, pci});
                    while (loc->pci_track[pci].size() > loc->max_history)
                        loc->pci_track[pci].pop_front();
                }
            }
        }

        loc->rsrp_history.push_back(best_rsrp);
        loc->rsrq_history.push_back(best_rsrq);
        loc->rssi_history.push_back(best_rssi);
        loc->frequency_history.push_back(best_earfcn);
        loc->network_type_history.push_back(best_type);
    }

    {
        const auto& last = all_data.back();

        const json* loc_src = (last.contains("location") && last["location"].is_object())
                              ? &last["location"] : &last;

        if (loc_src->contains("latitude"))  loc->lat = (*loc_src)["latitude"].get<double>();
        if (loc_src->contains("longitude")) loc->lon = (*loc_src)["longitude"].get<double>();
        if (loc_src->contains("altitude"))  loc->alt = (*loc_src)["altitude"].get<double>();

        long long t = 0;
        if (loc_src->contains("time")) t = (*loc_src)["time"].get<long long>();
        if (t == 0 && last.contains("timestamp")) t = last["timestamp"].get<long long>();
        if (t > 0) {
            auto tp = std::chrono::system_clock::from_time_t(t / 1000);
            auto tt = std::chrono::system_clock::to_time_t(tp);
            std::stringstream ss;
            ss << std::put_time(std::localtime(&tt), "%H:%M:%S");
            loc->time_str = ss.str();
            loc->timestamp = t;
        }

        if (!loc->rsrp_history.empty()) loc->rsrp = loc->rsrp_history.back();
        if (!loc->rsrq_history.empty()) loc->rsrq = loc->rsrq_history.back();
        if (!loc->rssi_history.empty()) loc->rssi = loc->rssi_history.back();
        if (!loc->frequency_history.empty()) loc->frequency = loc->frequency_history.back();
        if (!loc->network_type_history.empty()) loc->network_type = loc->network_type_history.back();

        loc->current_cells.clear();
        if (last.contains("telephony") && last["telephony"].is_array()) {
            for (const auto& cell_j : last["telephony"]) {
                if (!cell_j.contains("type") || cell_j["type"] != "LTE") continue;
                const auto* id  = cell_j.contains("CellIdentityLte")      ? &cell_j["CellIdentityLte"]      : nullptr;
                const auto* sig = cell_j.contains("CellSignalStrengthLte") ? &cell_j["CellSignalStrengthLte"] : nullptr;
                if (!id || !sig) continue;
                CellData cd;
                cd.pci     = id->value("PCI",   -1);
                cd.rsrp    = sig->value("RSRP", -110);
                cd.rsrq    = sig->value("RSRQ", -20);
                cd.rssi    = sig->value("RSSI", -100);
                cd.earfcn  = id->value("EARFCN", 0);
                cd.timestamp = loc->timestamp;
                cd.time_str  = loc->time_str;
                if (cd.pci >= 0) loc->current_cells.push_back(cd);
            }
        } else if (last.contains("cells") && last["cells"].is_array()) {
            for (const auto& cell_j : last["cells"]) {
                CellData cd;
                cd.pci     = cell_j.value("pci",   -1);
                cd.rsrp    = cell_j.value("rsrp", -110);
                cd.rsrq    = cell_j.value("rsrq",  -20);
                cd.rssi    = cell_j.value("rssi", -100);
                cd.earfcn  = cell_j.value("earfcn",  0);
                cd.timestamp = loc->timestamp;
                cd.time_str  = loc->time_str;
                if (cd.pci >= 0) loc->current_cells.push_back(cd);
            }
        }

        if (last.contains("ip_address"))   loc->ip_address    = last["ip_address"].get<std::string>();
        if (last.contains("provider"))     loc->operator_name = last["provider"].get<std::string>();
        if (last.contains("device_id"))    loc->device_id     = last["device_id"].get<std::string>();
    }

    std::cout << "Position: " << loc->lat << ", " << loc->lon << std::endl;
    std::cout << "History: " << loc->lat_history.size() << " points" << std::endl;
    std::cout << "PCI count: " << loc->pci_history.size() << std::endl;
    std::cout << "Last RSRP: " << loc->rsrp << " dBm" << std::endl;
}

std::string getSignalQuality(int rsrp) {
    if (rsrp > -80) return "Excellent";
    if (rsrp > -90) return "Good";
    if (rsrp > -100) return "Fair";
    if (rsrp > -110) return "Poor";
    return "Very Poor";
}

ImU32 rsrpToColor(int rsrp) {
    const float RSRP_MIN = -110.0f;
    const float RSRP_MAX = -80.0f;
    float t = (std::max(RSRP_MIN, std::min(RSRP_MAX, (float)rsrp)) - RSRP_MIN) / (RSRP_MAX - RSRP_MIN);
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
    return IM_COL32((int)(r*255), (int)(g*255), (int)(b*255), 50);
}

int main(int argc, char *argv[]) {
    std::cout << "Starting GPS & Network Monitor with OSM Map..." << std::endl;
    
    static LocationData locationInfo;
    locationInfo.is_running = true;

    char log_file_path[512] = "location.json";
    loadHistoryFromFile(&locationInfo, log_file_path);

    {
        std::lock_guard<std::mutex> lk(heat_track_mtx);
        for (const auto& [pci, dq] : locationInfo.pci_track) {
            for (const auto& tp : dq) {
                heat_tracks[pci].push_back(tp);
            }
        }
    }
    
    const float NOVOSIBIRSK_LAT = 55.0304;
    const float NOVOSIBIRSK_LON = 82.9202;
    
    float map_center_lat = NOVOSIBIRSK_LAT;
    float map_center_lon = NOVOSIBIRSK_LON;
    int current_zoom = 14;
    
    if (locationInfo.lat != 0.0f || locationInfo.lon != 0.0f) {
        map_center_lat = locationInfo.lat;
        map_center_lon = locationInfo.lon;
        std::cout << "Setting map center to last known position: " << map_center_lat << ", " << map_center_lon << std::endl;
    } else {
        std::cout << "No position data found, using Novosibirsk: " << NOVOSIBIRSK_LAT << ", " << NOVOSIBIRSK_LON << std::endl;
    }
    
    std::cout << "\n=== Current Data ===" << std::endl;
    std::cout << "Position: " << locationInfo.lat << ", " << locationInfo.lon << std::endl;
    std::cout << "History points: " << locationInfo.lat_history.size() << std::endl;
    std::cout << "RSRP history: " << locationInfo.rsrp_history.size() << " points" << std::endl;
    std::cout << "Last RSRP: " << locationInfo.rsrp << " dBm" << std::endl;
    std::cout << "Network: " << locationInfo.network_type << std::endl;
    std::cout << "===================\n" << std::endl;
    
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL Init failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window* window = SDL_CreateWindow(
        "GPS & Network Monitor with OSM Map",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1600, 900,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );
    
    if (!window) {
        std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        std::cerr << "Failed to create GL context: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);
    
    glewExperimental = GL_TRUE;
    GLenum glewError = glewInit();
    if (glewError != GLEW_OK) {
        std::cerr << "GLEW init failed: " << glewGetErrorString(glewError) << std::endl;
    }
    
    glGetError();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "imgui_layout.ini";
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    std::thread server_thread(run_server, &locationInfo);
    std::thread heat_thread(heat_worker_thread);
    
    OSMTileManager tileManager;
    
    bool show_map = true;
    bool show_settings = false;
    bool show_gps_plots = true;
    bool show_network_plots = true;
    bool show_history = true;
    bool show_pci_panel = true;
    
    int selected_pci_filter = -1;
    bool show_all_points = true;
    std::vector<int> all_pci_list;
    
    {
        std::lock_guard<std::mutex> lk(heat_track_mtx);
        for (const auto& [pci, _] : heat_tracks)
            all_pci_list.push_back(pci);
    }
    if (all_pci_list.empty()) {
        std::lock_guard<std::mutex> lock(locationInfo.mtx);
        for (const auto& [pci, _] : locationInfo.pci_track)
            all_pci_list.push_back(pci);
    }
    std::sort(all_pci_list.begin(), all_pci_list.end());
    all_pci_list.erase(std::unique(all_pci_list.begin(), all_pci_list.end()), all_pci_list.end());

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
    ImPlotColormap signal_colormap = ImPlot::AddColormap(
        "SignalStrength", signal_colors, IM_ARRAYSIZE(signal_colors), false
    );
    
    auto last_time = std::chrono::steady_clock::now();

    while (true) {
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_time).count();
        
        if (elapsed < 16) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16 - elapsed));
        }
        last_time = current_time;
        
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                locationInfo.is_running = false;
                goto cleanup;
            }
        }

        tileManager.uploadTexturesToGPU();

        {
            std::lock_guard<std::mutex> lk(locationInfo.mtx);
            if (!locationInfo.current_cells.empty()) {
                std::vector<HeatPoint> pts;
                for (const auto& cell : locationInfo.current_cells)
                    pts.push_back({cell.pci, locationInfo.lat, locationInfo.lon, cell.rsrp});
                heat_add_points(pts);
            }
        }
        
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit")) {
                    locationInfo.is_running = false;
                    goto cleanup;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Windows")) {
                ImGui::MenuItem("Location Info", nullptr, &show_settings);
                ImGui::MenuItem("GPS & Altitude Plot", nullptr, &show_gps_plots);
                ImGui::MenuItem("Network Plots", nullptr, &show_network_plots);
                ImGui::MenuItem("History", nullptr, &show_history);
                ImGui::MenuItem("Map", nullptr, &show_map);
                ImGui::MenuItem("PCI Selector", nullptr, &show_pci_panel);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        ImGui::Begin("GPS & Network Monitor", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        
        float currentLat, currentLon, currentAlt;
        long long currentTime;
        std::string currentTimeStr;
        int currentRSRP, currentRSRQ, currentRSSI, currentFreq;
        std::string currentIP, currentNetworkType, currentOperator, currentDevice;
        
        {
            std::lock_guard<std::mutex> lock(locationInfo.mtx);
            currentLat = locationInfo.lat;
            currentLon = locationInfo.lon;
            currentAlt = locationInfo.alt;
            currentTime = locationInfo.timestamp;
            currentTimeStr = locationInfo.time_str;
            currentRSRP = locationInfo.rsrp;
            currentRSRQ = locationInfo.rsrq;
            currentRSSI = locationInfo.rssi;
            currentFreq = locationInfo.frequency;
            currentIP = locationInfo.ip_address;
            currentNetworkType = locationInfo.network_type;
            currentOperator = locationInfo.operator_name;
            currentDevice = locationInfo.device_id;
        }

        ImGui::Text("GPS DATA");
        ImGui::Separator();
        ImGui::Text("Latitude:  %.6f", currentLat);
        ImGui::Text("Longitude: %.6f", currentLon);
        ImGui::Text("Altitude:  %.2f m", currentAlt);
        ImGui::Text("Time:      %s", currentTimeStr.c_str());
        ImGui::Text("Timestamp: %lld", currentTime);
        
        ImGui::Spacing();
        ImGui::Separator();
        
        ImGui::Text("NETWORK DATA");
        ImGui::Separator();
        ImGui::Text("Device:    %s", currentDevice.c_str());
        ImGui::Text("Provider:  %s", currentOperator.c_str());
        ImGui::Text("Network:   %s", currentNetworkType.c_str());
        ImGui::Text("IP:        %s", currentIP.c_str());
        ImGui::Text("Frequency: %d MHz", currentFreq);
        
        ImGui::Spacing();
        
        ImGui::Text("SIGNAL PARAMETERS");
        ImGui::Separator();
        ImGui::Text("RSRP:      %d dBm  (%s)", currentRSRP, getSignalQuality(currentRSRP).c_str());
        ImGui::Text("RSRQ:      %d dB", currentRSRQ);
        ImGui::Text("RSSI:      %d dBm", currentRSSI);
        
        float quality = std::max(0.0f, std::min(1.0f, (currentRSRP + 110.0f) / 30.0f));
        ImGui::ProgressBar(quality, ImVec2(200, 20), ("Signal: " + std::to_string((int)(quality * 100)) + "%").c_str());
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Server:    Online (port 5566)");
        ImGui::Text("History:   %zu points", locationInfo.lat_history.size());
        ImGui::Text("PCI count: %zu", locationInfo.pci_history.size());

        {
            std::ifstream test(log_file_path);
            if (test.is_open()) {
                ImGui::TextColored(ImVec4(0,1,0,1), "File: OK  [%s]", log_file_path);
            } else {
                ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "File NOT FOUND: [%s]", log_file_path);
            }
        }
        
        if (ImGui::Button("Clear Log")) {
            std::ofstream file(log_file_path, std::ios::trunc);
            file.close();
        }
        ImGui::SameLine();
        if (ImGui::Button("Settings")) {
            show_settings = true;
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Reload Data")) {
            loadHistoryFromFile(&locationInfo, log_file_path);
            std::lock_guard<std::mutex> lk(heat_track_mtx);
            heat_tracks.clear();
            for (const auto& [pci, dq] : locationInfo.pci_track) {
                for (const auto& tp : dq) {
                    heat_tracks[pci].push_back(tp);
                }
            }
            all_pci_list.clear();
            for (const auto& [pci, _] : heat_tracks)
                all_pci_list.push_back(pci);
            std::sort(all_pci_list.begin(), all_pci_list.end());
            all_pci_list.erase(std::unique(all_pci_list.begin(), all_pci_list.end()), all_pci_list.end());
            std::cout << "Data reloaded from file: " << log_file_path << std::endl;
        }

        ImGui::Spacing();
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("Log file", log_file_path, sizeof(log_file_path));
        ImGui::SameLine();
        if (ImGui::Button("Load")) {
            loadHistoryFromFile(&locationInfo, log_file_path);
            std::lock_guard<std::mutex> lk(heat_track_mtx);
            heat_tracks.clear();
            for (const auto& [pci, dq] : locationInfo.pci_track) {
                for (const auto& tp : dq) {
                    heat_tracks[pci].push_back(tp);
                }
            }
            all_pci_list.clear();
            for (const auto& [pci, _] : heat_tracks)
                all_pci_list.push_back(pci);
            std::sort(all_pci_list.begin(), all_pci_list.end());
            all_pci_list.erase(std::unique(all_pci_list.begin(), all_pci_list.end()), all_pci_list.end());
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Center Map")) {
            map_center_lat = currentLat;
            map_center_lon = currentLon;
            std::cout << "Centering map on: " << map_center_lat << ", " << map_center_lon << std::endl;
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Novosibirsk")) {
            map_center_lat = NOVOSIBIRSK_LAT;
            map_center_lon = NOVOSIBIRSK_LON;
            current_zoom = 14;
            std::cout << "Centering map on Novosibirsk" << std::endl;
        }
        
        ImGui::End();

        if (show_pci_panel) {
            ImGui::Begin("PCI Selector", &show_pci_panel, 
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
            
            ImGui::Text("PCI Filter");
            ImGui::Separator();
            
            if (show_all_points) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
                ImGui::Button("Show All Points", ImVec2(150, 0));
                ImGui::PopStyleColor();
            } else {
                if (ImGui::Button("Show All Points", ImVec2(150, 0))) {
                    show_all_points = true;
                }
            }
            ImGui::SameLine();
            if (!show_all_points) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.3f, 0.1f, 1.0f));
                ImGui::Button("Show Heatmap", ImVec2(150, 0));
                ImGui::PopStyleColor();
            } else {
                if (ImGui::Button("Show Heatmap", ImVec2(150, 0))) {
                    show_all_points = false;
                }
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            
            if (ImGui::Button("All PCIs", ImVec2(180, 0))) {
                selected_pci_filter = -1;
            }
            
            ImGui::Spacing();
            
            if (ImGui::BeginListBox("##pci_list", ImVec2(200, 200))) {
                for (int pci : all_pci_list) {
                    bool is_selected = (selected_pci_filter == pci);
                    char label[32];
                    snprintf(label, sizeof(label), "PCI %d", pci);
                    if (ImGui::Selectable(label, is_selected)) {
                        selected_pci_filter = pci;
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndListBox();
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            
            if (selected_pci_filter != -1) {
                std::lock_guard<std::mutex> lk(heat_track_mtx);
                auto it = heat_tracks.find(selected_pci_filter);
                if (it != heat_tracks.end()) {
                    int samples = it->second.size();
                    int last_rsrp = samples > 0 ? it->second.back().rsrp : -110;
                    ImGui::Text("Statistics");
                    ImGui::Text("Samples: %d", samples);
                    ImGui::Text("Last RSRP: %d dBm", last_rsrp);
                    
                    ImDrawList* draw_list = ImGui::GetWindowDrawList();
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    float bar_width = 180;
                    for (int px = 0; px < bar_width; px++) {
                        float t = (float)px / bar_width;
                        int rsrp_val = (int)(-110.0f + t * 30.0f);
                        ImU32 c = rsrpToColor(rsrp_val);
                        draw_list->AddRectFilled(
                            ImVec2(pos.x + px, pos.y),
                            ImVec2(pos.x + px + 1, pos.y + 20), c);
                    }
                    ImGui::Dummy(ImVec2(bar_width, 20));
                    ImGui::Text(" -110                     -80");
                }
            } else {
                int total_samples = 0;
                std::lock_guard<std::mutex> lk(heat_track_mtx);
                for (const auto& [_, points] : heat_tracks) {
                    total_samples += points.size();
                }
                ImGui::Text("Total PCI: %zu", all_pci_list.size());
                ImGui::Text("Total points: %d", total_samples);
                ImGui::Text("Mode: %s", show_all_points ? "All Points" : "Heatmap");
            }
            
            ImGui::End();
        }

        if (show_map) {
            ImGui::Begin("OpenStreetMap", &show_map);
            
            ImVec2 map_pos = ImGui::GetCursorScreenPos();
            ImVec2 map_size = ImGui::GetContentRegionAvail();
            if (map_size.x < 100) map_size.x = 800;
            if (map_size.y < 100) map_size.y = 600;
            
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            
            const int TILE_SIZE = 256;
            
            double center_pixel_x = lonToX(map_center_lon, current_zoom);
            double center_pixel_y = latToY(map_center_lat, current_zoom);
            
            double offset_x = map_size.x / 2.0 - center_pixel_x;
            double offset_y = map_size.y / 2.0 - center_pixel_y;
            
            int start_tile_x = (int)((-offset_x) / TILE_SIZE) - 1;
            int start_tile_y = (int)((-offset_y) / TILE_SIZE) - 1;
            int end_tile_x = start_tile_x + (int)(map_size.x / TILE_SIZE) + 2;
            int end_tile_y = start_tile_y + (int)(map_size.y / TILE_SIZE) + 2;
            
            int max_tiles = (1 << current_zoom);
            
            for (int tx = start_tile_x; tx <= end_tile_x; tx++) {
                for (int ty = start_tile_y; ty <= end_tile_y; ty++) {
                    if (tx >= 0 && tx < max_tiles && ty >= 0 && ty < max_tiles) {
                        double screen_x = (tx * TILE_SIZE) + offset_x;
                        double screen_y = (ty * TILE_SIZE) + offset_y;
                        
                        if (screen_x + TILE_SIZE > 0 && screen_x < map_size.x &&
                            screen_y + TILE_SIZE > 0 && screen_y < map_size.y) {
                            
                            tileManager.requestTile(tx, ty, current_zoom);
                            Tile* tile = tileManager.getTile(tx, ty, current_zoom);
                            
                            if (tile && tile->loaded && tile->texture.texture_id != 0) {
                                draw_list->AddImage(
                                    (void*)(intptr_t)tile->texture.texture_id,
                                    ImVec2(map_pos.x + screen_x, map_pos.y + screen_y),
                                    ImVec2(map_pos.x + screen_x + TILE_SIZE, map_pos.y + screen_y + TILE_SIZE)
                                );
                            } else {
                                draw_list->AddRectFilled(
                                    ImVec2(map_pos.x + screen_x, map_pos.y + screen_y),
                                    ImVec2(map_pos.x + screen_x + TILE_SIZE, map_pos.y + screen_y + TILE_SIZE),
                                    IM_COL32(240, 240, 240, 255)
                                );
                                draw_list->AddRect(
                                    ImVec2(map_pos.x + screen_x, map_pos.y + screen_y),
                                    ImVec2(map_pos.x + screen_x + TILE_SIZE, map_pos.y + screen_y + TILE_SIZE),
                                    IM_COL32(200, 200, 200, 255)
                                );
                            }
                        }
                    }
                }
            }
            {
                std::lock_guard<std::mutex> lk(heat_track_mtx);
                
                std::vector<TrackPoint> points_to_show;
                if (selected_pci_filter == -1) {
                    for (const auto& [pci, points] : heat_tracks) {
                        for (const auto& p : points) {
                            points_to_show.push_back(p);
                        }
                    }
                } else {
                    auto it = heat_tracks.find(selected_pci_filter);
                    if (it != heat_tracks.end()) {
                        points_to_show = std::vector<TrackPoint>(it->second.begin(), it->second.end());
                    }
                }
                
                if (show_all_points) {
                    if (selected_pci_filter == -1) {
                        for (const auto& [pci, points] : heat_tracks) {
                            if (points.size() < 2) continue;
                            std::vector<ImVec2> screen_points;
                            for (size_t idx = 0; idx < points.size(); idx++) {
                                const auto& p = points[idx];
                                double x = lonToX(p.lon, current_zoom) + offset_x;
                                double y = latToY(p.lat, current_zoom) + offset_y;
                                if (x >= -20 && x <= map_size.x + 20 && y >= -20 && y <= map_size.y + 20) {
                                    screen_points.push_back(ImVec2(map_pos.x + x, map_pos.y + y));
                                } else {
                                    if (screen_points.size() > 1) {
                                        draw_list->AddPolyline(screen_points.data(), screen_points.size(),
                                            rsrpToColor(points[idx - 1].rsrp), false, 2.5f);
                                    }
                                    screen_points.clear();
                                }
                            }
                            if (screen_points.size() > 1) {
                                draw_list->AddPolyline(screen_points.data(), screen_points.size(),
                                    rsrpToColor(points.back().rsrp), false, 2.5f);
                            }
                        }
                    } else {
                        if (points_to_show.size() > 1) {
                            std::vector<ImVec2> screen_points;
                            for (size_t idx = 0; idx < points_to_show.size(); idx++) {
                                const auto& p = points_to_show[idx];
                                double x = lonToX(p.lon, current_zoom) + offset_x;
                                double y = latToY(p.lat, current_zoom) + offset_y;
                                if (x >= -20 && x <= map_size.x + 20 && y >= -20 && y <= map_size.y + 20) {
                                    screen_points.push_back(ImVec2(map_pos.x + x, map_pos.y + y));
                                } else {
                                    if (screen_points.size() > 1) {
                                        draw_list->AddPolyline(screen_points.data(), screen_points.size(),
                                            rsrpToColor(points_to_show[idx - 1].rsrp), false, 3.0f);
                                    }
                                    screen_points.clear();
                                }
                            }
                            if (screen_points.size() > 1) {
                                draw_list->AddPolyline(screen_points.data(), screen_points.size(),
                                    rsrpToColor(points_to_show.back().rsrp), false, 3.0f);
                            }
                        }
                    }
                    
                    for (const auto& p : points_to_show) {
                        double x = lonToX(p.lon, current_zoom) + offset_x;
                        double y = latToY(p.lat, current_zoom) + offset_y;
                        if (x >= -20 && x <= map_size.x + 20 && y >= -20 && y <= map_size.y + 20) {
                            ImVec2 pos(map_pos.x + x, map_pos.y + y);
                            draw_list->AddCircleFilled(pos, 6.0f, IM_COL32(0, 100, 200, 200), 12);
                        }
                    }
                } else {
                    float point_radius = 20.0f;
                    for (const auto& p : points_to_show) {
                        double x = lonToX(p.lon, current_zoom) + offset_x;
                        double y = latToY(p.lat, current_zoom) + offset_y;
                        if (x >= -20 && x <= map_size.x + 20 && y >= -20 && y <= map_size.y + 20) {
                            ImVec2 pos(map_pos.x + x, map_pos.y + y);
                            ImU32 color = rsrpToColor(p.rsrp);
                            draw_list->AddCircleFilled(pos, point_radius, color, 16);
                        }
                    }
                }
            }
            
            double current_pixel_x = lonToX(currentLon, current_zoom) + offset_x;
            double current_pixel_y = latToY(currentLat, current_zoom) + offset_y;
            
            if (current_pixel_x > 0 && current_pixel_x < map_size.x && 
                current_pixel_y > 0 && current_pixel_y < map_size.y) {
                draw_list->AddCircleFilled(ImVec2(map_pos.x + current_pixel_x, map_pos.y + current_pixel_y), 
                                          8, IM_COL32(255, 0, 0, 255), 12);
                draw_list->AddCircle(ImVec2(map_pos.x + current_pixel_x, map_pos.y + current_pixel_y), 
                                    12, IM_COL32(255, 255, 255, 255), 12, 2.0f);
            }
            
            if (!show_all_points) {
                ImVec2 legend_pos = ImVec2(map_pos.x + map_size.x - 130, map_pos.y + map_size.y - 90);
                draw_list->AddRectFilled(ImVec2(legend_pos.x - 5, legend_pos.y - 5), 
                                         ImVec2(legend_pos.x + 125, legend_pos.y + 85), 
                                         IM_COL32(0,0,0,180), 4);
                draw_list->AddText(ImVec2(legend_pos.x, legend_pos.y), IM_COL32(255,255,255,255), "RSRP Heatmap");
                float ly = legend_pos.y + 20;
                int BAR_W = 100, BAR_H = 12;
                for (int px = 0; px < BAR_W; px++) {
                    float t = (float)px / BAR_W;
                    int rsrp_val = (int)(-110.0f + t * 30.0f);
                    ImU32 c = rsrpToColor(rsrp_val);
                    draw_list->AddRectFilled(ImVec2(legend_pos.x + px, ly), ImVec2(legend_pos.x + px + 1, ly + BAR_H), c);
                }
                draw_list->AddText(ImVec2(legend_pos.x, ly + BAR_H + 2), IM_COL32(100,180,255,255), "-110");
                draw_list->AddText(ImVec2(legend_pos.x + 72, ly + BAR_H + 2), IM_COL32(255,80,80,255), "-80");
            }
            
            if (ImGui::IsWindowHovered()) {
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                    if (drag_delta.x != 0 || drag_delta.y != 0) {
                        double pixels_per_degree_x = (1 << current_zoom) * TILE_SIZE / 360.0;
                        double pixels_per_degree_y = (1 << current_zoom) * TILE_SIZE / (2 * M_PI);
                        
                        map_center_lon -= drag_delta.x / pixels_per_degree_x;
                        map_center_lat += drag_delta.y / pixels_per_degree_y;
                        
                        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                    }
                }
                
                float mouse_wheel = ImGui::GetIO().MouseWheel;
                if (mouse_wheel != 0) {
                    int new_zoom = current_zoom + (mouse_wheel > 0 ? 1 : -1);
                    if (new_zoom >= 3 && new_zoom <= 18) {
                        current_zoom = new_zoom;
                    }
                }
            }
            
            ImGui::Dummy(map_size);
            ImGui::End();
        }

        if (show_gps_plots) {
            ImGui::Begin("GPS & Altitude Plot", &show_gps_plots, ImGuiWindowFlags_AlwaysAutoResize);
            
            if (ImPlot::BeginPlot("Latitude & Longitude vs Time", ImVec2(800, 400))) {
                ImPlot::SetupAxes("Time (samples)", "Value");
                ImPlot::SetupLegend(ImPlotLocation_NorthWest);
                
                std::lock_guard<std::mutex> lock(locationInfo.mtx);
                if (locationInfo.lat_history.size() > 0) {
                    std::vector<float> indices(locationInfo.lat_history.size());
                    std::vector<float> lats(locationInfo.lat_history.begin(), locationInfo.lat_history.end());
                    std::vector<float> lons(locationInfo.lon_history.begin(), locationInfo.lon_history.end());
                    
                    for (size_t i = 0; i < indices.size(); i++) {
                        indices[i] = static_cast<float>(i);
                    }
                    
                    ImPlot::PlotLine("Latitude", indices.data(), lats.data(), lats.size());
                    ImPlot::PlotLine("Longitude", indices.data(), lons.data(), lons.size());
                }
                ImPlot::EndPlot();
            }
            
            if (ImPlot::BeginPlot("Altitude vs Time", ImVec2(800, 300))) {
                ImPlot::SetupAxes("Time (samples)", "Altitude (m)");
                
                std::lock_guard<std::mutex> lock(locationInfo.mtx);
                if (locationInfo.alt_history.size() > 0) {
                    std::vector<float> indices(locationInfo.alt_history.size());
                    std::vector<float> alts(locationInfo.alt_history.begin(), locationInfo.alt_history.end());
                    
                    for (size_t i = 0; i < indices.size(); i++) {
                        indices[i] = static_cast<float>(i);
                    }
                    ImPlot::PlotLine("Altitude", indices.data(), alts.data(), alts.size());
                }
                ImPlot::EndPlot();
            }
            
            ImGui::End();
        }

        if (show_network_plots) {
            ImGui::Begin("Network Plots", &show_network_plots, ImGuiWindowFlags_AlwaysAutoResize);
            
            if (ImPlot::BeginPlot("RSRP vs Time", ImVec2(600, 250))) {
                ImPlot::SetupAxes("Time (samples)", "RSRP (dBm)");
                ImPlot::SetupAxisLimits(ImAxis_Y1, -110, -80);
                
                std::lock_guard<std::mutex> lock(locationInfo.mtx);
                if (locationInfo.rsrp_history.size() > 0) {
                    std::vector<float> indices(locationInfo.rsrp_history.size());
                    std::vector<float> rsrp_values;
                    
                    for (auto val : locationInfo.rsrp_history) {
                        rsrp_values.push_back(static_cast<float>(val));
                    }
                    
                    for (size_t i = 0; i < indices.size(); i++) {
                        indices[i] = static_cast<float>(i);
                    }
                    ImPlot::PlotLine("RSRP", indices.data(), rsrp_values.data(), rsrp_values.size());
                }
                ImPlot::EndPlot();
            }
            
            if (ImPlot::BeginPlot("RSRQ vs Time", ImVec2(600, 200))) {
                ImPlot::SetupAxes("Time (samples)", "RSRQ (dB)");
                ImPlot::SetupAxisLimits(ImAxis_Y1, -20, -3);
                
                std::lock_guard<std::mutex> lock(locationInfo.mtx);
                if (locationInfo.rsrq_history.size() > 0) {
                    std::vector<float> indices(locationInfo.rsrq_history.size());
                    std::vector<float> rsrq_values;
                    
                    for (auto val : locationInfo.rsrq_history) {
                        rsrq_values.push_back(static_cast<float>(val));
                    }
                    
                    for (size_t i = 0; i < indices.size(); i++) {
                        indices[i] = static_cast<float>(i);
                    }
                    ImPlot::PlotLine("RSRQ", indices.data(), rsrq_values.data(), rsrq_values.size());
                }
                ImPlot::EndPlot();
            }
            
            if (ImPlot::BeginPlot("RSSI vs Time", ImVec2(600, 200))) {
                ImPlot::SetupAxes("Time (samples)", "RSSI (dBm)");
                ImPlot::SetupAxisLimits(ImAxis_Y1, -120, -50);
                
                std::lock_guard<std::mutex> lock(locationInfo.mtx);
                if (locationInfo.rssi_history.size() > 0) {
                    std::vector<float> indices(locationInfo.rssi_history.size());
                    std::vector<float> rssi_values;
                    
                    for (auto val : locationInfo.rssi_history) {
                        rssi_values.push_back(static_cast<float>(val));
                    }
                    
                    for (size_t i = 0; i < indices.size(); i++) {
                        indices[i] = static_cast<float>(i);
                    }
                    ImPlot::PlotLine("RSSI", indices.data(), rssi_values.data(), rssi_values.size());
                }
                ImPlot::EndPlot();
            }
            
            ImGui::End();
        }

        if (show_history) {
            ImGui::Begin("History", &show_history, ImGuiWindowFlags_AlwaysAutoResize);
            
            std::lock_guard<std::mutex> lock(locationInfo.mtx);
            
            if (ImGui::BeginTable("History Table", 10, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(1100, 400))) {
                ImGui::TableSetupColumn("#");
                ImGui::TableSetupColumn("Time");
                ImGui::TableSetupColumn("Timestamp");
                ImGui::TableSetupColumn("Latitude");
                ImGui::TableSetupColumn("Longitude");
                ImGui::TableSetupColumn("Altitude");
                ImGui::TableSetupColumn("Network");
                ImGui::TableSetupColumn("RSRP");
                ImGui::TableSetupColumn("RSRQ");
                ImGui::TableSetupColumn("RSSI");
                ImGui::TableHeadersRow();
                
                int start = std::max(0, (int)locationInfo.lat_history.size() - 50);
                for (int i = start; i < locationInfo.lat_history.size(); i++) {
                    ImGui::TableNextRow();
                    
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d", i + 1);
                    
                    ImGui::TableSetColumnIndex(1);
                    if (i < locationInfo.time_history.size())
                        ImGui::Text("%s", locationInfo.time_history[i].c_str());
                    else
                        ImGui::Text("--");
                    
                    ImGui::TableSetColumnIndex(2);
                    if (i < locationInfo.timestamp_history.size()) {
                        ImGui::Text("%lld", locationInfo.timestamp_history[i]);
                    } else {
                        ImGui::Text("--");
                    }
                    
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.6f", locationInfo.lat_history[i]);
                    
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%.6f", locationInfo.lon_history[i]);
                    
                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%.2f", locationInfo.alt_history[i]);
                    
                    ImGui::TableSetColumnIndex(6);
                    if (i < locationInfo.network_type_history.size())
                        ImGui::Text("%s", locationInfo.network_type_history[i].c_str());
                    else
                        ImGui::Text("--");
                    
                    ImGui::TableSetColumnIndex(7);
                    if (i < locationInfo.rsrp_history.size()) {
                        int rsrp_val = locationInfo.rsrp_history[i];
                        ImVec4 color;
                        if (rsrp_val > -80) color = ImVec4(1,0,0,1);
                        else if (rsrp_val > -90) color = ImVec4(1,0.5f,0,1);
                        else if (rsrp_val > -100) color = ImVec4(0,1,0,1);
                        else color = ImVec4(0,0.5f,1,1);
                        ImGui::TextColored(color, "%d", rsrp_val);
                    } else {
                        ImGui::Text("--");
                    }
                    
                    ImGui::TableSetColumnIndex(8);
                    if (i < locationInfo.rsrq_history.size()) {
                        ImGui::Text("%d", locationInfo.rsrq_history[i]);
                    } else {
                        ImGui::Text("--");
                    }
                    
                    ImGui::TableSetColumnIndex(9);
                    if (i < locationInfo.rssi_history.size()) {
                        ImGui::Text("%d", locationInfo.rssi_history[i]);
                    } else {
                        ImGui::Text("--");
                    }
                }
                
                ImGui::EndTable();
            }
            
            ImGui::End();
        }

        if (show_settings) {
            ImGui::Begin("Settings", &show_settings);
            
            ImGui::ShowStyleEditor();
            
            if (ImGui::SliderFloat("UI Scale", &io.FontGlobalScale, 0.5f, 2.0f)) {}
            
            if (ImGui::Button("Save Settings")) {
                ImGui::SaveIniSettingsToDisk("imgui_layout.ini");
            }
            
            ImGui::Separator();
            ImGui::Text("Server: tcp://*:5566");
            ImGui::Text("Data file: %s", log_file_path);
            ImGui::Text("History points: %zu", locationInfo.lat_history.size());
            ImGui::Text("Map Zoom: %d", current_zoom);
            
            ImGui::End();
        }

        ImGui::Render();
        int display_w, display_h;
        SDL_GL_GetDrawableSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }
    
cleanup:
    locationInfo.is_running = false;
    heat_running = false;
    if (server_thread.joinable()) server_thread.join();
    if (heat_thread.joinable()) heat_thread.join();
    
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    return 0;
}