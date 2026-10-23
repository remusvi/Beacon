#include "servers.h"
#include "../utils/utils.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <deque>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <mutex>
#include <chrono>

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

static inline void remove_online_player(std::set<std::string>& online_players, const std::string& target) {
    std::string clean = trim(target);
    while (!clean.empty() && (clean.back() == ']' || clean.back() == ':' || clean.back() == ')' || clean.back() == '.' || clean.back() == ',')) {
        clean.pop_back();
    }
    while (!clean.empty() && (clean.front() == '[' || clean.front() == '(')) {
        clean.erase(clean.begin());
    }
    clean = trim(clean);
    if (clean.empty()) return;

    std::string lower_clean = to_lower(clean);
    for (auto it = online_players.begin(); it != online_players.end(); ) {
        if (to_lower(*it) == lower_clean) {
            it = online_players.erase(it);
        } else {
            ++it;
        }
    }
}

struct EvictionRecord {
    std::string player_name_lower;
    std::chrono::steady_clock::time_point timestamp;
    bool is_ban;
};

static std::unordered_map<std::string, std::vector<EvictionRecord>> s_evicted_players;
static std::mutex s_evicted_mutex;

static void record_eviction(const std::string& server_id, const std::string& player_name, bool is_ban) {
    std::string lower_name = to_lower(trim(player_name));
    if (lower_name.empty()) return;
    std::lock_guard<std::mutex> lock(s_evicted_mutex);
    auto& list = s_evicted_players[server_id];
    for (auto& ev : list) {
        if (ev.player_name_lower == lower_name) {
            ev.timestamp = std::chrono::steady_clock::now();
            ev.is_ban = ev.is_ban || is_ban;
            return;
        }
    }
    list.push_back({lower_name, std::chrono::steady_clock::now(), is_ban});
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

    // 3. Read banned-players.json
    std::unordered_set<std::string> banned_names;
    fs::path banned_path = server_dir / "banned-players.json";
    if (fs::exists(banned_path)) {
        try {
            std::ifstream b_file(banned_path);
            if (b_file.is_open()) {
                jlib::json b_data;
                b_file >> b_data;
                if (b_data.is_array()) {
                    for (const auto& item : b_data) {
                        std::string b_name = item.value("name", "");
                        if (!b_name.empty()) {
                            banned_names.insert(to_lower(trim(b_name)));
                        }
                    }
                }
            }
        } catch (...) {}
    }

    // 4. Parse latest.log to track active players
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
                            if (banned_names.find(to_lower(name)) == banned_names.end()) {
                                online_players.insert(name);
                            }
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
                        remove_online_player(online_players, name);
                    }
                    continue;
                }

                // Check for lost connection
                size_t lost_pos = line.find(" lost connection:");
                if (lost_pos != std::string::npos) {
                    size_t colon_pos = line.rfind(": ", lost_pos);
                    if (colon_pos != std::string::npos) {
                        std::string name = trim(line.substr(colon_pos + 2, lost_pos - (colon_pos + 2)));
                        remove_online_player(online_players, name);
                    }
                    continue;
                }

                std::string lower_line = to_lower(line);

                // Check for " was kicked "
                size_t was_kicked_pos = lower_line.find(" was kicked ");
                if (was_kicked_pos != std::string::npos) {
                    size_t colon_pos = line.rfind(": ", was_kicked_pos);
                    if (colon_pos != std::string::npos) {
                        std::string name = trim(line.substr(colon_pos + 2, was_kicked_pos - (colon_pos + 2)));
                        remove_online_player(online_players, name);
                    }
                    continue;
                }

                // Check for "kicked "
                size_t kicked_pos = lower_line.find("kicked ");
                if (kicked_pos != std::string::npos) {
                    size_t name_start = kicked_pos + 7;
                    if (lower_line.substr(name_start, 7) == "player ") {
                        name_start += 7;
                    }
                    size_t delim_pos = line.find_first_of(": ]", name_start);
                    std::string name = (delim_pos != std::string::npos)
                        ? line.substr(name_start, delim_pos - name_start)
                        : line.substr(name_start);
                    remove_online_player(online_players, name);
                    continue;
                }

                // Check for " was banned "
                size_t was_banned_pos = lower_line.find(" was banned ");
                if (was_banned_pos != std::string::npos) {
                    size_t colon_pos = line.rfind(": ", was_banned_pos);
                    if (colon_pos != std::string::npos) {
                        std::string name = trim(line.substr(colon_pos + 2, was_banned_pos - (colon_pos + 2)));
                        remove_online_player(online_players, name);
                    }
                    continue;
                }

                // Check for "banned "
                size_t banned_pos = lower_line.find("banned ");
                if (banned_pos != std::string::npos) {
                    size_t name_start = banned_pos + 7;
                    if (lower_line.substr(name_start, 7) == "player ") {
                        name_start += 7;
                    }
                    size_t delim_pos = line.find_first_of(": ]", name_start);
                    std::string name = (delim_pos != std::string::npos)
                        ? line.substr(name_start, delim_pos - name_start)
                        : line.substr(name_start);
                    remove_online_player(online_players, name);
                    continue;
                }

                // Check for "disconnecting "
                if (lower_line.find("disconnecting ") != std::string::npos) {
                    size_t np = lower_line.find("name=");
                    if (np != std::string::npos) {
                        size_t end_p = line.find_first_of(",])", np + 5);
                        std::string name = (end_p != std::string::npos)
                            ? line.substr(np + 5, end_p - (np + 5))
                            : line.substr(np + 5);
                        remove_online_player(online_players, name);
                    } else {
                        size_t disc_idx = lower_line.find("disconnecting ");
                        size_t start_n = disc_idx + 14;
                        size_t colon_p = line.find(":", start_n);
                        std::string name = (colon_p != std::string::npos)
                            ? line.substr(start_n, colon_p - start_n)
                            : line.substr(start_n);
                        remove_online_player(online_players, name);
                    }
                    continue;
                }
            }
        }
    }

    // Exclude any player found in banned-players.json
    for (const auto& bname : banned_names) {
        remove_online_player(online_players, bname);
    }

    // Exclude any player currently evicted (kicked within 20s or banned)
    {
        std::lock_guard<std::mutex> lock(s_evicted_mutex);
        auto it = s_evicted_players.find(server_id);
        if (it != s_evicted_players.end()) {
            auto now = std::chrono::steady_clock::now();
            for (const auto& ev : it->second) {
                if (ev.is_ban || std::chrono::duration_cast<std::chrono::seconds>(now - ev.timestamp).count() < 20) {
                    remove_online_player(online_players, ev.player_name_lower);
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
    std::string clean_name = trim(player_name);
    if (clean_name.empty()) return false;
    return send_server_command(server_id, "op " + clean_name);
}

bool deop_player(const std::string& server_id, const std::string& player_name) {
    std::string clean_name = trim(player_name);
    if (clean_name.empty()) return false;
    return send_server_command(server_id, "deop " + clean_name);
}

bool kick_player(const std::string& server_id, const std::string& player_name, const std::string& reason) {
    std::string clean_name = trim(player_name);
    if (clean_name.empty()) return false;

    std::string clean_reason = reason;
    for (char& c : clean_reason) {
        if (c == '\r' || c == '\n') c = ' ';
    }
    clean_reason = trim(clean_reason);

    std::string cmd = "kick " + clean_name;
    if (!clean_reason.empty()) {
        cmd += " " + clean_reason;
    }

    bool ok = send_server_command(server_id, cmd);
    if (ok) {
        record_eviction(server_id, clean_name, false);
    }
    return ok;
}

bool ban_player(const std::string& server_id, const std::string& player_name, const std::string& reason) {
    std::string clean_name = trim(player_name);
    if (clean_name.empty()) return false;

    std::string clean_reason = reason;
    for (char& c : clean_reason) {
        if (c == '\r' || c == '\n') c = ' ';
    }
    clean_reason = trim(clean_reason);

    std::string cmd = "ban " + clean_name;
    if (!clean_reason.empty()) {
        cmd += " " + clean_reason;
    }

    bool ok = send_server_command(server_id, cmd);
    if (ok) {
        record_eviction(server_id, clean_name, true);
    }
    return ok;
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