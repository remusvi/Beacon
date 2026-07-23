//
// Created by Adafa Ralph on 6/21/26.
//
#include "nlohmann/json.hpp"
#include "./serverEntity.h"

namespace jlib = nlohmann;
#ifndef BEACON2_0_SERVERS_H
#define BEACON2_0_SERVERS_H


void add_server(std::string& id,
std::string& name,
Provider provider,
std::string& version,
std::int32_t ram,
std::int32_t port,
bool online_mode);

jlib::json get_servers();

void start_server(std::string& server_id);

#endif //BEACON2_0_SERVERS_H
