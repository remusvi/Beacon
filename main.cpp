#include <cstdint>
#include <iostream>
#include <string>
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>
#include "webview.h"
#include "index_html.h"
#include "core/servers/servers.h"
#include "core/servers/serverEntity.h"
#include "core/p2p/p2p_manager.h"

Provider determine_provider(const std::string& type_str) {
    std::string lower;
    lower.reserve(type_str.size());
    for (char c : type_str) {
        lower.push_back(std::tolower(static_cast<unsigned char>(c)));
    }

    if (lower == "vanilla") return Provider::Vanilla;
    if (lower == "forge")   return Provider::Forge;
    if (lower == "fabric")  return Provider::Fabric;
    if (lower == "paper")   return Provider::Paper;

    throw std::runtime_error("Unknown provider type: " + type_str);
}

int main() {
    webview::webview main_window(true, nullptr);
    main_window.set_title("Beacon");
    main_window.set_size(1280, 720, WEBVIEW_HINT_NONE);

    const std::string html_content(reinterpret_cast<const char*>(INDEX_HTML_BYTES), INDEX_HTML_SIZE);
    main_window.set_html(html_content);

    try {
        main_window.bind("startServer", [](const std::string& req) -> std::string {
            try {
                auto data = jlib::json::parse(req);
                std::string server_id = data[0].get<std::string>();
                std::cout << "Starting server: " << server_id << std::endl;
                start_server(server_id);
                return "{\"status\":\"ok\"}";
            } catch (const std::exception& e) {
                std::cerr << "startServer error: " << e.what() << std::endl;
                return "{\"error\":\"" + std::string(e.what()) + "\"}";
            }
        });

        main_window.bind("stopServer", [](const std::string& req) -> std::string {
            try {
                auto data = jlib::json::parse(req);
                std::string server_id = data[0].get<std::string>();
                std::cout << "Stopping server: " << server_id << std::endl;
                stop_server(server_id);
                return "{\"status\":\"ok\"}";
            } catch (const std::exception& e) {
                std::cerr << "stopServer error: " << e.what() << std::endl;
                return "{\"error\":\"" + std::string(e.what()) + "\"}";
            }
        });

        main_window.bind("deleteServer", [](const std::string& req) -> std::string {
            try {
                auto data = jlib::json::parse(req);
                std::string server_id;
                if (data.is_array() && !data.empty()) {
                    if (data[0].is_string()) {
                        server_id = data[0].get<std::string>();
                    } else if (data[0].is_number()) {
                        server_id = std::to_string(data[0].get<long long>());
                    }
                } else if (data.is_string()) {
                    server_id = data.get<std::string>();
                } else if (data.is_number()) {
                    server_id = std::to_string(data.get<long long>());
                }
                std::cout << "Deleting server: " << server_id << std::endl;
                delete_server(server_id);
                return "{\"status\":\"ok\"}";
            } catch (const std::exception& e) {
                std::cerr << "deleteServer error: " << e.what() << std::endl;
                return "{\"error\":\"" + std::string(e.what()) + "\"}";
            }
        });

        main_window.bind("getServers", [](const std::string& req) -> std::string {
            try {
                return get_servers().dump();
            } catch (const std::exception& e) {
                std::cerr << "getServers error: " << e.what() << std::endl;
                return "[]";
            }
        });

        main_window.bind("addServer", [](const std::string& req) -> std::string {
            try {
                auto data = jlib::json::parse(req);
                auto server_data = jlib::json::parse(data[0].get<std::string>());
                std::cout << "addServer payload: " << server_data.dump() << std::endl;

                std::string id = server_data.value("id", "");
                std::string name = server_data.value("name", "");
                std::string version = server_data.value("version", "1.20.1");

                std::int32_t memory = 3;
                if (server_data.contains("memory")) {
                    if (server_data["memory"].is_number()) {
                        memory = server_data["memory"].get<std::int32_t>();
                    } else if (server_data["memory"].is_string()) {
                        memory = std::stoi(server_data["memory"].get<std::string>());
                    }
                }

                std::int32_t port = 25565;
                if (server_data.contains("port")) {
                    if (server_data["port"].is_number()) {
                        port = server_data["port"].get<std::int32_t>();
                    } else if (server_data["port"].is_string()) {
                        port = std::stoi(server_data["port"].get<std::string>());
                    }
                }

                bool online = server_data.value("online", false);
                std::string type_str = server_data.value("type", "Vanilla");

                Provider provider = determine_provider(type_str);
                add_server(id, name, provider, version, memory, port, online);

                return "{\"status\":\"ok\"}";
            } catch (const std::exception& e) {
                std::cerr << "addServer error: " << e.what() << std::endl;
                return "{\"error\":\"" + std::string(e.what()) + "\"}";
            }
        });

        auto parse_server_id = [](const jlib::json& val) -> std::string {
            if (val.is_string()) return val.get<std::string>();
            if (val.is_number_integer()) return std::to_string(val.get<int64_t>());
            if (val.is_number()) return std::to_string(static_cast<int64_t>(val.get<double>()));
            return val.dump();
        };

        main_window.bind("getServerPlayers", [&](const std::string& req) -> std::string {
            try {
                auto data = jlib::json::parse(req);
                std::string server_id = parse_server_id(data.at(0));
                return get_server_players(server_id).dump();
            } catch (const std::exception& e) {
                std::cerr << "getServerPlayers error: " << e.what() << std::endl;
                return "[]";
            }
        });

        main_window.bind("opPlayer", [&](const std::string& req) -> std::string {
            try {
                auto data = jlib::json::parse(req);
                std::string server_id = parse_server_id(data.at(0));
                std::string player_name = data.at(1).get<std::string>();
                bool ok = op_player(server_id, player_name);
                return ok ? "{\"status\":\"ok\"}" : "{\"error\":\"Server not running or op command failed\"}";
            } catch (const std::exception& e) {
                std::cerr << "opPlayer error: " << e.what() << std::endl;
                return "{\"error\":\"" + std::string(e.what()) + "\"}";
            }
        });

        main_window.bind("deopPlayer", [&](const std::string& req) -> std::string {
            try {
                auto data = jlib::json::parse(req);
                std::string server_id = parse_server_id(data.at(0));
                std::string player_name = data.at(1).get<std::string>();
                bool ok = deop_player(server_id, player_name);
                return ok ? "{\"status\":\"ok\"}" : "{\"error\":\"Server not running or deop command failed\"}";
            } catch (const std::exception& e) {
                std::cerr << "deopPlayer error: " << e.what() << std::endl;
                return "{\"error\":\"" + std::string(e.what()) + "\"}";
            }
        });

        main_window.bind("kickPlayer", [&](const std::string& req) -> std::string {
            try {
                auto data = jlib::json::parse(req);
                std::string server_id = parse_server_id(data.at(0));
                std::string player_name = data.at(1).get<std::string>();
                std::string reason = (data.size() > 2 && data.at(2).is_string()) ? data.at(2).get<std::string>() : "Kicked by operator";
                bool ok = kick_player(server_id, player_name, reason);
                return ok ? "{\"status\":\"ok\"}" : "{\"error\":\"Server not running or kick command failed\"}";
            } catch (const std::exception& e) {
                std::cerr << "kickPlayer error: " << e.what() << std::endl;
                return "{\"error\":\"" + std::string(e.what()) + "\"}";
            }
        });

        main_window.bind("banPlayer", [&](const std::string& req) -> std::string {
            try {
                auto data = jlib::json::parse(req);
                std::string server_id = parse_server_id(data.at(0));
                std::string player_name = data.at(1).get<std::string>();
                std::string reason = (data.size() > 2 && data.at(2).is_string()) ? data.at(2).get<std::string>() : "Banned by operator";
                bool ok = ban_player(server_id, player_name, reason);
                return ok ? "{\"status\":\"ok\"}" : "{\"error\":\"Server not running or ban command failed\"}";
            } catch (const std::exception& e) {
                std::cerr << "banPlayer error: " << e.what() << std::endl;
                return "{\"error\":\"" + std::string(e.what()) + "\"}";
            }
        });

        main_window.bind("sendServerCommand", [&](const std::string& req) -> std::string {
            try {
                auto data = jlib::json::parse(req);
                std::string server_id = parse_server_id(data.at(0));
                std::string command = data.at(1).get<std::string>();
                bool ok = send_server_command(server_id, command);
                return ok ? "{\"status\":\"ok\"}" : "{\"error\":\"Server not running or command failed\"}";
            } catch (const std::exception& e) {
                std::cerr << "sendServerCommand error: " << e.what() << std::endl;
                return "{\"error\":\"" + std::string(e.what()) + "\"}";
            }
        });

        main_window.bind("getServerLogs", [&](const std::string& req) -> std::string {
            try {
                auto data = jlib::json::parse(req);
                std::string server_id = parse_server_id(data.at(0));
                int max_lines = 150;
                if (data.size() > 1 && data.at(1).is_number()) {
                    max_lines = data.at(1).get<int>();
                }
                return get_server_logs(server_id, max_lines).dump();
            } catch (const std::exception& e) {
                std::cerr << "getServerLogs error: " << e.what() << std::endl;
                return "[]";
            }
        });

        // --- P2P Networking Bindings ---
        main_window.bind("startP2PHost", [](const std::string& req) -> std::string {
            try {
                auto data = jlib::json::parse(req);
                std::string server_id = "default";
                uint16_t port = 25565;
                if (!data.empty() && data[0].is_string()) {
                    server_id = data[0].get<std::string>();
                }
                if (data.size() > 1 && data[1].is_number()) {
                    port = data[1].get<uint16_t>();
                }
                return beacon::p2p::P2PManager::instance().start_host(server_id, port).dump();
            } catch (const std::exception& e) {
                std::cerr << "startP2PHost error: " << e.what() << std::endl;
                return "{\"error\":\"" + std::string(e.what()) + "\"}";
            }
        });

        main_window.bind("stopP2PHost", [](const std::string& /*req*/) -> std::string {
            try {
                return beacon::p2p::P2PManager::instance().stop_host().dump();
            } catch (const std::exception& e) {
                std::cerr << "stopP2PHost error: " << e.what() << std::endl;
                return "{\"error\":\"" + std::string(e.what()) + "\"}";
            }
        });

        main_window.bind("joinP2PSession", [](const std::string& req) -> std::string {
            try {
                auto data = jlib::json::parse(req);
                std::string room_code;
                uint16_t local_port = 25565;
                if (!data.empty() && data[0].is_string()) {
                    room_code = data[0].get<std::string>();
                }
                if (data.size() > 1 && data[1].is_number()) {
                    local_port = data[1].get<uint16_t>();
                }
                return beacon::p2p::P2PManager::instance().join_session(room_code, local_port).dump();
            } catch (const std::exception& e) {
                std::cerr << "joinP2PSession error: " << e.what() << std::endl;
                return "{\"error\":\"" + std::string(e.what()) + "\"}";
            }
        });

        main_window.bind("leaveP2PSession", [](const std::string& /*req*/) -> std::string {
            try {
                return beacon::p2p::P2PManager::instance().leave_session().dump();
            } catch (const std::exception& e) {
                std::cerr << "leaveP2PSession error: " << e.what() << std::endl;
                return "{\"error\":\"" + std::string(e.what()) + "\"}";
            }
        });

        main_window.bind("getP2PStatus", [](const std::string& /*req*/) -> std::string {
            try {
                return beacon::p2p::P2PManager::instance().get_status().dump();
            } catch (const std::exception& e) {
                std::cerr << "getP2PStatus error: " << e.what() << std::endl;
                return "{\"error\":\"" + std::string(e.what()) + "\"}";
            }
        });
    } catch (std::exception& e) {
        std::cerr << "Binding setup error: " << e.what() << std::endl;
    }

    main_window.run();
    return 0;
}
