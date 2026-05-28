#include "config.h"
#include "tile_manager.h"
#include "heat_worker.h"
#include "location_data.h"
#include "server.h"
#include "map_renderer.h"
#include "ui_panels.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

#include <iostream>
#include <thread>
#include <chrono>

int main(int argc, char *argv[]) {
    std::cout << "Starting GPS & Network Monitor with OSM Map..." << std::endl;
    
    auto& locationInfo = LocationData::instance();
    locationInfo.getRunningFlag() = true;
    
    char log_file_path[512] = LOG_FILE;
    locationInfo.loadFromFile(log_file_path);
    
    {
        auto pci_tracks = locationInfo.getPCITracks();
        for (const auto& [pci, dq] : pci_tracks) {
            std::vector<HeatPoint> pts;
            for (const auto& tp : dq) {
                pts.push_back({tp.pci, tp.lat, tp.lon, tp.rsrp});
            }
            HeatWorker::instance().addPoints(pts);
        }
    }
    
    float map_center_lat = NOVOSIBIRSK_LAT;
    float map_center_lon = NOVOSIBIRSK_LON;
    int current_zoom = DEFAULT_ZOOM;
    
    if (locationInfo.getLat() != 0.0f || locationInfo.getLon() != 0.0f) {
        map_center_lat = locationInfo.getLat();
        map_center_lon = locationInfo.getLon();
        std::cout << "Setting map center to last known position: " 
                  << map_center_lat << ", " << map_center_lon << std::endl;
    } else {
        std::cout << "No position data found, using Novosibirsk: " 
                  << NOVOSIBIRSK_LAT << ", " << NOVOSIBIRSK_LON << std::endl;
    }
    
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
    
    ImPlotColormap signal_colormap = ImPlot::AddColormap(
        "SignalStrength", signal_colors, IM_ARRAYSIZE(signal_colors), false
    );
    
    ZMQServer server;
    try {
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Failed to start server: " << e.what() << std::endl;
    }
    
    MapRenderer map_renderer;
    
    bool show_map = true;
    bool show_settings = false;
    bool show_gps_plots = true;
    bool show_network_plots = true;
    bool show_history = true;
    bool show_pci_panel = true;
    
    int selected_pci_filter = -1;
    bool show_all_points = true;
    std::vector<int> all_pci_list;
    
    all_pci_list = HeatWorker::instance().getPCIList();
    
    auto last_time = std::chrono::steady_clock::now();
    
    while (locationInfo.getRunningFlag()) {
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time - last_time).count();
        
        if (elapsed < 16) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16 - elapsed));
        }
        last_time = current_time;
        
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                locationInfo.getRunningFlag() = false;
                goto cleanup;
            }
        }
        
        OSMTileManager::instance().uploadTexturesToGPU();
        
        {
            auto cells = locationInfo.getCurrentCells();
            if (!cells.empty()) {
                std::vector<HeatPoint> pts;
                for (const auto& cell : cells) {
                    pts.push_back({cell.pci, locationInfo.getLat(), 
                                  locationInfo.getLon(), cell.rsrp});
                }
                HeatWorker::instance().addPoints(pts);
            }
        }
        
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit")) {
                    locationInfo.getRunningFlag() = false;
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
        
        UIPanels::renderLocationInfo();
        
        ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        {
            std::ifstream test(log_file_path);
            if (test.is_open()) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "File: OK  [%s]", log_file_path);
                test.close();
            } else {
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "File NOT FOUND: [%s]", log_file_path);
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
            locationInfo.loadFromFile(log_file_path);
            HeatWorker::instance().clearTracks();
            
            auto pci_tracks = locationInfo.getPCITracks();
            for (const auto& [pci, dq] : pci_tracks) {
                std::vector<HeatPoint> pts;
                for (const auto& tp : dq) {
                    pts.push_back({tp.pci, tp.lat, tp.lon, tp.rsrp});
                }
                HeatWorker::instance().addPoints(pts);
            }
            
            all_pci_list = HeatWorker::instance().getPCIList();
        }
        
        ImGui::Spacing();
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("Log file", log_file_path, sizeof(log_file_path));
        ImGui::SameLine();
        if (ImGui::Button("Load")) {
            locationInfo.loadFromFile(log_file_path);
            HeatWorker::instance().clearTracks();
            
            auto pci_tracks = locationInfo.getPCITracks();
            for (const auto& [pci, dq] : pci_tracks) {
                std::vector<HeatPoint> pts;
                for (const auto& tp : dq) {
                    pts.push_back({tp.pci, tp.lat, tp.lon, tp.rsrp});
                }
                HeatWorker::instance().addPoints(pts);
            }
            
            all_pci_list = HeatWorker::instance().getPCIList();
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Center Map")) {
            map_center_lat = locationInfo.getLat();
            map_center_lon = locationInfo.getLon();
        }
        ImGui::SameLine();
        if (ImGui::Button("Novosibirsk")) {
            map_center_lat = NOVOSIBIRSK_LAT;
            map_center_lon = NOVOSIBIRSK_LON;
            current_zoom = DEFAULT_ZOOM;
        }
        
        ImGui::End();
        if (show_map) {
            map_renderer.render(map_center_lat, map_center_lon, current_zoom,
                               selected_pci_filter, show_all_points);
        }
        
        if (show_pci_panel) {
            UIPanels::renderPCISelector(selected_pci_filter, show_all_points, all_pci_list);
        }
        
        UIPanels::renderGPSPlots(show_gps_plots);
        UIPanels::renderNetworkPlots(show_network_plots);
        UIPanels::renderHistory(show_history);
        
        if (show_settings) {
            UIPanels::renderSettings(show_settings, io.FontGlobalScale, 
                                    current_zoom, log_file_path);
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
    locationInfo.getRunningFlag() = false;
    server.stop();
    
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    return 0;
}