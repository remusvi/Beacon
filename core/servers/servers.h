//
// Created by Adafa Ralph on 6/21/26.
//

#ifndef BEACON2_0_SERVERS_H
#define BEACON2_0_SERVERS_H

#include "nlohmann/json.hpp"
#include "./serverEntity.h"
#include <string>

namespace jlib = nlohmann;

void start_server(const std::string& server_id);
void stop_server(const std::string& server_id);
void delete_server(const std::string& server_id);
bool is_server_running(const std::string& server_id);
bool send_server_command(const std::string& server_id, const std::string& command);

void add_server(const std::string& id,
                const std::string& name,
                Provider provider,
                const std::string& version,
                std::int32_t ram,
                std::int32_t port,
                bool online_mode);

jlib::json get_servers();

// Player and Log Management
jlib::json get_server_players(const std::string& server_id);
bool op_player(const std::string& server_id, const std::string& player_name);
bool deop_player(const std::string& server_id, const std::string& player_name);
bool kick_player(const std::string& server_id, const std::string& player_name, const std::string& reason = "Kicked by operator");
bool ban_player(const std::string& server_id, const std::string& player_name, const std::string& reason = "Banned by operator");
jlib::json get_server_logs(const std::string& server_id, int max_lines = 150);

#endif //BEACON2_0_SERVERS_H
