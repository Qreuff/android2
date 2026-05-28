#include "ui_panels.h"
#include <fstream>
#include <algorithm>

void UIPanels::renderLocationInfo() {
    ImGui::Begin("GPS & Network Monitor", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    
    auto& loc = LocationData::instance();
    
    ImGui::Text("GPS DATA");
    ImGui::Separator();
    ImGui::Text("Latitude:  %.6f", loc.getLat());
    ImGui::Text("Longitude: %.6f", loc.getLon());
    ImGui::Text("Altitude:  %.2f m", loc.getAlt());
    ImGui::Text("Time:      %s", loc.getTimeStr().c_str());
    ImGui::Text("Timestamp: %lld", loc.getTimestamp());
    
    ImGui::Spacing();
    ImGui::Separator();
    
    ImGui::Text("NETWORK DATA");
    ImGui::Separator();
    ImGui::Text("Device:    %s", loc.getDeviceID().c_str());
    ImGui::Text("Provider:  %s", loc.getOperatorName().c_str());
    ImGui::Text("Network:   %s", loc.getNetworkType().c_str());
    ImGui::Text("IP:        %s", loc.getIPAddress().c_str());
    ImGui::Text("Frequency: %d MHz", loc.getFrequency());
    
    ImGui::Spacing();
    
    ImGui::Text("SIGNAL PARAMETERS");
    ImGui::Separator();
    ImGui::Text("RSRP:      %d dBm  (%s)", loc.getRSRP(), getSignalQuality(loc.getRSRP()).c_str());
    ImGui::Text("RSRQ:      %d dB", loc.getRSRQ());
    ImGui::Text("RSSI:      %d dBm", loc.getRSSI());
    
    float quality = std::max(0.0f, std::min(1.0f, (loc.getRSRP() + 110.0f) / 30.0f));
    ImGui::ProgressBar(quality, ImVec2(200, 20), 
                      ("Signal: " + std::to_string(static_cast<int>(quality * 100)) + "%").c_str());
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Server:    Online (port 5566)");
    ImGui::Text("History:   %zu points", loc.getLatHistory().size());
    ImGui::Text("PCI count: %zu", loc.getPCIHistorySize());
    
    ImGui::End();
}

void UIPanels::renderPCISelector(int& selected_pci_filter, bool& show_all_points,
                                  std::vector<int>& all_pci_list) {
    ImGui::Begin("PCI Selector", nullptr, 
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
        std::map<int, std::deque<TrackPoint>> tracks;
        HeatWorker::instance().getTracks(tracks);
        
        auto it = tracks.find(selected_pci_filter);
        if (it != tracks.end()) {
            int samples = it->second.size();
            int last_rsrp = samples > 0 ? it->second.back().rsrp : -110;
            
            ImGui::Text("Statistics");
            ImGui::Text("Samples: %d", samples);
            ImGui::Text("Last RSRP: %d dBm", last_rsrp);
            
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float bar_width = 180;
            
            for (int px = 0; px < static_cast<int>(bar_width); px++) {
                float t = static_cast<float>(px) / bar_width;
                int rsrp_val = static_cast<int>(-110.0f + t * 30.0f);
                ImU32 c = rsrpToColor(rsrp_val);
                draw_list->AddRectFilled(
                    ImVec2(pos.x + px, pos.y),
                    ImVec2(pos.x + px + 1, pos.y + 20), c);
            }
            
            ImGui::Dummy(ImVec2(bar_width, 20));
            ImGui::Text(" -110                     -80");
        }
    } else {
        ImGui::Text("Total PCI: %zu", all_pci_list.size());
        ImGui::Text("Total points: %zu", HeatWorker::instance().getTotalPoints());
        ImGui::Text("Mode: %s", show_all_points ? "All Points" : "Heatmap");
    }
    
    ImGui::End();
}

void UIPanels::renderGPSPlots(bool& show_gps_plots) {
    if (!show_gps_plots) return;
    
    ImGui::Begin("GPS & Altitude Plot", &show_gps_plots, ImGuiWindowFlags_AlwaysAutoResize);
    
    auto& loc = LocationData::instance();
    
    if (ImPlot::BeginPlot("Latitude & Longitude vs Time", ImVec2(800, 400))) {
        ImPlot::SetupAxes("Time (samples)", "Value");
        ImPlot::SetupLegend(ImPlotLocation_NorthWest);
        
        const auto& lat_history = loc.getLatHistory();
        const auto& lon_history = loc.getLonHistory();
        
        if (!lat_history.empty()) {
            std::vector<float> indices(lat_history.size());
            std::vector<float> lats(lat_history.begin(), lat_history.end());
            std::vector<float> lons(lon_history.begin(), lon_history.end());
            
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
        
        const auto& alt_history = loc.getAltHistory();
        
        if (!alt_history.empty()) {
            std::vector<float> indices(alt_history.size());
            std::vector<float> alts(alt_history.begin(), alt_history.end());
            
            for (size_t i = 0; i < indices.size(); i++) {
                indices[i] = static_cast<float>(i);
            }
            
            ImPlot::PlotLine("Altitude", indices.data(), alts.data(), alts.size());
        }
        
        ImPlot::EndPlot();
    }
    
    ImGui::End();
}

void UIPanels::renderNetworkPlots(bool& show_network_plots) {
    if (!show_network_plots) return;
    
    ImGui::Begin("Network Plots", &show_network_plots, ImGuiWindowFlags_AlwaysAutoResize);
    
    auto& loc = LocationData::instance();
    
    if (ImPlot::BeginPlot("RSRP vs Time", ImVec2(600, 250))) {
        ImPlot::SetupAxes("Time (samples)", "RSRP (dBm)");
        ImPlot::SetupAxisLimits(ImAxis_Y1, -110, -80);
        
        const auto& rsrp_history = loc.getRSRPHistory();
        
        if (!rsrp_history.empty()) {
            std::vector<float> indices(rsrp_history.size());
            std::vector<float> rsrp_values;
            
            for (auto val : rsrp_history) {
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
        
        const auto& rsrq_history = loc.getRSRQHistory();
        
        if (!rsrq_history.empty()) {
            std::vector<float> indices(rsrq_history.size());
            std::vector<float> rsrq_values;
            
            for (auto val : rsrq_history) {
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
        
        const auto& rssi_history = loc.getRSSIHistory();
        
        if (!rssi_history.empty()) {
            std::vector<float> indices(rssi_history.size());
            std::vector<float> rssi_values;
            
            for (auto val : rssi_history) {
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

void UIPanels::renderHistory(bool& show_history) {
    if (!show_history) return;
    
    ImGui::Begin("History", &show_history, ImGuiWindowFlags_AlwaysAutoResize);
    
    auto& loc = LocationData::instance();
    
    if (ImGui::BeginTable("History Table", 10, 
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, 
        ImVec2(1100, 400))) {
        
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
        
        const auto& lat_history = loc.getLatHistory();
        const auto& lon_history = loc.getLonHistory();
        const auto& alt_history = loc.getAltHistory();
        const auto& timestamp_history = loc.getTimestampHistory();
        const auto& rsrp_history = loc.getRSRPHistory();
        const auto& rsrq_history = loc.getRSRQHistory();
        const auto& rssi_history = loc.getRSSIHistory();
        const auto& time_history = loc.getTimeHistory();
        const auto& network_type_history = loc.getNetworkTypeHistory();
        
        int start = std::max(0, static_cast<int>(lat_history.size()) - 50);
        
        for (int i = start; i < static_cast<int>(lat_history.size()); i++) {
            ImGui::TableNextRow();
            
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", i + 1);
            
            ImGui::TableSetColumnIndex(1);
            if (i < static_cast<int>(time_history.size()))
                ImGui::Text("%s", time_history[i].c_str());
            else
                ImGui::Text("--");
            
            ImGui::TableSetColumnIndex(2);
            if (i < static_cast<int>(timestamp_history.size()))
                ImGui::Text("%lld", timestamp_history[i]);
            else
                ImGui::Text("--");
            
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.6f", lat_history[i]);
            
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.6f", lon_history[i]);
            
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%.2f", alt_history[i]);
            
            ImGui::TableSetColumnIndex(6);
            if (i < static_cast<int>(network_type_history.size()))
                ImGui::Text("%s", network_type_history[i].c_str());
            else
                ImGui::Text("--");
            
            ImGui::TableSetColumnIndex(7);
            if (i < static_cast<int>(rsrp_history.size())) {
                int rsrp_val = rsrp_history[i];
                ImVec4 color;
                if (rsrp_val > -80) color = ImVec4(1, 0, 0, 1);
                else if (rsrp_val > -90) color = ImVec4(1, 0.5f, 0, 1);
                else if (rsrp_val > -100) color = ImVec4(0, 1, 0, 1);
                else color = ImVec4(0, 0.5f, 1, 1);
                ImGui::TextColored(color, "%d", rsrp_val);
            } else {
                ImGui::Text("--");
            }
            
            ImGui::TableSetColumnIndex(8);
            if (i < static_cast<int>(rsrq_history.size()))
                ImGui::Text("%d", rsrq_history[i]);
            else
                ImGui::Text("--");
            
            ImGui::TableSetColumnIndex(9);
            if (i < static_cast<int>(rssi_history.size()))
                ImGui::Text("%d", rssi_history[i]);
            else
                ImGui::Text("--");
        }
        
        ImGui::EndTable();
    }
    
    ImGui::End();
}

void UIPanels::renderSettings(bool& show_settings, float& ui_scale,
                               int current_zoom, const char* log_file_path) {
    if (!show_settings) return;
    
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
    ImGui::Text("Data file: %s", log_file_path);
    ImGui::Text("History points: %zu", LocationData::instance().getLatHistory().size());
    ImGui::Text("Map Zoom: %d", current_zoom);
    
    ImGui::End();
}