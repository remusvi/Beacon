#include "../utils/utils.h"
#include "servers.h"
#include "serverEntity.h"
#include <string>
#include <filesystem>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <fstream>
#if __has_include(<boost/process/v1.hpp>)
#include <boost/process/v1.hpp>
namespace bp = boost::process::v1;
#else
#include <boost/process.hpp>
namespace bp = boost::process;
#endif

#include <thread>
#include <chrono>

namespace fs = std::filesystem;
namespace jlib = nlohmann;

struct ServerProcess {
    std::shared_ptr<bp::child> process;
    std::shared_ptr<bp::opstream> in;
};

static std::unordered_map<std::string, ServerProcess> running_servers;
static std::mutex server_mutex;

bool is_server_running(const std::string& server_id) {
    std::lock_guard<std::mutex> lock(server_mutex);
    auto it = running_servers.find(server_id);
    if (it != running_servers.end() && it->second.process) {
        if (it->second.process->running()) {
            return true;
        } else {
            running_servers.erase(it);
            return false;
        }
    }
    return false;
}

bool send_server_command(const std::string& server_id, const std::string& command) {
    std::lock_guard<std::mutex> lock(server_mutex);
    auto it = running_servers.find(server_id);
    if (it != running_servers.end() && it->second.process && it->second.process->running() && it->second.in) {
        try {
            *(it->second.in) << command << std::endl;
            it->second.in->flush();
            std::cout << "Sent command to server " << server_id << ": " << command << std::endl;
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Failed to send command to server " << server_id << ": " << e.what() << std::endl;
        }
    }
    return false;
}

void start_server(const std::string& server_id) {
    if (is_server_running(server_id)) {
        std::cout << "Server " << server_id << " is already running." << std::endl;
        return;
    }

    std::string resource_path = get_resource_path();
    fs::path resource_dir(resource_path);
    fs::path server_dir = resource_dir / "servers" / server_id;
    fs::path binary_dir = server_dir / "server.jar";

    if (!fs::exists(binary_dir)) {
        throw std::runtime_error("Server jar not found at: " + binary_dir.string());
    }

    // Determine RAM from db.json if available
    int ram_gb = 3;
    fs::path db_path = resource_dir / "db.json";
    if (fs::exists(db_path)) {
        try {
            std::ifstream db_file(db_path);
            if (db_file.is_open()) {
                jlib::json data;
                db_file >> data;
                if (data.is_array()) {
                    for (const auto& item : data) {
                        if (item.value("id", "") == server_id) {
                            if (item.contains("ram") && item["ram"].is_number()) {
                                ram_gb = item["ram"].get<int>();
                            }
                            break;
                        }
                    }
                }
            }
        } catch (...) {}
    }

    std::string xmx = "-Xmx" + std::to_string(ram_gb > 0 ? ram_gb : 2) + "G";
    std::string xms = "-Xms1G";

    std::string java_bin = "java";
    try {
        auto p = bp::search_path("java");
        if (!p.empty()) {
            java_bin = p.string();
        }
    } catch (...) {}

    std::cout << "Launching server " << server_id << " with " << xmx << " in " << server_dir.string() << std::endl;

    auto in_stream = std::make_shared<bp::opstream>();
    auto child = std::make_shared<bp::child>(
        java_bin,
        xms,
        xmx,
        "-jar",
        binary_dir.string(),
        "nogui",
        bp::std_in < *in_stream,
        bp::start_dir(server_dir.string())
    );

    ServerProcess sp;
    sp.process = child;
    sp.in = in_stream;

    std::lock_guard<std::mutex> lock(server_mutex);
    running_servers[server_id] = sp;
}

void stop_server(const std::string& server_id) {
    std::lock_guard<std::mutex> lock(server_mutex);
    auto it = running_servers.find(server_id);
    if (it != running_servers.end() && it->second.process) {
        if (it->second.process->running()) {
            std::cout << "Stopping server " << server_id << "..." << std::endl;
            if (it->second.in) {
                try {
                    *(it->second.in) << "stop" << std::endl;
                    it->second.in->flush();
                } catch (...) {}
            }
            int waited_ms = 0;
            while (it->second.process->running() && waited_ms < 2500) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                waited_ms += 100;
            }
            if (it->second.process->running()) {
                it->second.process->terminate();
            }
        }
        running_servers.erase(it);
    }
}
