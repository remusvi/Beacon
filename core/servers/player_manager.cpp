#include "servers.h"
#include "../utils/utils.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <deque>
#include <set>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <iostream>

namespace fs = std::filesystem;
namespace jlib = nlohmann;

static inline std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static inline bool is_valid_mc_name(const std::string& name) {
    if (name.empty() || name.length() > 16) return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            return false;
        }
    }
    return true;
}

jlib::json get_server_players(const std::string& server_id) {
    jlib::json result = jlib::json::array();

    if (!is_server_running(server_id)) {
        return result;
    }

    std::string res_path = get_resource_path();
    fs::path server_dir = fs::path(res_path) / "servers" / server_id;

    // 1. Read ops.json
    std::unordered_map<std::string, int> ops_map;
    fs::path ops_path = server_dir / "ops.json";
    if (fs::exists(ops_path)) {
        try {
            std::ifstream ops_file(ops_path);
            if (ops_file.is_open()) {
                jlib::json ops_data;
                ops_file >> ops_data;
                if (ops_data.is_array()) {
                    for (const auto& item : ops_data) {
                        std::string op_name = item.value("name", "");
                        int op_lvl = item.value("level", 4);
                        if (!op_name.empty()) {
                            ops_map[to_lower(op_name)] = op_lvl;
                        }
                    }
                }
            }
        } catch (...) {}
    }

    // 2. Read usercache.json
    std::unordered_map<std::string, std::string> uuid_map;
    fs::path usercache_path = server_dir / "usercache.json";
    if (fs::exists(usercache_path)) {
        try {
            std::ifstream uc_file(usercache_path);
            if (uc_file.is_open()) {
                jlib::json uc_data;
                uc_file >> uc_data;
                if (uc_data.is_array()) {
                    for (const auto& item : uc_data) {
                        std::string u_name = item.value("name", "");
                        std::string u_uuid = item.value("uuid", "");
                        if (!u_name.empty() && !u_uuid.empty()) {
                            uuid_map[to_lower(u_name)] = u_uuid;
                        }
                    }
                }
            }
        } catch (...) {}
    }

    // 3. Parse latest.log to track active players
    std::set<std::string> online_players;
    fs::path log_path = server_dir / "logs" / "latest.log";

    if (fs::exists(log_path)) {
        std::ifstream log_file(log_path);
        if (log_file.is_open()) {
            std::string line;
            while (std::getline(log_file, line)) {
                if (line.find("Starting minecraft server") != std::string::npos ||
                    line.find("Starting Minecraft server") != std::string::npos ||
                    line.find("Stopping server") != std::string::npos) {
                    online_players.clear();
                    continue;
                }

                // Check for join
                size_t join_pos = line.find(" joined the game");
                if (join_pos != std::string::npos) {
                    size_t colon_pos = line.rfind(": ", join_pos);
                    if (colon_pos != std::string::npos) {
                        std::string name = trim(line.substr(colon_pos + 2, join_pos - (colon_pos + 2)));
                        if (is_valid_mc_name(name)) {
                            online_players.insert(name);
                        }
                    }
                    continue;
                }

                // Check for leave
                size_t leave_pos = line.find(" left the game");
                if (leave_pos != std::string::npos) {
                    size_t colon_pos = line.rfind(": ", leave_pos);
                    if (colon_pos != std::string::npos) {
                        std::string name = trim(line.substr(colon_pos + 2, leave_pos - (colon_pos + 2)));
                        if (!name.empty()) {
                            online_players.erase(name);
                        }
                    }
                    continue;
                }

                // Check for lost connection
                size_t lost_pos = line.find(" lost connection:");
                if (lost_pos != std::string::npos) {
                    size_t colon_pos = line.rfind(": ", lost_pos);
                    if (colon_pos != std::string::npos) {
                        std::string name = trim(line.substr(colon_pos + 2, lost_pos - (colon_pos + 2)));
                        if (!name.empty()) {
                            online_players.erase(name);
                        }
                    }
                    continue;
                }

                // Check for Kicked
                size_t kicked_pos = line.find(" Kicked ");
                if (kicked_pos != std::string::npos) {
                    size_t colon_pos = line.find(":", kicked_pos + 8);
                    std::string name;
                    if (colon_pos != std::string::npos) {
                        name = trim(line.substr(kicked_pos + 8, colon_pos - (kicked_pos + 8)));
                    } else {
                        name = trim(line.substr(kicked_pos + 8));
                    }
                    if (!name.empty()) {
                        online_players.erase(name);
                    }
                    continue;
                }

                // Check for Disconnecting
                size_t disc_pos = line.find(" Disconnecting ");
                if (disc_pos != std::string::npos) {
                    size_t colon_pos = line.find(":", disc_pos + 15);
                    std::string name;
                    if (colon_pos != std::string::npos) {
                        name = trim(line.substr(disc_pos + 15, colon_pos - (disc_pos + 15)));
                    } else {
                        name = trim(line.substr(disc_pos + 15));
                    }
                    if (!name.empty()) {
                        online_players.erase(name);
                    }
                    continue;
                }
            }
        }
    }

    // Build the JSON list
    for (const auto& player_name : online_players) {
        std::string lower_name = to_lower(player_name);
        bool is_op = (ops_map.find(lower_name) != ops_map.end());
        int op_level = is_op ? ops_map[lower_name] : 0;

        std::string uuid = "";
        if (uuid_map.find(lower_name) != uuid_map.end()) {
            uuid = uuid_map[lower_name];
        } else {
            uuid = "beacon-" + player_name;
        }

        jlib::json p;
        p["name"] = player_name;
        p["uuid"] = uuid;
        p["isOp"] = is_op;
        p["opLevel"] = op_level;
        p["online"] = true;
        p["ping"] = 20;
        p["avatarUrl"] = "https://mc-heads.net/avatar/" + player_name + "/64";
        p["headUrl"] = "https://mc-heads.net/head/" + player_name + "/64";
        p["bodyUrl"] = "https://mc-heads.net/body/" + player_name + "/120";

        result.push_back(p);
    }

    return result;
}

bool op_player(const std::string& server_id, const std::string& player_name) {
    if (player_name.empty()) return false;
    return send_server_command(server_id, "op " + player_name);
}

bool deop_player(const std::string& server_id, const std::string& player_name) {
    if (player_name.empty()) return false;
    return send_server_command(server_id, "deop " + player_name);
}

bool kick_player(const std::string& server_id, const std::string& player_name, const std::string& reason) {
    if (player_name.empty()) return false;
    std::string cmd = "kick " + player_name;
    if (!reason.empty()) {
        cmd += " " + reason;
    }
    return send_server_command(server_id, cmd);
}

bool ban_player(const std::string& server_id, const std::string& player_name, const std::string& reason) {
    if (player_name.empty()) return false;
    std::string cmd = "ban " + player_name;
    if (!reason.empty()) {
        cmd += " " + reason;
    }
    return send_server_command(server_id, cmd);
}

jlib::json get_server_logs(const std::string& server_id, int max_lines) {
    jlib::json lines = jlib::json::array();
    std::string res_path = get_resource_path();
    fs::path log_path = fs::path(res_path) / "servers" / server_id / "logs" / "latest.log";

    if (!fs::exists(log_path)) {
        return lines;
    }

    std::ifstream file(log_path);
    if (!file.is_open()) {
        return lines;
    }

    std::deque<std::string> queue;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        queue.push_back(line);
        if (static_cast<int>(queue.size()) > max_lines) {
            queue.pop_front();
        }
    }

    for (const auto& l : queue) {
        lines.push_back(l);
    }

    return lines;
}
