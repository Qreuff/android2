#pragma once
#include "config.h"
#include "tile_manager.h"
#include "heat_worker.h"
#include <imgui.h>
#include <vector>
#include <map>
#include <deque>

class MapRenderer {
public:
    MapRenderer();
    
    void render(float& map_center_lat, float& map_center_lon, int& current_zoom,
                int selected_pci_filter, bool show_all_points);
    
    void handleInteraction(float& map_center_lat, float& map_center_lon, int& current_zoom);

private:
    void renderTiles(ImDrawList* draw_list, ImVec2 map_pos, ImVec2 map_size,
                     float map_center_lon, float map_center_lat, int current_zoom);
    
    void renderTracks(ImDrawList* draw_list, ImVec2 map_pos, ImVec2 map_size,
                      float map_center_lon, float map_center_lat, int current_zoom,
                      int selected_pci_filter, bool show_all_points);
    
    void renderCurrentPosition(ImDrawList* draw_list, ImVec2 map_pos, ImVec2 map_size,
                               float map_center_lon, float map_center_lat, int current_zoom);
    
    void renderLegend(ImDrawList* draw_list, ImVec2 map_pos, ImVec2 map_size, bool show_all_points);
    
    double offset_x_ = 0.0;
    double offset_y_ = 0.0;
};