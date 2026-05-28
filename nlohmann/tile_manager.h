#pragma once
#include "config.h"
#include <GL/glew.h>
#include <curl/curl.h>
#include <png.h>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <filesystem>

struct TileTexture {
    GLuint texture_id = 0;
    int width = 0;
    int height = 0;
    bool loaded = false;
    std::vector<unsigned char> pixel_data;
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
public:
    static OSMTileManager& instance();
    
    void requestTile(int x, int y, int zoom);
    Tile* getTile(int x, int y, int zoom);
    void uploadTexturesToGPU();
    void clearGPUResources();

private:
    OSMTileManager();
    ~OSMTileManager();
    OSMTileManager(const OSMTileManager&) = delete;
    OSMTileManager& operator=(const OSMTileManager&) = delete;

    std::string getTileKey(int x, int y, int zoom) const;
    std::string getTilePath(int x, int y, int zoom) const;
    bool downloadTile(int x, int y, int zoom, const std::string& save_path);
    bool loadPNGToMemory(const std::string& file_path, 
                         std::vector<unsigned char>& pixel_data, 
                         int& width, int& height);
    void loadCacheFromDisk();
    void processDownloadQueue();
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* data);

    std::string cache_dir_;
    std::map<std::string, Tile> tiles_;
    std::mutex tiles_mutex_;
    CURL* curl_ = nullptr;
    std::atomic<bool> running_{true};
    std::thread download_thread_;
};