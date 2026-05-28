#include "map_renderer.h"
#include "location_data.h"
#include <cmath>

MapRenderer::MapRenderer() {
}

void MapRenderer::render(float& map_center_lat, float& map_center_lon, int& current_zoom,
                         int selected_pci_filter, bool show_all_points) {
    if (!ImGui::Begin("OpenStreetMap")) {
        ImGui::End();
        return;
    }
    
    ImVec2 map_pos = ImGui::GetCursorScreenPos();
    ImVec2 map_size = ImGui::GetContentRegionAvail();
    if (map_size.x < 100) map_size.x = 800;
    if (map_size.y < 100) map_size.y = 600;
    
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    double center_pixel_x = lonToX(map_center_lon, current_zoom);
    double center_pixel_y = latToY(map_center_lat, current_zoom);
    
    offset_x_ = map_size.x / 2.0 - center_pixel_x;
    offset_y_ = map_size.y / 2.0 - center_pixel_y;
    
    renderTiles(draw_list, map_pos, map_size, map_center_lon, map_center_lat, current_zoom);
    renderTracks(draw_list, map_pos, map_size, map_center_lon, map_center_lat, 
                 current_zoom, selected_pci_filter, show_all_points);
    renderCurrentPosition(draw_list, map_pos, map_size, map_center_lon, map_center_lat, current_zoom);
    
    if (!show_all_points) {
        renderLegend(draw_list, map_pos, map_size, show_all_points);
    }
    
    handleInteraction(map_center_lat, map_center_lon, current_zoom);
    
    ImGui::Dummy(map_size);
    ImGui::End();
}

void MapRenderer::renderTiles(ImDrawList* draw_list, ImVec2 map_pos, ImVec2 map_size,
                               float map_center_lon, float map_center_lat, int current_zoom) {
    const int TILE_SIZE = 256;
    
    int start_tile_x = static_cast<int>((-offset_x_) / TILE_SIZE) - 1;
    int start_tile_y = static_cast<int>((-offset_y_) / TILE_SIZE) - 1;
    int end_tile_x = start_tile_x + static_cast<int>(map_size.x / TILE_SIZE) + 2;
    int end_tile_y = start_tile_y + static_cast<int>(map_size.y / TILE_SIZE) + 2;
    
    int max_tiles = 1 << current_zoom;
    
    for (int tx = start_tile_x; tx <= end_tile_x; tx++) {
        for (int ty = start_tile_y; ty <= end_tile_y; ty++) {
            if (tx >= 0 && tx < max_tiles && ty >= 0 && ty < max_tiles) {
                double screen_x = (tx * TILE_SIZE) + offset_x_;
                double screen_y = (ty * TILE_SIZE) + offset_y_;
                
                if (screen_x + TILE_SIZE > 0 && screen_x < map_size.x &&
                    screen_y + TILE_SIZE > 0 && screen_y < map_size.y) {
                    
                    OSMTileManager::instance().requestTile(tx, ty, current_zoom);
                    Tile* tile = OSMTileManager::instance().getTile(tx, ty, current_zoom);
                    
                    if (tile && tile->loaded && tile->texture.texture_id != 0) {
                        draw_list->AddImage(
                            reinterpret_cast<void*>(static_cast<intptr_t>(tile->texture.texture_id)),
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
}

void MapRenderer::renderTracks(ImDrawList* draw_list, ImVec2 map_pos, ImVec2 map_size,
                                float map_center_lon, float map_center_lat, int current_zoom,
                                int selected_pci_filter, bool show_all_points) {
    std::map<int, std::deque<TrackPoint>> tracks;
    HeatWorker::instance().getTracks(tracks);
    
    std::vector<TrackPoint> points_to_show;
    
    if (selected_pci_filter == -1) {
        for (const auto& [pci, points] : tracks) {
            for (const auto& p : points) {
                points_to_show.push_back(p);
            }
        }
    } else {
        auto it = tracks.find(selected_pci_filter);
        if (it != tracks.end()) {
            points_to_show.assign(it->second.begin(), it->second.end());
        }
    }
    
    if (points_to_show.empty()) return;
    
    if (show_all_points) {
        if (selected_pci_filter == -1) {
            for (const auto& [pci, points] : tracks) {
                if (points.size() < 2) continue;
                
                std::vector<ImVec2> screen_points;
                for (size_t idx = 0; idx < points.size(); idx++) {
                    const auto& p = points[idx];
                    double x = lonToX(p.lon, current_zoom) + offset_x_;
                    double y = latToY(p.lat, current_zoom) + offset_y_;
                    
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
                    double x = lonToX(p.lon, current_zoom) + offset_x_;
                    double y = latToY(p.lat, current_zoom) + offset_y_;
                    
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
            double x = lonToX(p.lon, current_zoom) + offset_x_;
            double y = latToY(p.lat, current_zoom) + offset_y_;
            
            if (x >= -20 && x <= map_size.x + 20 && y >= -20 && y <= map_size.y + 20) {
                ImVec2 pos(map_pos.x + x, map_pos.y + y);
                draw_list->AddCircleFilled(pos, 6.0f, IM_COL32(0, 100, 200, 200), 12);
            }
        }
        
    } else {
        // f(P) = Σ(wi · fi) / Σ(wi), где wi = 1 / d(P, Pi)^p      
        const double POWER = 2.0;
        const double SMOOTHING = 0.00001;
        const int GRID_STEP = 8;
        const double MAX_INFLUENCE = 500.0;
        
        struct ScreenPoint {
            ImVec2 pos;
            int rsrp;
        };
        
        std::vector<ScreenPoint> screen_points;
        for (const auto& p : points_to_show) {
            double x = lonToX(p.lon, current_zoom) + offset_x_;
            double y = latToY(p.lat, current_zoom) + offset_y_;
            
            if (x >= -MAX_INFLUENCE && x <= map_size.x + MAX_INFLUENCE && 
                y >= -MAX_INFLUENCE && y <= map_size.y + MAX_INFLUENCE) {
                screen_points.push_back({ImVec2(map_pos.x + x, map_pos.y + y), p.rsrp});
            }
        }
        
        if (screen_points.empty()) return;
        for (double px = map_pos.x; px < map_pos.x + map_size.x; px += GRID_STEP) {
            for (double py = map_pos.y; py < map_pos.y + map_size.y; py += GRID_STEP) {
                double sum_weighted = 0.0;
                double sum_weights = 0.0;
                
                for (const auto& sp : screen_points) {
                    double dx = px - sp.pos.x;
                    double dy = py - sp.pos.y;
                    double dist = sqrt(dx * dx + dy * dy);
                    
                    if (dist > MAX_INFLUENCE) continue;
                    
                    double wi = 1.0 / (pow(dist, POWER) + SMOOTHING);
                    
                    sum_weighted += wi * sp.rsrp;
                    sum_weights += wi;
                }
                
                if (sum_weights > 0.0) {
                    double interpolated_rsrp = sum_weighted / sum_weights;
                    
                    ImU32 color = rsrpToColor(static_cast<int>(interpolated_rsrp));
                    draw_list->AddRectFilled(
                        ImVec2(px, py),
                        ImVec2(px + GRID_STEP, py + GRID_STEP),
                        color
                    );
                }
            }
        }
    }
}

void MapRenderer::renderCurrentPosition(ImDrawList* draw_list, ImVec2 map_pos, ImVec2 map_size,
                                        float map_center_lon, float map_center_lat, int current_zoom) {
    auto& loc = LocationData::instance();
    float current_lat = loc.lat;
    float current_lon = loc.lon;
    
    if (current_lat == 0.0f && current_lon == 0.0f) return;
    
    double current_pixel_x = lonToX(current_lon, current_zoom) + offset_x_;
    double current_pixel_y = latToY(current_lat, current_zoom) + offset_y_;
    
    if (current_pixel_x > 0 && current_pixel_x < map_size.x && 
        current_pixel_y > 0 && current_pixel_y < map_size.y) {
        draw_list->AddCircleFilled(
            ImVec2(map_pos.x + current_pixel_x, map_pos.y + current_pixel_y), 
            8, IM_COL32(255, 0, 0, 255), 12
        );
        draw_list->AddCircle(
            ImVec2(map_pos.x + current_pixel_x, map_pos.y + current_pixel_y), 
            12, IM_COL32(255, 255, 255, 255), 12, 2.0f
        );
    }
}

void MapRenderer::renderLegend(ImDrawList* draw_list, ImVec2 map_pos, ImVec2 map_size, bool show_all_points) {
    if (show_all_points) return;
    
    ImVec2 legend_pos = ImVec2(map_pos.x + map_size.x - 130, map_pos.y + map_size.y - 90);
    draw_list->AddRectFilled(
        ImVec2(legend_pos.x - 5, legend_pos.y - 5), 
        ImVec2(legend_pos.x + 125, legend_pos.y + 85), 
        IM_COL32(0, 0, 0, 180), 4
    );
    
    draw_list->AddText(ImVec2(legend_pos.x, legend_pos.y), IM_COL32(255, 255, 255, 255), "RSRP Heatmap");
    
    float ly = legend_pos.y + 20;
    const int BAR_W = 100, BAR_H = 12;
    
    for (int px = 0; px < BAR_W; px++) {
        float t = static_cast<float>(px) / BAR_W;
        int rsrp_val = static_cast<int>(-110.0f + t * 30.0f);
        ImU32 c = rsrpToColor(rsrp_val);
        draw_list->AddRectFilled(
            ImVec2(legend_pos.x + px, ly), 
            ImVec2(legend_pos.x + px + 1, ly + BAR_H), c
        );
    }
    
    draw_list->AddText(ImVec2(legend_pos.x, ly + BAR_H + 2), IM_COL32(100, 180, 255, 255), "-110");
    draw_list->AddText(ImVec2(legend_pos.x + 72, ly + BAR_H + 2), IM_COL32(255, 80, 80, 255), "-80");
}

void MapRenderer::handleInteraction(float& map_center_lat, float& map_center_lon, int& current_zoom) {
    if (!ImGui::IsWindowHovered()) return;
    
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