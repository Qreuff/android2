#include "tile_manager.h"
#include <fstream>
#include <iostream>
#include <cstring>

OSMTileManager& OSMTileManager::instance() {
    static OSMTileManager instance;
    return instance;
}

OSMTileManager::OSMTileManager() : cache_dir_(CACHE_DIR) {
    try {
        std::filesystem::create_directories(cache_dir_);
        std::cout << "Cache directory created: " << cache_dir_ << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error creating cache dir: " << e.what() << std::endl;
    }
    
    curl_ = curl_easy_init();
    if (!curl_) {
        std::cerr << "Failed to initialize CURL" << std::endl;
    } else {
        std::cout << "CURL initialized successfully" << std::endl;
    }
    
    loadCacheFromDisk();
    
    download_thread_ = std::thread([this]() {
        std::cout << "Download thread started" << std::endl;
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            processDownloadQueue();
        }
        std::cout << "Download thread stopped" << std::endl;
    });
}

OSMTileManager::~OSMTileManager() {
    std::cout << "Shutting down tile manager..." << std::endl;
    running_ = false;
    if (download_thread_.joinable()) {
        download_thread_.join();
    }
    
    clearGPUResources();
    
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
    
    std::cout << "Tile manager shut down complete" << std::endl;
}

std::string OSMTileManager::getTileKey(int x, int y, int zoom) const {
    return std::to_string(zoom) + "_" + std::to_string(x) + "_" + std::to_string(y);
}

std::string OSMTileManager::getTilePath(int x, int y, int zoom) const {
    return cache_dir_ + "/" + std::to_string(zoom) + "/" + 
           std::to_string(x) + "_" + std::to_string(y) + ".png";
}

size_t OSMTileManager::writeCallback(void* contents, size_t size, size_t nmemb, std::string* data) {
    size_t totalSize = size * nmemb;
    data->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

bool OSMTileManager::downloadTile(int x, int y, int zoom, const std::string& save_path) {
    std::string url = "https://tile.openstreetmap.org/" + 
                     std::to_string(zoom) + "/" + 
                     std::to_string(x) + "/" + 
                     std::to_string(y) + ".png";
    
    std::string response_data;
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, &OSMTileManager::writeCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl_, CURLOPT_USERAGENT, "GPS-Network-Monitor/1.0");
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
    
    CURLcode res = curl_easy_perform(curl_);
    
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
    } else {
        std::cerr << "CURL error: " << curl_easy_strerror(res) << std::endl;
    }
    return false;
}

bool OSMTileManager::loadPNGToMemory(const std::string& file_path, 
                                      std::vector<unsigned char>& pixel_data, 
                                      int& width, int& height) {
    FILE* fp = fopen(file_path.c_str(), "rb");
    if (!fp) {
        return false;
    }
    
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        fclose(fp);
        return false;
    }
    
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        fclose(fp);
        return false;
    }
    
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        return false;
    }
    
    png_init_io(png, fp);
    png_read_info(png, info);
    
    width = png_get_image_width(png, info);
    height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);
    
    if (bit_depth == 16) {
        png_set_strip_16(png);
    }
    
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }
    
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }
    
    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png);
    }
    
    if (color_type == PNG_COLOR_TYPE_RGB || 
        color_type == PNG_COLOR_TYPE_GRAY || 
        color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
    }
    
    png_read_update_info(png, info);
    
    size_t row_bytes = png_get_rowbytes(png, info);
    pixel_data.resize(row_bytes * height);
    
    png_bytep* row_pointers = static_cast<png_bytep*>(malloc(sizeof(png_bytep) * height));
    if (!row_pointers) {
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        return false;
    }
    
    for (int y = 0; y < height; y++) {
        row_pointers[y] = pixel_data.data() + (y * row_bytes);
    }
    
    png_read_image(png, row_pointers);
    
    fclose(fp);
    free(row_pointers);
    png_destroy_read_struct(&png, &info, nullptr);
    
    return true;
}

void OSMTileManager::loadCacheFromDisk() {
    std::lock_guard<std::mutex> lock(tiles_mutex_);
    
    if (!std::filesystem::exists(cache_dir_)) {
        std::cout << "Cache directory does not exist: " << cache_dir_ << std::endl;
        return;
    }
    
    int loaded_count = 0;
    
    try {
        for (const auto& zoom_dir : std::filesystem::directory_iterator(cache_dir_)) {
            if (!zoom_dir.is_directory()) {
                continue;
            }
            
            int zoom = 0;
            try {
                zoom = std::stoi(zoom_dir.path().filename().string());
            } catch (const std::exception& e) {
                std::cerr << "Invalid zoom directory: " << zoom_dir.path().filename() << std::endl;
                continue;
            }
            
            for (const auto& tile_file : std::filesystem::directory_iterator(zoom_dir.path())) {
                if (tile_file.path().extension() != ".png") {
                    continue;
                }
                
                std::string filename = tile_file.path().stem().string();
                size_t underscore_pos = filename.find('_');
                if (underscore_pos == std::string::npos) {
                    continue;
                }
                
                int x = 0, y = 0;
                try {
                    x = std::stoi(filename.substr(0, underscore_pos));
                    y = std::stoi(filename.substr(underscore_pos + 1));
                } catch (const std::exception& e) {
                    std::cerr << "Invalid tile filename: " << filename << std::endl;
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
                
                int width = 0, height = 0;
                if (loadPNGToMemory(tile_file.path().string(), tile.texture.pixel_data, width, height)) {
                    tile.texture.width = width;
                    tile.texture.height = height;
                    tile.loaded = true;
                    loaded_count++;
                }
                
                tiles_[key] = tile;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading cache: " << e.what() << std::endl;
    }
    
    std::cout << "Loaded " << loaded_count << " cached tiles (total in memory: " << tiles_.size() << ")" << std::endl;
}

void OSMTileManager::processDownloadQueue() {
    std::lock_guard<std::mutex> lock(tiles_mutex_);
    
    for (auto& [key, tile] : tiles_) {
        if (!tile.loaded && !tile.is_loading) {
            tile.is_loading = true;
            
            try {
                std::string save_path = getTilePath(tile.x, tile.y, tile.zoom);
                
                if (downloadTile(tile.x, tile.y, tile.zoom, save_path)) {
                    int width = 0, height = 0;
                    std::vector<unsigned char> pixel_data;
                    if (loadPNGToMemory(save_path, pixel_data, width, height)) {
                        tile.texture.pixel_data = std::move(pixel_data);
                        tile.texture.width = width;
                        tile.texture.height = height;
                        tile.loaded = true;
                        tile.needs_upload = true;
                        std::cout << "Downloaded tile: " << tile.zoom << "/" 
                                  << tile.x << "_" << tile.y << std::endl;
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

void OSMTileManager::uploadTexturesToGPU() {
    std::lock_guard<std::mutex> lock(tiles_mutex_);
    
    for (auto& [key, tile] : tiles_) {
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

void OSMTileManager::clearGPUResources() {
    std::lock_guard<std::mutex> lock(tiles_mutex_);
    for (auto& [key, tile] : tiles_) {
        if (tile.texture.texture_id != 0) {
            glDeleteTextures(1, &tile.texture.texture_id);
            tile.texture.texture_id = 0;
        }
    }
}

void OSMTileManager::requestTile(int x, int y, int zoom) {
    if (x < 0 || y < 0 || zoom < 0) {
        return;
    }
    
    std::string key = getTileKey(x, y, zoom);
    std::lock_guard<std::mutex> lock(tiles_mutex_);
    
    if (tiles_.find(key) == tiles_.end()) {
        Tile tile;
        tile.x = x;
        tile.y = y;
        tile.zoom = zoom;
        tile.loaded = false;
        tile.is_loading = false;
        tile.needs_upload = false;
        tiles_[key] = tile;
    }
    
    tiles_[key].last_used = std::chrono::steady_clock::now();
}

Tile* OSMTileManager::getTile(int x, int y, int zoom) {
    std::string key = getTileKey(x, y, zoom);
    std::lock_guard<std::mutex> lock(tiles_mutex_);
    
    auto it = tiles_.find(key);
    if (it != tiles_.end() && it->second.loaded && it->second.texture.texture_id != 0) {
        return &it->second;
    }
    return nullptr;
}