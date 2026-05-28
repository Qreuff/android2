#pragma once
#include "config.h"
#include "location_data.h"
#include "heat_worker.h"
#include <imgui.h>
#include <implot.h>
#include <vector>
#include <string>

class UIPanels {
public:
    static void renderLocationInfo();
    static void renderPCISelector(int& selected_pci_filter, bool& show_all_points,
                                   std::vector<int>& all_pci_list);
    static void renderGPSPlots(bool& show_gps_plots);
    static void renderNetworkPlots(bool& show_network_plots);
    static void renderHistory(bool& show_history);
    static void renderSettings(bool& show_settings, float& ui_scale,
                               int current_zoom, const char* log_file_path);
};