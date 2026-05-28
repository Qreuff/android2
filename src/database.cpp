#include "database.h"
#include "location_data.h"
#include "heat_worker.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <pqxx/pqxx>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace std;

const string DB_CONN = "host=127.0.0.1 port=5533 dbname=vis user=myuser password=mypassword";
bool check_db_alive() {
    try {
        pqxx::connection c(DB_CONN);
        return c.is_open();
    } catch (...) {
        return false;
    }
}

void init_database() {
    auto& loc = LocationData::instance();
    
    if (check_db_alive()) {
        cout << "PostgreSQL connected" << endl;
        sync_all_data();
    } else {
        cout << "PostgreSQL unavailable, loading from JSON" << endl;
        loc.loadFromFile("location.json");
    }
}

void sync_all_data() {
    auto& loc = LocationData::instance();
    
    loc.lat_history.clear();
    loc.lon_history.clear();
    loc.alt_history.clear();
    loc.timestamp_history.clear();
    loc.rsrp_history.clear();
    loc.rsrq_history.clear();
    loc.rssi_history.clear();
    loc.time_history.clear();
    loc.network_type_history.clear();
    loc.frequency_history.clear();
    loc.pci_history.clear();
    loc.pci_names.clear();
    loc.pci_track.clear();
    loc.current_cells.clear();
    
    HeatWorker::instance().clearTracks();

    try {
        pqxx::connection c(DB_CONN);
        pqxx::nontransaction N(c);

        pqxx::result res = N.exec(
            "SELECT latitude, longitude, altitude, timestamp_ms, rsrp, rsrq, rssi, "
            "frequency, network_type, ip_address, operator_name, device_id "
            "FROM measurements ORDER BY timestamp_ms ASC"
        );

        for (auto const &row : res) {
            loc.lat_history.push_back(row["latitude"].as<float>());
            loc.lon_history.push_back(row["longitude"].as<float>());
            loc.alt_history.push_back(row["altitude"].as<float>());
            loc.timestamp_history.push_back(row["timestamp_ms"].as<long long>());
            loc.rsrp_history.push_back(row["rsrp"].as<int>());
            loc.rsrq_history.push_back(row["rsrq"].as<int>());
            loc.rssi_history.push_back(row["rssi"].as<int>());
            loc.frequency_history.push_back(row["frequency"].as<int>());
            loc.network_type_history.push_back(row["network_type"].as<string>());
            
            auto tp = chrono::system_clock::from_time_t(row["timestamp_ms"].as<long long>() / 1000);
            auto tt = chrono::system_clock::to_time_t(tp);
            stringstream ss;
            ss << put_time(localtime(&tt), "%H:%M:%S");
            loc.time_history.push_back(ss.str());
        }

        pqxx::result cell_res = N.exec(
            "SELECT m.timestamp_ms, m.latitude, m.longitude, "
            "c.pci, c.rsrp, c.rsrq, c.rssi, c.earfcn "
            "FROM cell_data c "
            "JOIN measurements m ON c.measurement_id = m.id "
            "ORDER BY m.timestamp_ms ASC"
        );

        for (auto const &row : cell_res) {
            int pci = row["pci"].as<int>();
            int rsrp = row["rsrp"].as<int>();
            float lat = row["latitude"].as<float>();
            float lon = row["longitude"].as<float>();

            loc.pci_history[pci].push_back(rsrp);
            loc.pci_names[pci] = "PCI " + to_string(pci);
            loc.pci_track[pci].push_back({lat, lon, rsrp, pci});

            vector<HeatPoint> pts;
            pts.push_back({pci, lat, lon, rsrp});
            HeatWorker::instance().addPoints(pts);
        }

        if (!loc.lat_history.empty()) {
            loc.lat = loc.lat_history.back();
            loc.lon = loc.lon_history.back();
            loc.alt = loc.alt_history.back();
            loc.timestamp = loc.timestamp_history.back();
            loc.rsrp = loc.rsrp_history.back();
            loc.rsrq = loc.rsrq_history.back();
            loc.rssi = loc.rssi_history.back();
            loc.frequency = loc.frequency_history.back();
            loc.network_type = loc.network_type_history.back();
            loc.time_str = loc.time_history.back();
        }

        while (loc.lat_history.size() > loc.max_history) {
            loc.lat_history.pop_front();
            loc.lon_history.pop_front();
            loc.alt_history.pop_front();
            loc.timestamp_history.pop_front();
            loc.rsrp_history.pop_front();
            loc.rsrq_history.pop_front();
            loc.rssi_history.pop_front();
            loc.time_history.pop_front();
            loc.network_type_history.pop_front();
            loc.frequency_history.pop_front();
        }

        cout << "Synced from SQL. Points: " << res.size() << endl;

    } catch (const exception &e) {
        cerr << "SQL sync failed: " << e.what() << endl;
        loc.loadFromFile("location.json");
    }
}

void save_packet(const string& raw_json) {
    {
        ofstream log_file("location.json", ios::app);
        if (log_file.is_open()) {
            log_file << raw_json << endl;
        }
    }

    if (check_db_alive()) {
        try {
            json j = json::parse(raw_json);
            pqxx::connection c(DB_CONN);
            pqxx::work W(c);

            const json* src = &j;
            if (j.contains("location") && j["location"].is_object()) {
                src = &j["location"];
            }

            float lat = src->value("latitude", src->value("lat", 0.0));
            float lon = src->value("longitude", src->value("lon", 0.0));
            float alt = src->value("altitude", src->value("alt", 0.0));
            float accuracy = j.value("accuracy", 0.0);
            long long ts = src->value("time", j.value("timestamp", 0LL));
            int rsrp = j.value("rsrp", -110);
            int rsrq = j.value("rsrq", -20);
            int rssi = j.value("rssi", -100);
            int freq = j.value("frequency", 0);
            string net_type = j.value("network_type", j.value("net_type", "Unknown"));
            string ip = j.value("ip_address", "0.0.0.0");
            string oper = j.value("provider", j.value("operator", "Unknown"));
            string device = j.value("device_id", "Unknown");

            pqxx::result res = W.exec_params(
                "INSERT INTO measurements (latitude, longitude, altitude, accuracy, "
                "timestamp_ms, rsrp, rsrq, rssi, frequency, network_type, "
                "ip_address, operator_name, device_id) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13) RETURNING id",
                lat, lon, alt, accuracy, ts, rsrp, rsrq, rssi, freq, net_type, ip, oper, device
            );

            int m_id = res[0][0].as<int>();

            if (j.contains("cells") && j["cells"].is_array()) {
                for (const auto& cell : j["cells"]) {
                    W.exec_params(
                        "INSERT INTO cell_data (measurement_id, cell_type, band, cell_identity, "
                        "earfcn, mcc, mnc, pci, tac, asu_level, cqi, rsrp, rsrq, rssi, rssnr, timing_advance) "
                        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16)",
                        m_id,
                        cell.value("type", "LTE"),
                        cell.value("band", ""),
                        cell.value("cell_identity", 0),
                        cell.value("earfcn", 0),
                        cell.value("mcc", 0),
                        cell.value("mnc", 0),
                        cell.value("pci", 0),
                        cell.value("tac", 0),
                        cell.value("asu_level", 0),
                        cell.value("cqi", 0),
                        cell.value("rsrp", -110),
                        cell.value("rsrq", -20),
                        cell.value("rssi", -100),
                        cell.value("rssnr", 0),
                        cell.value("timing_advance", 0)
                    );
                }
            }

            if (j.contains("telephony") && j["telephony"].is_array()) {
                for (const auto& cell : j["telephony"]) {
                    string c_type = cell.value("type", "Unknown");
                    
                    if (c_type == "LTE") {
                        const auto& id = cell.value("CellIdentityLte", json::object());
                        const auto& sig = cell.value("CellSignalStrengthLte", json::object());
                        
                        W.exec_params(
                            "INSERT INTO cell_data (measurement_id, cell_type, band, cell_identity, "
                            "earfcn, mcc, mnc, pci, tac, asu_level, cqi, rsrp, rsrq, rssi, rssnr, timing_advance) "
                            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16)",
                            m_id,
                            c_type,
                            id.value("band", ""),
                            id.value("cell_identity", id.value("ci", 0)),
                            id.value("earfcn", 0),
                            id.value("mcc", 0),
                            id.value("mnc", 0),
                            id.value("pci", -1),
                            id.value("tac", 0),
                            sig.value("asu_level", 0),
                            sig.value("cqi", 0),
                            sig.value("rsrp", sig.value("RSRP", -110)),
                            sig.value("rsrq", sig.value("RSRQ", -20)),
                            sig.value("rssi", sig.value("RSSI", -100)),
                            sig.value("rssnr", sig.value("RSSNR", 0)),
                            sig.value("timing_advance", 0)
                        );
                    } else if (c_type == "GSM") {
                        const auto& id = cell.value("CellIdentityGsm", json::object());
                        const auto& sig = cell.value("CellSignalStrengthGsm", json::object());
                        
                        W.exec_params(
                            "INSERT INTO cell_data (measurement_id, cell_type, band, cell_identity, "
                            "earfcn, mcc, mnc, pci, tac, asu_level, cqi, rsrp, rsrq, rssi, rssnr, timing_advance) "
                            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16)",
                            m_id,
                            c_type,
                            id.value("band", ""),
                            id.value("cell_identity", id.value("cid", 0)),
                            id.value("earfcn", id.value("arfcn", 0)),
                            id.value("mcc", 0),
                            id.value("mnc", 0),
                            id.value("pci", id.value("bsic", -1)),
                            id.value("tac", id.value("lac", 0)),
                            sig.value("asu_level", 0),
                            sig.value("cqi", 0),
                            sig.value("rsrp", sig.value("dbm", -110)),
                            sig.value("rsrq", -20),
                            sig.value("rssi", sig.value("rssi", -100)),
                            sig.value("rssnr", 0),
                            sig.value("timing_advance", 0)
                        );
                    } else if (c_type == "WCDMA") {
                        const auto& id = cell.value("CellIdentityWcdma", json::object());
                        const auto& sig = cell.value("CellSignalStrengthWcdma", json::object());
                        
                        W.exec_params(
                            "INSERT INTO cell_data (measurement_id, cell_type, band, cell_identity, "
                            "earfcn, mcc, mnc, pci, tac, asu_level, cqi, rsrp, rsrq, rssi, rssnr, timing_advance) "
                            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16)",
                            m_id,
                            c_type,
                            id.value("band", ""),
                            id.value("cell_identity", id.value("cid", 0)),
                            id.value("earfcn", id.value("uarfcn", 0)),
                            id.value("mcc", 0),
                            id.value("mnc", 0),
                            id.value("pci", id.value("psc", -1)),
                            id.value("tac", id.value("lac", 0)),
                            sig.value("asu_level", 0),
                            sig.value("cqi", 0),
                            sig.value("rsrp", sig.value("dbm", -110)),
                            sig.value("rsrq", -20),
                            sig.value("rssi", sig.value("rssi", -100)),
                            sig.value("rssnr", 0),
                            sig.value("timing_advance", 0)
                        );
                    }
                }
            }

            W.commit();
            cout << "[DB] Saved to SQL. Measurement ID: " << m_id << endl;

        } catch (const exception &e) {
            cerr << "[DB] Save to SQL failed: " << e.what() << endl;
        }
    }

    LocationData::instance().updateFromJson(json::parse(raw_json));
}

void migrate_json_to_sql() {
    if (!check_db_alive()) {
        cerr << "Cannot migrate: DB unreachable" << endl;
        return;
    }

    try {
        pqxx::connection c(DB_CONN);
        pqxx::work W(c);

        W.exec("TRUNCATE measurements CASCADE;");

        ifstream log_file("location.json");
        string line;
        int count = 0;

        while (getline(log_file, line)) {
            if (line.empty()) continue;

            try {
                json j = json::parse(line);

                const json* src = &j;
                if (j.contains("location") && j["location"].is_object()) {
                    src = &j["location"];
                }

                float lat = src->value("latitude", src->value("lat", 0.0));
                float lon = src->value("longitude", src->value("lon", 0.0));
                float alt = src->value("altitude", src->value("alt", 0.0));
                float accuracy = j.value("accuracy", 0.0);
                long long ts = src->value("time", j.value("timestamp", 0LL));
                int rsrp = j.value("rsrp", -110);
                int rsrq = j.value("rsrq", -20);
                int rssi = j.value("rssi", -100);
                int freq = j.value("frequency", 0);
                string net_type = j.value("network_type", j.value("net_type", "Unknown"));
                string ip = j.value("ip_address", "0.0.0.0");
                string oper = j.value("provider", j.value("operator", "Unknown"));
                string device = j.value("device_id", "Unknown");

                pqxx::result res = W.exec_params(
                    "INSERT INTO measurements (latitude, longitude, altitude, accuracy, "
                    "timestamp_ms, rsrp, rsrq, rssi, frequency, network_type, "
                    "ip_address, operator_name, device_id) "
                    "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13) RETURNING id",
                    lat, lon, alt, accuracy, ts, rsrp, rsrq, rssi, freq, net_type, ip, oper, device
                );

                int m_id = res[0][0].as<int>();

                if (j.contains("cells") && j["cells"].is_array()) {
                    for (const auto& cell : j["cells"]) {
                        W.exec_params(
                            "INSERT INTO cell_data (measurement_id, cell_type, band, cell_identity, "
                            "earfcn, mcc, mnc, pci, tac, asu_level, cqi, rsrp, rsrq, rssi, rssnr, timing_advance) "
                            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16)",
                            m_id,
                            cell.value("type", "LTE"),
                            cell.value("band", ""),
                            cell.value("cell_identity", 0),
                            cell.value("earfcn", 0),
                            cell.value("mcc", 0),
                            cell.value("mnc", 0),
                            cell.value("pci", 0),
                            cell.value("tac", 0),
                            cell.value("asu_level", 0),
                            cell.value("cqi", 0),
                            cell.value("rsrp", -110),
                            cell.value("rsrq", -20),
                            cell.value("rssi", -100),
                            cell.value("rssnr", 0),
                            cell.value("timing_advance", 0)
                        );
                    }
                }

                if (j.contains("telephony") && j["telephony"].is_array()) {
                    for (const auto& cell : j["telephony"]) {
                        string c_type = cell.value("type", "Unknown");
                        
                        if (c_type == "LTE") {
                            const auto& id = cell.value("CellIdentityLte", json::object());
                            const auto& sig = cell.value("CellSignalStrengthLte", json::object());
                            
                            W.exec_params(
                                "INSERT INTO cell_data (measurement_id, cell_type, band, cell_identity, "
                                "earfcn, mcc, mnc, pci, tac, asu_level, cqi, rsrp, rsrq, rssi, rssnr, timing_advance) "
                                "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16)",
                                m_id, c_type,
                                id.value("band", ""),
                                id.value("cell_identity", id.value("ci", 0)),
                                id.value("earfcn", 0),
                                id.value("mcc", 0),
                                id.value("mnc", 0),
                                id.value("pci", -1),
                                id.value("tac", 0),
                                sig.value("asu_level", 0),
                                sig.value("cqi", 0),
                                sig.value("rsrp", sig.value("RSRP", -110)),
                                sig.value("rsrq", sig.value("RSRQ", -20)),
                                sig.value("rssi", sig.value("RSSI", -100)),
                                sig.value("rssnr", sig.value("RSSNR", 0)),
                                sig.value("timing_advance", 0)
                            );
                        }
                    }
                }

                count++;
                if (count % 100 == 0) {
                    cout << "Migrated " << count << " records..." << endl;
                }

            } catch (const exception &e) {
                cerr << "Error migrating line: " << e.what() << endl;
            }
        }

        W.commit();
        cout << "Migration complete. Records: " << count << endl;

        sync_all_data();

    } catch (const exception &e) {
        cerr << "Migration failed: " << e.what() << endl;
    }
}