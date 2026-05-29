#pragma once
#include <string>
#include <atomic>

void init_database();
void save_packet(const std::string& raw_json);
void sync_all_data();
void migrate_json_to_sql();
bool check_db_alive();