//
// Created by Adafa Ralph on 6/22/26.
//

#include <filesystem>
#include <fstream>
#include <iostream>

#include "container.h"
#include "../utils/utils.h"
#include "../servers/serverEntity.h"

namespace fs = std::filesystem;

std::string create_container(const Container& container, bool online_mode, const std::string& version_path) {
    std::string container_path = get_resource_path();
    fs::path containder_dir(container_path);

    fs::path server_dir = containder_dir / "servers" / container.server_id;

    if (!fs::exists(server_dir)) {
        fs::create_directories(server_dir);
    }

    fs::path server_properties = server_dir / "server.properties";
    fs::path eula = server_dir / "eula.txt";

    std::ofstream filestream(server_properties);
    std::ofstream eulastream(eula);

    if (filestream.is_open()) {
        filestream << "server-port=" << container.port << std::endl;
        filestream << "max-players=20" << std::endl;
        filestream << "online-mode=" << (online_mode ? "true" : "false") << std::endl;
        filestream << "gamemode=survival" << std::endl;
        filestream.close();
    }

    if (eulastream.is_open()) {
        eulastream << "eula=true" << std::endl;
        eulastream.close();
    }

    // Symlink binary
    fs::path server = server_dir / "server.jar";
    std::error_code ec;
    if (fs::exists(server, ec) || fs::is_symlink(server, ec)) {
        fs::remove(server, ec);
    }
    fs::create_symlink(version_path, server, ec);
    if (ec) {
        // Fallback to copy if symlink creation is not permitted
        fs::copy_file(version_path, server, fs::copy_options::overwrite_existing, ec);
    }

    return server.string();
}
