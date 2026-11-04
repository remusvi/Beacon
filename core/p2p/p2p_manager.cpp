#include "p2p_manager.h"
#include "../servers/servers.h"
#include <iostream>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <cpr/cpr.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#endif

namespace beacon::p2p {

static const std::string STUN_HOST = "stun.l.google.com";
static const uint16_t STUN_PORT = 19302;

P2PManager& P2PManager::instance() {
    static P2PManager mgr;
    return mgr;
}

P2PManager::P2PManager() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

P2PManager::~P2PManager() {
    stop_host();
    leave_session();
#ifdef _WIN32
    WSACleanup();
#endif
}

std::string P2PManager::generate_room_code() {
    const char chars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(chars) - 2);

    std::string code = "BCN-";
    for (int i = 0; i < 4; ++i) {
        code += chars[dis(gen)];
    }
    return code;
}

std::string P2PManager::discover_local_ip() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return "127.0.0.1";
    struct sockaddr_in loopback{};
    loopback.sin_family = AF_INET;
    loopback.sin_addr.s_addr = inet_addr("8.8.8.8");
    loopback.sin_port = htons(53);
    if (connect(sock, (struct sockaddr*)&loopback, sizeof(loopback)) == 0) {
        struct sockaddr_in name{};
#ifdef _WIN32
        int namelen = sizeof(name);
#else
        socklen_t namelen = sizeof(name);
#endif
        if (getsockname(sock, (struct sockaddr*)&name, &namelen) == 0) {
            std::string ip = inet_ntoa(name.sin_addr);
#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
            return ip;
        }
    }
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return "127.0.0.1";
}

std::string P2PManager::discover_public_endpoint(uint16_t local_port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return "";

    struct sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(local_port);

#ifdef _WIN32
    DWORD timeout = 2000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv{2, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    struct hostent* he = gethostbyname(STUN_HOST.c_str());
    if (!he) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return "";
    }

    struct sockaddr_in stun_addr{};
    stun_addr.sin_family = AF_INET;
    stun_addr.sin_port = htons(STUN_PORT);
    memcpy(&stun_addr.sin_addr, he->h_addr_list[0], he->h_length);

    uint8_t stun_req[20] = {0};
    stun_req[0] = 0x00; stun_req[1] = 0x01;
    stun_req[2] = 0x00; stun_req[3] = 0x00;
    stun_req[4] = 0x21; stun_req[5] = 0x12; stun_req[6] = 0xA4; stun_req[7] = 0x42;
    for (int i = 8; i < 20; ++i) stun_req[i] = static_cast<uint8_t>(rand() % 256);

    sendto(sock, (const char*)stun_req, sizeof(stun_req), 0, (struct sockaddr*)&stun_addr, sizeof(stun_addr));

    uint8_t buf[512];
    struct sockaddr_in from{};
#ifdef _WIN32
    int from_len = sizeof(from);
#else
    socklen_t from_len = sizeof(from);
#endif
    auto len = recvfrom(sock, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&from, &from_len);

    std::string mapped_endpoint;
    if (len >= 20 && buf[0] == 0x01 && buf[1] == 0x01) {
        size_t idx = 20;
        while (idx + 4 <= static_cast<size_t>(len)) {
            uint16_t attr_type = (buf[idx] << 8) | buf[idx + 1];
            uint16_t attr_len = (buf[idx + 2] << 8) | buf[idx + 3];
            idx += 4;

            if (attr_type == 0x0020 && attr_len >= 8 && idx + attr_len <= static_cast<size_t>(len)) {
                uint8_t family = buf[idx + 1];
                if (family == 0x01) {
                    uint16_t xor_port = (buf[idx + 2] << 8) | buf[idx + 3];
                    uint16_t port = xor_port ^ 0x2112;

                    uint32_t xor_ip = (buf[idx + 4] << 24) | (buf[idx + 5] << 16) | (buf[idx + 6] << 8) | buf[idx + 7];
                    uint32_t ip = xor_ip ^ 0x2112A442;

                    struct in_addr ip_addr;
                    ip_addr.s_addr = htonl(ip);
                    mapped_endpoint = std::string(inet_ntoa(ip_addr)) + ":" + std::to_string(port);
                    break;
                }
            }
            idx += (attr_len + 3) & ~3;
        }
    }

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return mapped_endpoint;
}

bool P2PManager::try_upnp_port_forward(uint16_t port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;

    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast, sizeof(broadcast));

#ifdef _WIN32
    DWORD tv = 1500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
    struct timeval tv{1, 500000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(1900);
    dest.sin_addr.s_addr = inet_addr("239.255.255.250");

    std::string msg = 
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n";

    sendto(sock, msg.c_str(), msg.size(), 0, (struct sockaddr*)&dest, sizeof(dest));

    char buf[2048] = {0};
    struct sockaddr_in from{};
#ifdef _WIN32
    int from_len = sizeof(from);
#else
    socklen_t from_len = sizeof(from);
#endif
    auto n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&from, &from_len);

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif

    if (n <= 0) return false;

    std::string resp(buf, n);
    auto loc_pos = resp.find("LOCATION:");
    if (loc_pos == std::string::npos) {
        loc_pos = resp.find("Location:");
    }
    if (loc_pos == std::string::npos) return false;

    auto line_end = resp.find("\r\n", loc_pos);
    if (line_end == std::string::npos) line_end = resp.find("\n", loc_pos);
    std::string location_url = resp.substr(loc_pos + 9, line_end - (loc_pos + 9));
    location_url.erase(0, location_url.find_first_not_of(" \t"));
    location_url.erase(location_url.find_last_not_of(" \t\r\n") + 1);

    if (location_url.empty()) return false;

    // Fetch device description XML
    auto desc_res = cpr::Get(cpr::Url{location_url}, cpr::Timeout{3000});
    if (desc_res.status_code != 200 || desc_res.text.empty()) return false;

    std::string service_type = "urn:schemas-upnp-org:service:WANIPConnection:1";
    auto s_pos = desc_res.text.find(service_type);
    if (s_pos == std::string::npos) {
        service_type = "urn:schemas-upnp-org:service:WANPPPConnection:1";
        s_pos = desc_res.text.find(service_type);
    }
    if (s_pos == std::string::npos) return false;

    auto ctrl_pos = desc_res.text.find("<controlURL>", s_pos);
    if (ctrl_pos == std::string::npos) return false;
    auto ctrl_end = desc_res.text.find("</controlURL>", ctrl_pos);
    if (ctrl_end == std::string::npos) return false;

    std::string rel_ctrl = desc_res.text.substr(ctrl_pos + 12, ctrl_end - (ctrl_pos + 12));
    rel_ctrl.erase(0, rel_ctrl.find_first_not_of(" \t\r\n"));
    rel_ctrl.erase(rel_ctrl.find_last_not_of(" \t\r\n") + 1);

    std::string control_url;
    if (rel_ctrl.rfind("http", 0) == 0) {
        control_url = rel_ctrl;
    } else {
        auto slash_idx = location_url.find('/', 7);
        std::string base = (slash_idx != std::string::npos) ? location_url.substr(0, slash_idx) : location_url;
        control_url = base + (rel_ctrl[0] == '/' ? "" : "/") + rel_ctrl;
    }

    upnp_control_url_ = control_url;
    upnp_service_type_ = service_type;

    // Send SOAP AddPortMapping
    std::string local_ip = discover_local_ip();
    std::string soap_body =
        "<?xml version=\"1.0\"?>\r\n"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
        "<s:Body>\r\n"
        "<u:AddPortMapping xmlns:u=\"" + service_type + "\">\r\n"
        "<NewRemoteHost></NewRemoteHost>\r\n"
        "<NewExternalPort>" + std::to_string(port) + "</NewExternalPort>\r\n"
        "<NewProtocol>TCP</NewProtocol>\r\n"
        "<NewInternalPort>" + std::to_string(port) + "</NewInternalPort>\r\n"
        "<NewInternalClient>" + local_ip + "</NewInternalClient>\r\n"
        "<NewEnabled>1</NewEnabled>\r\n"
        "<NewPortMappingDescription>Beacon P2P Multiplayer</NewPortMappingDescription>\r\n"
        "<NewLeaseDuration>0</NewLeaseDuration>\r\n"
        "</u:AddPortMapping>\r\n"
        "</s:Body>\r\n"
        "</s:Envelope>\r\n";

    auto soap_res = cpr::Post(
        cpr::Url{control_url},
        cpr::Header{
            {"Content-Type", "text/xml; charset=\"utf-8\""},
            {"SOAPAction", "\"" + service_type + "#AddPortMapping\""}
        },
        cpr::Body{soap_body},
        cpr::Timeout{3000}
    );

    bool ok = (soap_res.status_code == 200);
    std::cout << "[P2P] UPnP AddPortMapping status: " << soap_res.status_code << (ok ? " (Success)" : " (Ignored)") << std::endl;
    return ok;
}

bool P2PManager::delete_upnp_port_forward(uint16_t port) {
    if (upnp_control_url_.empty() || upnp_service_type_.empty()) return false;

    std::string soap_body =
        "<?xml version=\"1.0\"?>\r\n"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
        "<s:Body>\r\n"
        "<u:DeletePortMapping xmlns:u=\"" + upnp_service_type_ + "\">\r\n"
        "<NewRemoteHost></NewRemoteHost>\r\n"
        "<NewExternalPort>" + std::to_string(port) + "</NewExternalPort>\r\n"
        "<NewProtocol>TCP</NewProtocol>\r\n"
        "</u:DeletePortMapping>\r\n"
        "</s:Body>\r\n"
        "</s:Envelope>\r\n";

    auto soap_res = cpr::Post(
        cpr::Url{upnp_control_url_},
        cpr::Header{
            {"Content-Type", "text/xml; charset=\"utf-8\""},
            {"SOAPAction", "\"" + upnp_service_type_ + "#DeletePortMapping\""}
        },
        cpr::Body{soap_body},
        cpr::Timeout{2000}
    );

    return (soap_res.status_code == 200);
}

void P2PManager::start_host_signaling() {
    stop_host_signaling();
    host_signaling_running_ = true;

    host_signaling_thread_ = std::make_unique<std::thread>([this]() {
        int loop_count = 0;
        while (host_signaling_running_) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!host_signaling_running_) break;

            std::string room_code;
            std::string server_id;
            uint16_t port = 25565;
            std::string pub_ep;
            std::string loc_ep;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!is_running_ || status_.role != SessionRole::Host) break;
                room_code = status_.room_code;
                server_id = status_.server_id;
                port = status_.target_port;
                pub_ep = status_.public_endpoint;
                loc_ep = status_.local_endpoint;
            }

            loop_count++;

            // 1. Keep room announcement alive every 20 seconds
            if (loop_count % 10 == 0 && !room_code.empty()) {
                std::string pub_ip = pub_ep;
                auto colon = pub_ip.find(':');
                if (colon != std::string::npos) pub_ip = pub_ip.substr(0, colon);

                std::string loc_ip = loc_ep;
                auto colon_loc = loc_ip.find(':');
                if (colon_loc != std::string::npos) loc_ip = loc_ip.substr(0, colon_loc);

                nlohmann::json room_announcement = {
                    {"room_code", room_code},
                    {"server_id", server_id},
                    {"public_ip", pub_ip},
                    {"local_ip", loc_ip},
                    {"port", port}
                };

                cpr::Post(
                    cpr::Url{"https://ntfy.sh/beacon_p2p_room_" + room_code},
                    cpr::Body{room_announcement.dump()},
                    cpr::Timeout{2000}
                );
            }

            // 2. Poll peer signals
            if (!room_code.empty()) {
                auto res = cpr::Get(
                    cpr::Url{"https://ntfy.sh/beacon_p2p_peers_" + room_code + "/json?poll=1"},
                    cpr::Timeout{2000}
                );

                if (res.status_code == 200 && !res.text.empty()) {
                    std::stringstream ss(res.text);
                    std::string line;
                    while (std::getline(ss, line)) {
                        if (line.empty()) continue;
                        try {
                            auto j = nlohmann::json::parse(line);
                            if (j.contains("message")) {
                                auto msg = nlohmann::json::parse(j["message"].get<std::string>());
                                std::string ev = msg.value("event", "");
                                std::string pid = msg.value("peer_id", "");
                                std::string ep = msg.value("endpoint", "");
                                std::string cat = msg.value("connected_at", "");

                                std::lock_guard<std::mutex> lock(mutex_);
                                if (ev == "join") {
                                    bool exists = false;
                                    for (auto& p : status_.connected_peers) {
                                        if (p.id == pid) { exists = true; break; }
                                    }
                                    if (!exists && !pid.empty()) {
                                        status_.connected_peers.push_back({
                                            pid,
                                            ep.empty() ? "Connected" : ep,
                                            cat.empty() ? "Just now" : cat,
                                            0, 0
                                        });
                                        std::cout << "[P2P] Registered peer: " << pid << " (" << ep << ")" << std::endl;
                                    }
                                } else if (ev == "leave") {
                                    status_.connected_peers.erase(
                                        std::remove_if(status_.connected_peers.begin(), status_.connected_peers.end(),
                                            [&pid](const PeerInfo& p) { return p.id == pid; }),
                                        status_.connected_peers.end()
                                    );
                                    std::cout << "[P2P] Peer left: " << pid << std::endl;
                                }
                            }
                        } catch (...) {}
                    }
                }
            }

            // 3. Sync active in-game Minecraft players from server logs
            if (!server_id.empty()) {
                try {
                    auto mc_players = get_server_players(server_id);
                    if (mc_players.is_array()) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        for (const auto& player_obj : mc_players) {
                            std::string p_name = player_obj.value("name", "");
                            if (p_name.empty()) continue;
                            bool found = false;
                            for (const auto& existing : status_.connected_peers) {
                                if (existing.id == p_name) { found = true; break; }
                            }
                            if (!found) {
                                auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                                char time_buf[32];
                                std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", std::localtime(&now));
                                status_.connected_peers.push_back({
                                    p_name,
                                    "In-Game Player",
                                    std::string(time_buf),
                                    0, 0
                                });
                            }
                        }
                    }
                } catch (...) {}
            }
        }
    });
}

void P2PManager::stop_host_signaling() {
    host_signaling_running_ = false;
    if (host_signaling_thread_ && host_signaling_thread_->joinable()) {
        host_signaling_thread_->join();
        host_signaling_thread_.reset();
    }
}

void P2PManager::start_client_heartbeat() {
    stop_client_heartbeat();
    client_heartbeat_running_ = true;

    client_heartbeat_thread_ = std::make_unique<std::thread>([this]() {
        while (client_heartbeat_running_) {
            std::this_thread::sleep_for(std::chrono::seconds(15));
            if (!client_heartbeat_running_) break;

            std::string code;
            std::string pid;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!is_running_ || status_.role != SessionRole::Client) break;
                code = status_.room_code;
                pid = my_client_peer_id_;
            }

            if (!code.empty() && !pid.empty()) {
                nlohmann::json ping = {
                    {"event", "ping"},
                    {"peer_id", pid}
                };
                cpr::Post(
                    cpr::Url{"https://ntfy.sh/beacon_p2p_peers_" + code},
                    cpr::Body{ping.dump()},
                    cpr::Timeout{2000}
                );
            }
        }
    });
}

void P2PManager::stop_client_heartbeat() {
    client_heartbeat_running_ = false;
    if (client_heartbeat_thread_ && client_heartbeat_thread_->joinable()) {
        client_heartbeat_thread_->join();
        client_heartbeat_thread_.reset();
    }
}

void P2PManager::start_client_proxy(uint16_t listen_port, const std::string& target_host_ip, uint16_t target_host_port) {
    stop_client_proxy();

    proxy_listen_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_listen_socket_ < 0) return;

    int opt = 1;
    setsockopt(proxy_listen_socket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    uint16_t bound_port = listen_port;
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(bound_port);

    // Try preferred port (default 25565); if already bound, fall back to next free port
    while (bind(proxy_listen_socket_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        bound_port++;
        if (bound_port > listen_port + 10) {
            std::cerr << "[P2P] Failed to bind local client proxy listener." << std::endl;
#ifdef _WIN32
            closesocket(proxy_listen_socket_);
#else
            close(proxy_listen_socket_);
#endif
            proxy_listen_socket_ = -1;
            return;
        }
        addr.sin_port = htons(bound_port);
    }

    if (listen(proxy_listen_socket_, 5) < 0) {
#ifdef _WIN32
        closesocket(proxy_listen_socket_);
#else
        close(proxy_listen_socket_);
#endif
        proxy_listen_socket_ = -1;
        return;
    }

    status_.local_proxy_port = bound_port;
    proxy_running_ = true;

    client_listener_thread_ = std::make_unique<std::thread>([this, bound_port, target_host_ip, target_host_port]() {
        std::cout << "[P2P] Local TCP proxy listening on 127.0.0.1:" << bound_port << " -> Forwarding to " << target_host_ip << ":" << target_host_port << std::endl;

        while (proxy_running_) {
            struct sockaddr_in client_addr{};
#ifdef _WIN32
            int client_len = sizeof(client_addr);
#else
            socklen_t client_len = sizeof(client_addr);
#endif
            int client_sock = accept(proxy_listen_socket_, (struct sockaddr*)&client_addr, &client_len);
            if (client_sock < 0) {
                if (!proxy_running_) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            std::cout << "[P2P] Minecraft client connected to local proxy! Bridging to " << target_host_ip << ":" << target_host_port << std::endl;

            // Connect to remote host and pipe streams bidirectionally
            std::thread([client_sock, target_host_ip, target_host_port]() {
                int remote_sock = socket(AF_INET, SOCK_STREAM, 0);
                if (remote_sock < 0) {
#ifdef _WIN32
                    closesocket(client_sock);
#else
                    close(client_sock);
#endif
                    return;
                }

                struct sockaddr_in raddr{};
                raddr.sin_family = AF_INET;
                raddr.sin_port = htons(target_host_port);
                raddr.sin_addr.s_addr = inet_addr(target_host_ip.c_str());

                if (connect(remote_sock, (struct sockaddr*)&raddr, sizeof(raddr)) < 0) {
                    std::cerr << "[P2P] Failed to connect to remote host " << target_host_ip << ":" << target_host_port << std::endl;
#ifdef _WIN32
                    closesocket(remote_sock);
                    closesocket(client_sock);
#else
                    close(remote_sock);
                    close(client_sock);
#endif
                    return;
                }

                std::cout << "[P2P] Active bidirectional tunnel established between Minecraft client and host!" << std::endl;

                auto forward_stream = [](int src, int dst, std::atomic<bool>& active) {
                    char buf[8192];
                    while (active) {
                        int n = recv(src, buf, sizeof(buf), 0);
                        if (n <= 0) break;
                        int sent = 0;
                        while (sent < n) {
                            int w = send(dst, buf + sent, n - sent, 0);
                            if (w <= 0) break;
                            sent += w;
                        }
                        if (sent < n) break;
                    }
                    active = false;
#ifdef _WIN32
                    shutdown(dst, SD_BOTH);
#else
                    shutdown(dst, SHUT_RDWR);
#endif
                };

                std::atomic<bool> tunnel_active{true};
                std::thread t1(forward_stream, client_sock, remote_sock, std::ref(tunnel_active));
                std::thread t2(forward_stream, remote_sock, client_sock, std::ref(tunnel_active));

                if (t1.joinable()) t1.join();
                if (t2.joinable()) t2.join();

#ifdef _WIN32
                closesocket(client_sock);
                closesocket(remote_sock);
#else
                close(client_sock);
                close(remote_sock);
#endif
                std::cout << "[P2P] Tunnel connection closed." << std::endl;
            }).detach();
        }
    });
}

void P2PManager::stop_client_proxy() {
    proxy_running_ = false;
    if (proxy_listen_socket_ >= 0) {
#ifdef _WIN32
        closesocket(proxy_listen_socket_);
#else
        close(proxy_listen_socket_);
#endif
        proxy_listen_socket_ = -1;
    }
    if (client_listener_thread_ && client_listener_thread_->joinable()) {
        client_listener_thread_->join();
        client_listener_thread_.reset();
    }
}

nlohmann::json P2PManager::start_host(const std::string& server_id, uint16_t target_port) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (is_running_) {
        if (status_.role == SessionRole::Host && status_.server_id == server_id) {
            return {
                {"status", "ok"},
                {"room_code", status_.room_code},
                {"public_endpoint", status_.public_endpoint},
                {"local_endpoint", status_.local_endpoint},
                {"target_port", status_.target_port}
            };
        }
        return {{"error", "A P2P session is already running. Please stop it first."}};
    }

    std::string code = generate_room_code();
    status_.role = SessionRole::Host;
    status_.state = ConnectionState::Connecting;
    status_.room_code = code;
    status_.server_id = server_id;
    status_.target_port = target_port;
    status_.connected_peers.clear();
    status_.error_message = "";

    std::string local_ip = discover_local_ip();
    status_.local_endpoint = local_ip + ":" + std::to_string(target_port);

    status_.public_endpoint = discover_public_endpoint(target_port);
    if (status_.public_endpoint.empty()) {
        status_.public_endpoint = status_.local_endpoint;
    }

    bool upnp_ok = try_upnp_port_forward(target_port);

    // Announce room to signaling endpoint
    std::string pub_ip = status_.public_endpoint;
    auto colon_pos = pub_ip.find(':');
    if (colon_pos != std::string::npos) {
        pub_ip = pub_ip.substr(0, colon_pos);
    }

    nlohmann::json room_announcement = {
        {"room_code", code},
        {"server_id", server_id},
        {"public_ip", pub_ip},
        {"local_ip", local_ip},
        {"port", target_port},
        {"upnp", upnp_ok}
    };

    cpr::Post(
        cpr::Url{"https://ntfy.sh/beacon_p2p_room_" + code},
        cpr::Body{room_announcement.dump()},
        cpr::Header{{"Title", "Lobby Announced"}},
        cpr::Timeout{3000}
    );

    is_running_ = true;
    status_.state = ConnectionState::Connected;

    start_host_signaling();

    std::cout << "[P2P] Host session started for server " << server_id
              << " | Room Code: " << code 
              << " | Public: " << status_.public_endpoint
              << " | LAN: " << status_.local_endpoint << std::endl;

    return {
        {"status", "ok"},
        {"room_code", code},
        {"public_endpoint", status_.public_endpoint},
        {"local_endpoint", status_.local_endpoint},
        {"target_port", target_port}
    };
}

nlohmann::json P2PManager::stop_host() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_running_ || status_.role != SessionRole::Host) {
        return {{"status", "ok"}};
    }

    stop_host_signaling();
    delete_upnp_port_forward(status_.target_port);

    if (!status_.room_code.empty()) {
        nlohmann::json close_event = {
            {"event", "closed"},
            {"room_code", status_.room_code}
        };
        cpr::Post(
            cpr::Url{"https://ntfy.sh/beacon_p2p_room_" + status_.room_code},
            cpr::Body{close_event.dump()},
            cpr::Timeout{2000}
        );
    }

    is_running_ = false;
    status_.role = SessionRole::None;
    status_.state = ConnectionState::Idle;
    status_.room_code = "";
    status_.connected_peers.clear();

    std::cout << "[P2P] Host session stopped." << std::endl;
    return {{"status", "ok"}};
}

nlohmann::json P2PManager::join_session(const std::string& room_code, uint16_t local_listen_port) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (is_running_) {
        if (status_.role == SessionRole::Client && status_.room_code == room_code) {
            return {
                {"status", "ok"},
                {"room_code", status_.room_code},
                {"local_endpoint", "127.0.0.1:" + std::to_string(status_.local_proxy_port)}
            };
        }
        return {{"error", "Already connected to a session. Disconnect first."}};
    }

    std::string clean_code = room_code;
    clean_code.erase(0, clean_code.find_first_not_of(" \t\n\r"));
    clean_code.erase(clean_code.find_last_not_of(" \t\n\r") + 1);

    if (clean_code.empty()) {
        return {{"error", "Please enter a valid room code or host address."}};
    }

    std::string target_ip;
    uint16_t target_port = 25565;

    // Check if input is a direct IP:Port (e.g. 192.168.0.11:25565 or 127.0.0.1:25565)
    auto colon_idx = clean_code.find(':');
    if (colon_idx != std::string::npos && clean_code.rfind("BCN-", 0) != 0) {
        target_ip = clean_code.substr(0, colon_idx);
        try {
            target_port = static_cast<uint16_t>(std::stoi(clean_code.substr(colon_idx + 1)));
        } catch (...) {
            target_port = 25565;
        }
    } else {
        std::transform(clean_code.begin(), clean_code.end(), clean_code.begin(), ::toupper);

        // Query signaling endpoint for BCN-XXXX
        auto res = cpr::Get(
            cpr::Url{"https://ntfy.sh/beacon_p2p_room_" + clean_code + "/json?poll=1"},
            cpr::Timeout{4000}
        );

        if (res.status_code != 200 || res.text.empty()) {
            return {{"error", "Lobby " + clean_code + " was not found or the host is offline. Ensure the host has started hosting."}};
        }

        std::string found_public_ip;
        std::string found_local_ip;
        uint16_t found_port = 25565;
        bool found_room = false;

        std::stringstream ss(res.text);
        std::string line;
        while (std::getline(ss, line)) {
            if (line.empty()) continue;
            try {
                auto j = nlohmann::json::parse(line);
                if (j.contains("message")) {
                    auto msg = nlohmann::json::parse(j["message"].get<std::string>());
                    if (msg.contains("room_code") && msg["room_code"] == clean_code) {
                        found_public_ip = msg.value("public_ip", "");
                        found_local_ip = msg.value("local_ip", "");
                        found_port = msg.value("port", 25565);
                        found_room = true;
                    }
                }
            } catch (...) {}
        }

        if (!found_room) {
            return {{"error", "Lobby " + clean_code + " was not found. Please verify the code with the host."}};
        }

        // Try local LAN address first if on same subnet, else public IP
        target_port = found_port;
        bool local_reachable = false;
        if (!found_local_ip.empty() && found_local_ip != "127.0.0.1") {
            int test_sock = socket(AF_INET, SOCK_STREAM, 0);
            if (test_sock >= 0) {
                struct sockaddr_in test_addr{};
                test_addr.sin_family = AF_INET;
                test_addr.sin_port = htons(target_port);
                test_addr.sin_addr.s_addr = inet_addr(found_local_ip.c_str());

#ifdef _WIN32
                DWORD tv = 1000;
                setsockopt(test_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
                setsockopt(test_sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#else
                struct timeval tv{1, 0};
                setsockopt(test_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                setsockopt(test_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
                if (connect(test_sock, (struct sockaddr*)&test_addr, sizeof(test_addr)) == 0) {
                    local_reachable = true;
                }
#ifdef _WIN32
                closesocket(test_sock);
#else
                close(test_sock);
#endif
            }
        }

        if (local_reachable) {
            target_ip = found_local_ip;
            std::cout << "[P2P] Connecting via LAN address: " << target_ip << ":" << target_port << std::endl;
        } else if (!found_public_ip.empty()) {
            target_ip = found_public_ip;
            std::cout << "[P2P] Connecting via Public endpoint: " << target_ip << ":" << target_port << std::endl;
        } else if (!found_local_ip.empty()) {
            target_ip = found_local_ip;
        } else {
            return {{"error", "Host did not report a valid address."}};
        }
    }

    client_target_host_ip_ = target_ip;
    client_target_host_port_ = target_port;

    status_.role = SessionRole::Client;
    status_.state = ConnectionState::Connecting;
    status_.room_code = clean_code;
    status_.local_proxy_port = local_listen_port;
    status_.error_message = "";

    // Start local TCP client proxy forwarder
    start_client_proxy(local_listen_port, target_ip, target_port);

    // Announce client join to host
    my_client_peer_id_ = "Peer-" + std::to_string(rand() % 9000 + 1000);
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char time_str[32];
    std::strftime(time_str, sizeof(time_str), "%H:%M:%S", std::localtime(&now));

    nlohmann::json peer_event = {
        {"event", "join"},
        {"peer_id", my_client_peer_id_},
        {"endpoint", discover_local_ip()},
        {"connected_at", std::string(time_str)}
    };

    cpr::Post(
        cpr::Url{"https://ntfy.sh/beacon_p2p_peers_" + clean_code},
        cpr::Body{peer_event.dump()},
        cpr::Timeout{3000}
    );

    is_running_ = true;
    status_.state = ConnectionState::Connected;

    start_client_heartbeat();

    std::cout << "[P2P] Client bridged to room " << clean_code 
              << " (" << target_ip << ":" << target_port << ") via local proxy 127.0.0.1:" << status_.local_proxy_port << std::endl;

    return {
        {"status", "ok"},
        {"room_code", clean_code},
        {"local_endpoint", "127.0.0.1:" + std::to_string(status_.local_proxy_port)},
        {"target_endpoint", target_ip + ":" + std::to_string(target_port)}
    };
}

nlohmann::json P2PManager::leave_session() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_running_ || status_.role != SessionRole::Client) {
        return {{"status", "ok"}};
    }

    if (!status_.room_code.empty()) {
        nlohmann::json leave_event = {
            {"event", "leave"},
            {"peer_id", my_client_peer_id_}
        };
        cpr::Post(
            cpr::Url{"https://ntfy.sh/beacon_p2p_peers_" + status_.room_code},
            cpr::Body{leave_event.dump()},
            cpr::Timeout{2000}
        );
    }

    stop_client_heartbeat();
    stop_client_proxy();

    is_running_ = false;
    status_.role = SessionRole::None;
    status_.state = ConnectionState::Idle;
    status_.room_code = "";

    std::cout << "[P2P] Left session." << std::endl;
    return {{"status", "ok"}};
}

nlohmann::json P2PManager::get_status() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string role_str = "none";
    if (status_.role == SessionRole::Host) role_str = "host";
    else if (status_.role == SessionRole::Client) role_str = "client";

    std::string state_str = "idle";
    switch (status_.state) {
        case ConnectionState::Idle: state_str = "idle"; break;
        case ConnectionState::Connecting: state_str = "connecting"; break;
        case ConnectionState::Connected: state_str = "active"; break;
        case ConnectionState::Disconnected: state_str = "disconnected"; break;
        case ConnectionState::Error: state_str = "error"; break;
    }

    nlohmann::json peers_json = nlohmann::json::array();
    for (const auto& peer : status_.connected_peers) {
        peers_json.push_back({
            {"id", peer.id},
            {"endpoint", peer.endpoint},
            {"connected_at", peer.connected_at},
            {"bytes_sent", peer.bytes_sent},
            {"bytes_received", peer.bytes_received}
        });
    }

    return {
        {"role", role_str},
        {"status", state_str},
        {"room_code", status_.room_code},
        {"server_id", status_.server_id},
        {"target_port", status_.target_port},
        {"local_proxy_port", status_.local_proxy_port},
        {"public_endpoint", status_.public_endpoint},
        {"local_endpoint", status_.local_endpoint},
        {"error", status_.error_message},
        {"active_connections", peers_json}
    };
}

} // namespace beacon::p2p
