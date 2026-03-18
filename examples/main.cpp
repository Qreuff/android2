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

#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

using json = nlohmann::json;

struct LocationData {
    float lat = 0.0f;
    float lon = 0.0f;
    float alt = 0.0f;
    long long timestamp = 0;
    std::string time_str;
    std::mutex mtx;        
    volatile bool is_running;
    
    std::deque<float> lat_history;
    std::deque<float> lon_history;
    std::deque<float> alt_history;
    std::deque<std::string> time_history;
    const size_t max_history = 100;
};

void run_server(LocationData* loc) {
    
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);
    
    try {
        socket.bind("tcp://*:5566");
        std::cout << "✓ Socket bound successfully" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "✗ Bind Error: " << e.what() << std::endl;
        return;
    }

    socket.set(zmq::sockopt::rcvtimeo, 1000);
    
    std::cout << "Waiting for connections... (Ctrl+C to stop)" << std::endl;
    
    while (loc->is_running) {
        zmq::message_t request;
        auto res = socket.recv(request, zmq::recv_flags::none);
        
        if (res) {
            std::string msg_str(static_cast<char*>(request.data()), request.size());
            
            try {
                auto j = json::parse(msg_str);
                
                auto now = std::chrono::system_clock::now();
                auto now_time_t = std::chrono::system_clock::to_time_t(now);
                std::stringstream time_ss;
                time_ss << std::put_time(std::localtime(&now_time_t), "%H:%M:%S");
                
                {
                    std::lock_guard<std::mutex> lock(loc->mtx);
                    
                    loc->lat = j["latitude"].get<double>();
                    loc->lon = j["longitude"].get<double>();
                    loc->alt = j["altitude"].get<double>();
                    loc->timestamp = j["time"].get<long long>();
                    loc->time_str = time_ss.str();
                    
                    loc->lat_history.push_back(loc->lat);
                    loc->lon_history.push_back(loc->lon);
                    loc->alt_history.push_back(loc->alt);
                    loc->time_history.push_back(loc->time_str);
                    
                    if (loc->lat_history.size() > loc->max_history) {
                        loc->lat_history.pop_front();
                        loc->lon_history.pop_front();
                        loc->alt_history.pop_front();
                        loc->time_history.pop_front();
                    }
                }

                std::ofstream file("location_log.json", std::ios::app);
                file << j.dump() << std::endl;
                file.close();

                socket.send(zmq::str_buffer("OK"), zmq::send_flags::none);
                
            } catch (const std::exception& e) {
                std::cerr << "✗ Error: " << e.what() << std::endl;
                socket.send(zmq::str_buffer("Error"), zmq::send_flags::none);
            }
        }
    }
}

void loadHistoryFromFile(LocationData* loc) {
    std::ifstream file("location_log.json");
    if (!file.is_open()) return;
    
    std::string line;
    std::vector<json> all_data;
    
    while (std::getline(file, line)) {
        try {
            auto j = json::parse(line);
            all_data.push_back(j);
        } catch (...) {}
    }
    file.close();
    
    int start = std::max(0, (int)all_data.size() - 100);
    for (int i = start; i < all_data.size(); i++) {
        auto& j = all_data[i];
        loc->lat_history.push_back(j["latitude"].get<double>());
        loc->lon_history.push_back(j["longitude"].get<double>());
        loc->alt_history.push_back(j["altitude"].get<double>());
        loc->time_history.push_back("");
    }
}

int main(int argc, char *argv[]) {
    static LocationData locationInfo;
    locationInfo.is_running = true;
    
    loadHistoryFromFile(&locationInfo);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL Init failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window* window = SDL_CreateWindow(
        "GPS Server with Graphics",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    glewExperimental = GL_TRUE;
    glewInit();

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

    bool running = true;
    bool show_settings = false;
    bool show_plots = true;
    bool show_history = true;
    float ui_scale = 1.0f;
    
    std::vector<double> times;
    std::vector<double> lats, lons, alts;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit")) running = false;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Windows")) {
                ImGui::MenuItem("Location Info", nullptr, &show_settings);
                ImGui::MenuItem("Plots", nullptr, &show_plots);
                ImGui::MenuItem("History", nullptr, &show_history);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        ImGui::Begin("GPS Data Monitor", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        
        float currentLat, currentLon, currentAlt;
        long long currentTime;
        std::string currentTimeStr;
        {
            std::lock_guard<std::mutex> lock(locationInfo.mtx);
            currentLat = locationInfo.lat;
            currentLon = locationInfo.lon;
            currentAlt = locationInfo.alt;
            currentTime = locationInfo.timestamp;
            currentTimeStr = locationInfo.time_str;
        }

        ImGui::Text("Current GPS Data:");
        ImGui::Separator();
        ImGui::Text("Latitude:  %.6f", currentLat);
        ImGui::Text("Longitude: %.6f", currentLon);
        ImGui::Text("Altitude:  %.2f m", currentAlt);
        ImGui::Text("Time:      %s", currentTimeStr.c_str());
        ImGui::Text("Timestamp: %lld", currentTime);
        
        ImGui::Separator();
        ImGui::Text("Server status: Online (port 5566)");
        ImGui::Text("History size: %zu points", locationInfo.lat_history.size());
        
        if (ImGui::Button("Clear Log")) {
            std::ofstream file("location_log.json", std::ios::trunc);
            file.close();
        }
        ImGui::SameLine();
        if (ImGui::Button("Settings")) {
            show_settings = true;
        }
        
        ImGui::End();

        if (show_plots) {
            ImGui::Begin("GPS Plots", &show_plots, ImGuiWindowFlags_AlwaysAutoResize);
            
            if (ImPlot::BeginPlot("Latitude / Longitude", ImVec2(500, 300))) {
                ImPlot::SetupAxes("Longitude", "Latitude");
                ImPlot::SetupAxisLimits(ImAxis_X1, -180, 180);
                ImPlot::SetupAxisLimits(ImAxis_Y1, -90, 90);
                
                std::lock_guard<std::mutex> lock(locationInfo.mtx);
                if (!locationInfo.lon_history.empty() && !locationInfo.lat_history.empty()) {
                    std::vector<float> lons(locationInfo.lon_history.begin(), locationInfo.lon_history.end());
                    std::vector<float> lats(locationInfo.lat_history.begin(), locationInfo.lat_history.end());
                    
                    ImPlot::PlotLine("Path", lons.data(), lats.data(), lons.size());
                    
                    if (!lons.empty()) {
                        float last_lon = lons.back();
                        float last_lat = lats.back();
                        ImPlot::PlotScatter("Current", &last_lon, &last_lat, 1);
                    }
                }
                ImPlot::EndPlot();
            }
            
            if (ImPlot::BeginPlot("Altitude", ImVec2(500, 200))) {
                ImPlot::SetupAxes("Time", "Altitude (m)");
                
                std::lock_guard<std::mutex> lock(locationInfo.mtx);
                if (!locationInfo.alt_history.empty()) {
                    std::vector<float> indices(locationInfo.alt_history.size());
                    std::vector<float> alts(locationInfo.alt_history.begin(), locationInfo.alt_history.end());
                    
                    for (size_t i = 0; i < indices.size(); i++) {
                        indices[i] = i;
                    }
                    
                    ImPlot::PlotLine("Altitude", indices.data(), alts.data(), alts.size());
                }
                ImPlot::EndPlot();
            }
            
            ImGui::End();
        }

        if (show_history) {
            ImGui::Begin("History", &show_history, ImGuiWindowFlags_AlwaysAutoResize);
            
            std::lock_guard<std::mutex> lock(locationInfo.mtx);
            
            if (ImGui::BeginTable("History Table", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(600, 300))) {
                ImGui::TableSetupColumn("#");
                ImGui::TableSetupColumn("Time");
                ImGui::TableSetupColumn("Latitude");
                ImGui::TableSetupColumn("Longitude");
                ImGui::TableSetupColumn("Altitude");
                ImGui::TableHeadersRow();
                
                int start = std::max(0, (int)locationInfo.lat_history.size() - 20);
                for (int i = start; i < locationInfo.lat_history.size(); i++) {
                    ImGui::TableNextRow();
                    
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d", i + 1);
                    
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", locationInfo.time_history[i].c_str());
                    
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.6f", locationInfo.lat_history[i]);
                    
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.6f", locationInfo.lon_history[i]);
                    
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%.2f", locationInfo.alt_history[i]);
                }
                
                ImGui::EndTable();
            }
            
            ImGui::End();
        }

        if (show_settings) {
            ImGui::Begin("Settings", &show_settings);
            
            ImGui::ShowStyleEditor();
            
            if (ImGui::SliderFloat("UI Scale", &ui_scale, 0.5f, 2.0f)) {
                ImGui::GetIO().FontGlobalScale = ui_scale;
            }
            
            if (ImGui::Button("Save Settings")) {
                ImGui::SaveIniSettingsToDisk("imgui_layout.ini");
            }
            
            ImGui::Separator();
            ImGui::Text("Server: tcp://*:5566");
            ImGui::Text("Data file: location_log.json");
            ImGui::Text("History points: %zu", locationInfo.lat_history.size());
            
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
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    locationInfo.is_running = false; 
    if (server_thread.joinable()) {
        server_thread.join();
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}