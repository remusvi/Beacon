//
// Created by Adafa Ralph on 6/24/26.
//

#include <ostream>
#include "serverEntity.h"
#include "servers.h"
#include "../utils/utils.h"
#include "../containers/container.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
namespace jlib = nlohmann;

jlib::json get_servers() {
    fs::path db_path(get_resource_path());
    db_path /= "db.json";

    if (!fs::exists(db_path)) {
        fs::create_directories(db_path.parent_path());
        std::ofstream db(db_path, std::ios::binary);
        if (!db.is_open()) {
            std::cerr << "Failed to create DB file " << db_path.string() << std::endl;
        } else {
            db << "[]";
            db.close();
        }
    }

    std::ifstream filestream(db_path);
    std::vector<serverEntity> servers;

    if (filestream.is_open()) {
        try {
            jlib::json j;
            filestream >> j;
            if (j.is_array()) {
                servers = j.get<std::vector<serverEntity>>();
            }
        } catch (const std::exception& e) {
            std::cerr << "JSON Error in get_servers: " << e.what() << std::endl;
        }
        filestream.close();
    }

    // Sync live process status & paths
    std::string resource_path = get_resource_path();
    for (auto& server : servers) {
        if (is_server_running(server.id)) {
            server.status = "RUNNING";
        } else {
            server.status = "OFFLINE";
        }

        // Normalize instance_path to current environment if needed
        fs::path local_jar = fs::path(resource_path) / "servers" / server.id / "server.jar";
        if (fs::exists(local_jar)) {
            server.instance_path = local_jar.string();
        }
    }

    jlib::json server_json = servers;
    return server_json;
}
