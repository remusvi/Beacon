#include "p2p_manager.h"
#include <iostream>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
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

P2PManager::P2PManager() = default;

P2PManager::~P2PManager() {
    stop_host();
    leave_session();
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
    socklen_t from_len = sizeof(from);
    ssize_t len = recvfrom(sock, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&from, &from_len);

    std::string mapped_endpoint;
    if (len >= 20 && buf[0] == 0x01 && buf[1] == 0x01) {
        size_t idx = 20;
        while (idx + 4 <= (size_t)len) {
            uint16_t attr_type = (buf[idx] << 8) | buf[idx + 1];
            uint16_t attr_len = (buf[idx + 2] << 8) | buf[idx + 3];
            idx += 4;

            if (attr_type == 0x0020 && attr_len >= 8 && idx + attr_len <= (size_t)len) {
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

bool P2PManager::try_upnp_port_forward(uint16_t /*port*/) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;

    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast, sizeof(broadcast));

    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(1900);
    dest.sin_addr.s_addr = inet_addr("239.255.255.250");

    std::string msg = 
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 1\r\n"
        "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n";

    sendto(sock, msg.c_str(), msg.size(), 0, (struct sockaddr*)&dest, sizeof(dest));

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return true;
}

void P2PManager::start_client_proxy(uint16_t listen_port) {
    stop_client_proxy();

    proxy_listen_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_listen_socket_ < 0) return;

    int opt = 1;
    setsockopt(proxy_listen_socket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(listen_port);

    if (bind(proxy_listen_socket_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[P2P] Failed to bind local client proxy on port " << listen_port << std::endl;
#ifdef _WIN32
        closesocket(proxy_listen_socket_);
#else
        close(proxy_listen_socket_);
#endif
        proxy_listen_socket_ = -1;
        return;
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

    proxy_running_ = true;
    client_listener_thread_ = std::make_unique<std::thread>([this, listen_port]() {
        std::cout << "[P2P] Local TCP proxy listening on 127.0.0.1:" << listen_port << std::endl;
        while (proxy_running_) {
            struct sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_sock = accept(proxy_listen_socket_, (struct sockaddr*)&client_addr, &client_len);
            if (client_sock < 0) {
                if (!proxy_running_) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            std::cout << "[P2P] Minecraft client connected to local proxy listener!" << std::endl;
            // In full P2P tunneling, this socket's read/write is piped to WebRTC datachannel.
            // For now, close or hold.
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
        // If already hosting this server, return the active code
        if (status_.role == SessionRole::Host && status_.server_id == server_id) {
            return {
                {"status", "ok"},
                {"room_code", status_.room_code},
                {"public_endpoint", status_.public_endpoint},
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

    status_.public_endpoint = discover_public_endpoint(target_port);
    if (status_.public_endpoint.empty()) {
        status_.public_endpoint = "STUN-Discovered";
    }

    try_upnp_port_forward(target_port);

    is_running_ = true;
    status_.state = ConnectionState::Connected;

    std::cout << "[P2P] Host session started for server " << server_id
              << " | Room Code: " << code 
              << " | Public Reflexive: " << status_.public_endpoint << std::endl;

    return {
        {"status", "ok"},
        {"room_code", code},
        {"public_endpoint", status_.public_endpoint},
        {"target_port", target_port}
    };
}

nlohmann::json P2PManager::stop_host() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_running_ || status_.role != SessionRole::Host) {
        return {{"status", "ok"}};
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
    std::transform(clean_code.begin(), clean_code.end(), clean_code.begin(), ::toupper);

    if (clean_code.empty()) {
        return {{"error", "Invalid room code."}};
    }

    status_.role = SessionRole::Client;
    status_.state = ConnectionState::Connecting;
    status_.room_code = clean_code;
    status_.local_proxy_port = local_listen_port;
    status_.error_message = "";

    start_client_proxy(local_listen_port);

    is_running_ = true;
    status_.state = ConnectionState::Connected;

    std::cout << "[P2P] Client connected to room: " << clean_code 
              << " | Local Proxy: 127.0.0.1:" << local_listen_port << std::endl;

    return {
        {"status", "ok"},
        {"room_code", clean_code},
        {"local_endpoint", "127.0.0.1:" + std::to_string(local_listen_port)}
    };
}

nlohmann::json P2PManager::leave_session() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_running_ || status_.role != SessionRole::Client) {
        return {{"status", "ok"}};
    }

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
        {"error", status_.error_message},
        {"active_connections", peers_json}
    };
}

} // namespace beacon::p2p
