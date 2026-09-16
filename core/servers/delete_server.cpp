//
// Created by Adafa Ralph on 6/24/26.
//

#include "../utils/utils.h"
#include "servers.h"
#include "serverEntity.h"
#include <string>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include "nlohmann/json.hpp"

namespace fs = std::filesystem;
namespace jlib = nlohmann;

void delete_server(const std::string& server_id) {
    // 1. Stop if running
    stop_server(server_id);

    // 2. Remove from db.json
    std::string resource_path = get_resource_path();
    fs::path db_path = fs::path(resource_path) / "db.json";
    if (fs::exists(db_path)) {
        try {
            std::ifstream reader(db_path);
            if (reader.is_open()) {
                jlib::json dump;
                reader >> dump;
                reader.close();
                if (dump.is_array()) {
                    jlib::json updated = jlib::json::array();
                    for (const auto& item : dump) {
                        if (item.is_object() && item.contains("id")) {
                            std::string item_id;
                            if (item["id"].is_string()) {
                                item_id = item["id"].get<std::string>();
                            } else if (item["id"].is_number()) {
                                item_id = std::to_string(item["id"].get<long long>());
                            }
                            if (item_id == server_id) {
                                continue;
                            }
                        }
                        updated.push_back(item);
                    }
                    std::ofstream writer(db_path);
                    if (writer.is_open()) {
                        writer << updated.dump(4);
                        writer.close();
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error removing server from db.json: " << e.what() << std::endl;
        }
    }

    // 3. Remove server files from servers/<server_id>
    fs::path server_dir = fs::path(resource_path) / "servers" / server_id;
    if (fs::exists(server_dir)) {
        std::error_code ec;
        fs::remove_all(server_dir, ec);
    }
}
