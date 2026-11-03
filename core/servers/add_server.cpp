//
// Created by Adafa Ralph on 6/21/26.
//

#include <cstdint>
#include <iostream>
#include <ostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include "serverEntity.h"
#include "servers.h"
#include "../utils/utils.h"
#include "../containers/container.h"
#include "nlohmann/json.hpp"

namespace fs = std::filesystem;
namespace jlib = nlohmann;

std::string db_init() {
    std::string resource_dir = get_resource_path();
    fs::path resource_path(resource_dir);

    if (!fs::exists(resource_path)) {
        fs::create_directories(resource_path);
    }

    fs::path db_path = resource_path / "db.json";

    if (!fs::exists(db_path)) {
        std::ofstream dbstream(db_path);
        if (dbstream.is_open()) {
            dbstream << "[]";
            dbstream.close();
            std::cout << "created db file: " << db_path.string() << std::endl;
        }
    }

    return db_path.string();
}

void add_server(
    const std::string& id,
    const std::string& name,
    Provider provider,
    const std::string& version,
    std::int32_t ram,
    std::int32_t port,
    bool online_mode
) {
    std::string resource_dir = get_resource_path();
    std::string jar_path = ensure_jar_exists(resource_dir, version, provider);

    Container container {
        id,
        jar_path,
        port,
    };

    std::string jar_link = create_container(container, online_mode, jar_path);

    serverEntity server {
        id,
        name,
        jar_link,
        version,
        "OFFLINE",
        provider,
        std::nullopt,
        port,
        ram,
        online_mode
    };

    std::string db_path = db_init();
    std::ifstream reader(db_path);
    std::vector<serverEntity> server_list;

    if (reader.is_open()) {
        try {
            jlib::json dump;
            reader >> dump;
            if (dump.is_array()) {
                server_list = dump.get<std::vector<serverEntity>>();
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning reading DB: " << e.what() << std::endl;
        }
        reader.close();
    }

    // Replace if id already exists
    server_list.erase(
        std::remove_if(server_list.begin(), server_list.end(),
            [&](const serverEntity& s) { return s.id == id; }),
        server_list.end()
    );

    server_list.push_back(server);

    std::ofstream dbstream(db_path);
    if (dbstream.is_open()) {
        jlib::json t = server_list;
        dbstream << t.dump(4);
        dbstream.close();
    }
}
