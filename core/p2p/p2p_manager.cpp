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
#define CLOSE_SOCKET(s) closesocket(s)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#define CLOSE_SOCKET(s) close(s)
#endif

namespace beacon::p2p {

static const std::string STUN_HOST = "stun.l.google.com";
static const uint16_t STUN_PORT = 19302;
static const std::string BORE_HOST = "bore.pub";
static const std::string BORE_FALLBACK_IP = "159.223.110.159";
static const uint16_t BORE_CONTROL_PORT = 7835;

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

std::string P2PManager::resolve_host_to_ip(const std::string& host) {
    if (host.empty() || host == "0.0.0.0") return "";

    unsigned int b0 = 0, b1 = 0, b2 = 0, b3 = 0;
    if (sscanf(host.c_str(), "%u.%u.%u.%u", &b0, &b1, &b2, &b3) == 4) {
        if (b0 <= 255 && b1 <= 255 && b2 <= 255 && b3 <= 255) {
            return host;
        }
    }

    if (host == "localhost") {
        return "127.0.0.1";
    }

    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;

    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 && res != nullptr) {
        char ip_str[INET_ADDRSTRLEN] = {0};
        struct sockaddr_in* ipv4 = (struct sockaddr_in*)res->ai_addr;
        inet_ntop(AF_INET, &(ipv4->sin_addr), ip_str, INET_ADDRSTRLEN);
        freeaddrinfo(res);
        if (strlen(ip_str) > 0) {
            return std::string(ip_str);
        }
    }

    if (host == BORE_HOST) {
        return BORE_FALLBACK_IP;
    }

    return "";
}

std::string P2PManager::encode_endpoint_code(const std::string& ip, uint16_t port) {
    if (ip.empty() || ip == "0.0.0.0") return "";
    unsigned int b0 = 0, b1 = 0, b2 = 0, b3 = 0;
    if (sscanf(ip.c_str(), "%u.%u.%u.%u", &b0, &b1, &b2, &b3) != 4) {
        return "";
    }
    char hex_buf[16];
    snprintf(hex_buf, sizeof(hex_buf), "%02X%02X%02X%02X%04X", b0 & 0xFF, b1 & 0xFF, b2 & 0xFF, b3 & 0xFF, port);
    std::string s(hex_buf);
    if (s.length() != 12) return "";
    return "BCN-" + s.substr(0, 4) + "-" + s.substr(4, 4) + "-" + s.substr(8, 4);
}

bool P2PManager::decode_endpoint_code(const std::string& code, std::string& out_ip, uint16_t& out_port) {
    std::string clean = code;
    clean.erase(0, clean.find_first_not_of(" \t\r\n"));
    clean.erase(clean.find_last_not_of(" \t\r\n") + 1);

    if (clean.rfind("BCN-", 0) == 0 || clean.rfind("bcn-", 0) == 0) {
        clean = clean.substr(4);
    }
    clean.erase(std::remove(clean.begin(), clean.end(), '-'), clean.end());
    clean.erase(std::remove(clean.begin(), clean.end(), ' '), clean.end());

    if (clean.length() == 12) {
        bool all_hex = true;
        for (char c : clean) {
            if (!std::isxdigit(static_cast<unsigned char>(c))) {
                all_hex = false;
                break;
            }
        }
        if (all_hex) {
            unsigned int b0 = 0, b1 = 0, b2 = 0, b3 = 0, port_val = 0;
            if (sscanf(clean.substr(0, 2).c_str(), "%x", &b0) == 1 &&
                sscanf(clean.substr(2, 2).c_str(), "%x", &b1) == 1 &&
                sscanf(clean.substr(4, 2).c_str(), "%x", &b2) == 1 &&
                sscanf(clean.substr(6, 2).c_str(), "%x", &b3) == 1 &&
                sscanf(clean.substr(8, 4).c_str(), "%x", &port_val) == 1) {
                out_ip = std::to_string(b0) + "." + std::to_string(b1) + "." +
                         std::to_string(b2) + "." + std::to_string(b3);
                out_port = static_cast<uint16_t>(port_val);
                return true;
            }
        }
    }
    return false;
}

bool P2PManager::is_port_listening_locally(uint16_t port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int res = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    bool connected = false;
    if (res == 0) {
        connected = true;
    } else {
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(sock, &write_fds);
        struct timeval tv{0, 200000}; // 200ms
        if (select(sock + 1, nullptr, &write_fds, nullptr, &tv) > 0) {
            int err = 0;
#ifdef _WIN32
            int elen = sizeof(err);
#else
            socklen_t elen = sizeof(err);
#endif
            getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&err, &elen);
            if (err == 0) connected = true;
        }
    }

    CLOSE_SOCKET(sock);
    return connected;
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
            CLOSE_SOCKET(sock);
            return ip;
        }
    }
    CLOSE_SOCKET(sock);
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
        CLOSE_SOCKET(sock);
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

    CLOSE_SOCKET(sock);

    if (!mapped_endpoint.empty()) {
        auto colon = mapped_endpoint.find(':');
        std::string ip = (colon != std::string::npos) ? mapped_endpoint.substr(0, colon) : mapped_endpoint;
        if (!ip.empty() && ip != "0.0.0.0" && ip != "127.0.0.1") {
            return ip + ":" + std::to_string(local_port);
        }
    }

    // Fast HTTP Fallback via api.ipify.org
    try {
        auto ip_res = cpr::Get(
            cpr::Url{"https://api.ipify.org?format=json"},
            cpr::Header{{"User-Agent", "Beacon/2.0"}},
            cpr::Timeout{2000}
        );
        if (ip_res.status_code == 200 && !ip_res.text.empty()) {
            auto j = nlohmann::json::parse(ip_res.text);
            std::string pub_ip = j.value("ip", "");
            if (!pub_ip.empty() && pub_ip.find('.') != std::string::npos) {
                std::cout << "[P2P] Discovered public IP via HTTP fallback: " << pub_ip << std::endl;
                return pub_ip + ":" + std::to_string(local_port);
            }
        }
    } catch (...) {}

    return mapped_endpoint;
}

int P2PManager::connect_with_timeout(const std::string& ip_or_host, uint16_t port, int timeout_ms) {
    if (ip_or_host.empty() || ip_or_host == "0.0.0.0") return -1;

    std::string ip = resolve_host_to_ip(ip_or_host);
    if (ip.empty() || ip == "0.0.0.0") return -1;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif

    struct sockaddr_in raddr{};
    raddr.sin_family = AF_INET;
    raddr.sin_port = htons(port);
    raddr.sin_addr.s_addr = inet_addr(ip.c_str());

    int res = connect(sock, (struct sockaddr*)&raddr, sizeof(raddr));
    if (res < 0) {
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
            CLOSE_SOCKET(sock);
            return -1;
        }
#else
        if (errno != EINPROGRESS) {
            CLOSE_SOCKET(sock);
            return -1;
        }
#endif
        fd_set write_fds, err_fds;
        FD_ZERO(&write_fds);
        FD_ZERO(&err_fds);
        FD_SET(sock, &write_fds);
        FD_SET(sock, &err_fds);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int sel = select(sock + 1, nullptr, &write_fds, &err_fds, &tv);
        if (sel <= 0) {
            CLOSE_SOCKET(sock);
            return -1;
        }

        int so_error = 0;
#ifdef _WIN32
        int optlen = sizeof(so_error);
#else
        socklen_t optlen = sizeof(so_error);
#endif
        getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&so_error, &optlen);
        if (so_error != 0) {
            CLOSE_SOCKET(sock);
            return -1;
        }
    }

#ifdef _WIN32
    mode = 0;
    ioctlsocket(sock, FIONBIO, &mode);
    int nodelay = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
#else
    fcntl(sock, F_SETFL, flags);
    int nodelay = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
#endif

    return sock;
}

// -------------------------------------------------------------
// Outbound Public Reverse Tunnel (bore.pub protocol)
// -------------------------------------------------------------

bool P2PManager::start_public_tunnel(uint16_t local_target_port) {
    stop_public_tunnel();

    std::cout << "[P2P] Initiating outbound reverse tunnel to bore.pub:7835..." << std::endl;
    int ctrl_sock = connect_with_timeout(BORE_HOST, BORE_CONTROL_PORT, 4000);
    if (ctrl_sock < 0) {
        ctrl_sock = connect_with_timeout(BORE_FALLBACK_IP, BORE_CONTROL_PORT, 4000);
    }
    if (ctrl_sock < 0) {
        std::cerr << "[P2P] Failed to connect to bore.pub control server." << std::endl;
        return false;
    }

    std::string hello_msg = "{\"Hello\":0}";
    hello_msg.push_back('\0');
    int sent = send(ctrl_sock, hello_msg.data(), static_cast<int>(hello_msg.size()), 0);
    if (sent < static_cast<int>(hello_msg.size())) {
        std::cerr << "[P2P] Failed to send Hello to bore.pub." << std::endl;
        CLOSE_SOCKET(ctrl_sock);
        return false;
    }

    std::string response_buf;
    auto start_time = std::chrono::steady_clock::now();
    bool got_response = false;

    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(5)) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(ctrl_sock, &read_fds);
        struct timeval tv{1, 0};
        int sel = select(ctrl_sock + 1, &read_fds, nullptr, nullptr, &tv);
        if (sel > 0 && FD_ISSET(ctrl_sock, &read_fds)) {
            char temp[512];
            int n = recv(ctrl_sock, temp, sizeof(temp), 0);
            if (n <= 0) break;
            response_buf.append(temp, n);
            if (response_buf.find('\0') != std::string::npos) {
                got_response = true;
                break;
            }
        }
    }

    if (!got_response) {
        std::cerr << "[P2P] Timeout waiting for bore.pub Hello response." << std::endl;
        CLOSE_SOCKET(ctrl_sock);
        return false;
    }

    size_t null_pos = response_buf.find('\0');
    std::string initial_msg = response_buf.substr(0, null_pos);
    std::string remaining_buf = response_buf.substr(null_pos + 1);

    uint16_t remote_port = 0;
    try {
        auto j = nlohmann::json::parse(initial_msg);
        if (j.contains("Hello") && j["Hello"].is_number()) {
            remote_port = j["Hello"].get<uint16_t>();
        } else if (j.contains("Error")) {
            std::cerr << "[P2P] Bore server error: " << j["Error"].get<std::string>() << std::endl;
            CLOSE_SOCKET(ctrl_sock);
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "[P2P] Failed to parse bore Hello response: " << e.what() << " | Raw: " << initial_msg << std::endl;
        CLOSE_SOCKET(ctrl_sock);
        return false;
    }

    if (remote_port == 0) {
        std::cerr << "[P2P] Invalid remote port assigned by bore.pub." << std::endl;
        CLOSE_SOCKET(ctrl_sock);
        return false;
    }

    tunnel_control_socket_ = ctrl_sock;
    tunnel_remote_port_ = remote_port;
    tunnel_public_endpoint_ = BORE_HOST + ":" + std::to_string(remote_port);
    tunnel_running_ = true;

    std::cout << "[P2P] Outbound reverse tunnel active! Public endpoint: " << tunnel_public_endpoint_ << std::endl;

    tunnel_thread_ = std::make_unique<std::thread>(&P2PManager::tunnel_worker_loop, this, local_target_port, remaining_buf);
    return true;
}

void P2PManager::tunnel_worker_loop(uint16_t local_target_port, std::string initial_buf) {
    std::string rx_buf = std::move(initial_buf);

    while (tunnel_running_) {
        // Process any messages delimited by \0
        size_t null_idx = rx_buf.find('\0');
        while (null_idx != std::string::npos) {
            std::string msg_str = rx_buf.substr(0, null_idx);
            rx_buf.erase(0, null_idx + 1);

            if (!msg_str.empty()) {
                try {
                    auto j = nlohmann::json::parse(msg_str);
                    if (j.is_string() && j.get<std::string>() == "Heartbeat") {
                        // Keepalive from server - active
                    } else if (j.is_object() && j.contains("Connection")) {
                        std::string conn_uuid = j["Connection"].get<std::string>();
                        if (!conn_uuid.empty()) {
                            std::cout << "[P2P] Incoming player tunnel connection: " << conn_uuid << std::endl;
                            std::thread(&P2PManager::handle_tunnel_connection, this, conn_uuid, local_target_port).detach();
                        }
                    } else if (j.is_object() && j.contains("Error")) {
                        std::cerr << "[P2P] Bore tunnel control error: " << j["Error"].get<std::string>() << std::endl;
                    }
                } catch (...) {
                    // Ignore malformed packet
                }
            }
            null_idx = rx_buf.find('\0');
        }

        if (!tunnel_running_) break;

        fd_set read_fds;
        FD_ZERO(&read_fds);
        int sock = tunnel_control_socket_;
        if (sock < 0) break;
        FD_SET(sock, &read_fds);

        struct timeval tv{0, 500000}; // 500ms
        int sel = select(sock + 1, &read_fds, nullptr, nullptr, &tv);
        if (sel > 0 && FD_ISSET(sock, &read_fds)) {
            char chunk[2048];
            int n = recv(sock, chunk, sizeof(chunk), 0);
            if (n <= 0) {
                if (tunnel_running_) {
                    std::cout << "[P2P] Tunnel control connection closed by remote server." << std::endl;
                }
                break;
            }
            rx_buf.append(chunk, n);
        }
    }

    // Auto-reconnect if tunnel was unexpectedly dropped
    if (tunnel_running_) {
        std::cout << "[P2P] Tunnel dropped. Reconnecting..." << std::endl;
        if (tunnel_control_socket_ >= 0) {
            CLOSE_SOCKET(tunnel_control_socket_);
            tunnel_control_socket_ = -1;
        }

        int retries = 0;
        while (tunnel_running_ && retries < 15) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!tunnel_running_) break;

            int new_sock = connect_with_timeout(BORE_HOST, BORE_CONTROL_PORT, 4000);
            if (new_sock < 0) new_sock = connect_with_timeout(BORE_FALLBACK_IP, BORE_CONTROL_PORT, 4000);
            if (new_sock >= 0) {
                std::string hello_msg = "{\"Hello\":" + std::to_string(tunnel_remote_port_) + "}";
                hello_msg.push_back('\0');
                if (send(new_sock, hello_msg.data(), static_cast<int>(hello_msg.size()), 0) == static_cast<int>(hello_msg.size())) {
                    std::string resp;
                    char temp[512];
                    int n = recv(new_sock, temp, sizeof(temp), 0);
                    if (n > 0) {
                        resp.append(temp, n);
                        size_t npos = resp.find('\0');
                        if (npos != std::string::npos) {
                            try {
                                auto j = nlohmann::json::parse(resp.substr(0, npos));
                                if (j.contains("Hello")) {
                                    tunnel_remote_port_ = j["Hello"].get<uint16_t>();
                                    tunnel_public_endpoint_ = BORE_HOST + ":" + std::to_string(tunnel_remote_port_);
                                    tunnel_control_socket_ = new_sock;
                                    std::string rem = resp.substr(npos + 1);
                                    {
                                        std::lock_guard<std::mutex> lock(mutex_);
                                        status_.public_endpoint = tunnel_public_endpoint_;
                                        status_.room_code = encode_endpoint_code(BORE_FALLBACK_IP, tunnel_remote_port_);
                                    }
                                    std::cout << "[P2P] Tunnel re-established on " << tunnel_public_endpoint_ << std::endl;
                                    tunnel_worker_loop(local_target_port, rem);
                                    return;
                                }
                            } catch (...) {}
                        }
                    }
                }
                CLOSE_SOCKET(new_sock);
            }
            retries++;
        }
    }
}

void P2PManager::handle_tunnel_connection(const std::string& conn_uuid, uint16_t local_target_port) {
    std::cout << "[P2P] Handling incoming tunnel connection: " << conn_uuid << std::endl;

    int proxy_sock = connect_with_timeout(BORE_HOST, BORE_CONTROL_PORT, 3000);
    if (proxy_sock < 0) {
        proxy_sock = connect_with_timeout(BORE_FALLBACK_IP, BORE_CONTROL_PORT, 3000);
    }
    if (proxy_sock < 0) {
        std::cerr << "[P2P] Failed to connect to bore proxy for connection " << conn_uuid << std::endl;
        return;
    }

    std::string accept_msg = "{\"Accept\":\"" + conn_uuid + "\"}";
    accept_msg.push_back('\0');
    int sent = send(proxy_sock, accept_msg.data(), static_cast<int>(accept_msg.size()), 0);
    if (sent < static_cast<int>(accept_msg.size())) {
        std::cerr << "[P2P] Failed to send Accept for connection " << conn_uuid << std::endl;
        CLOSE_SOCKET(proxy_sock);
        return;
    }

    int local_sock = connect_with_timeout("127.0.0.1", local_target_port, 2500);
    if (local_sock < 0) {
        std::cerr << "[P2P] Tunnel failed to connect to local server on 127.0.0.1:" << local_target_port << std::endl;
        CLOSE_SOCKET(proxy_sock);
        return;
    }

    int nodelay = 1;
    setsockopt(proxy_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
    setsockopt(local_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

    std::string peer_id = "Player-" + (conn_uuid.length() >= 8 ? conn_uuid.substr(0, 8) : conn_uuid);
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", std::localtime(&now));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.connected_peers.push_back({
            peer_id,
            "bore.pub Tunnel",
            std::string(time_buf),
            0, 0
        });
    }
    std::cout << "[P2P] Direct tunnel active for " << peer_id << " to local Minecraft server." << std::endl;

    std::atomic<bool> active{true};
    std::atomic<uint64_t> bytes_sent{0};
    std::atomic<uint64_t> bytes_recv{0};

    auto forward_stream = [](int src, int dst, std::atomic<bool>& act, std::atomic<uint64_t>& counter) {
        char buf[16384];
        while (act) {
            int n = recv(src, buf, sizeof(buf), 0);
            if (n <= 0) break;
            int s = 0;
            while (s < n) {
                int w = send(dst, buf + s, n - s, 0);
                if (w <= 0) break;
                s += w;
            }
            if (s < n) break;
            counter += n;
        }
        act = false;
#ifdef _WIN32
        shutdown(dst, SD_BOTH);
#else
        shutdown(dst, SHUT_RDWR);
#endif
    };

    std::thread t1(forward_stream, proxy_sock, local_sock, std::ref(active), std::ref(bytes_sent));
    std::thread t2(forward_stream, local_sock, proxy_sock, std::ref(active), std::ref(bytes_recv));

    if (t1.joinable()) t1.join();
    if (t2.joinable()) t2.join();

    CLOSE_SOCKET(proxy_sock);
    CLOSE_SOCKET(local_sock);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.connected_peers.erase(
            std::remove_if(status_.connected_peers.begin(), status_.connected_peers.end(),
                [&peer_id](const PeerInfo& p) { return p.id == peer_id; }),
            status_.connected_peers.end()
        );
    }
    std::cout << "[P2P] Tunnel connection closed for " << peer_id << " (Sent: " << bytes_sent << ", Recv: " << bytes_recv << ")" << std::endl;
}

void P2PManager::stop_public_tunnel() {
    tunnel_running_ = false;
    if (tunnel_control_socket_ >= 0) {
#ifdef _WIN32
        shutdown(tunnel_control_socket_, SD_BOTH);
        closesocket(tunnel_control_socket_);
#else
        shutdown(tunnel_control_socket_, SHUT_RDWR);
        close(tunnel_control_socket_);
#endif
        tunnel_control_socket_ = -1;
    }
    if (tunnel_thread_ && tunnel_thread_->joinable()) {
        if (std::this_thread::get_id() != tunnel_thread_->get_id()) {
            tunnel_thread_->join();
        } else {
            tunnel_thread_->detach();
        }
        tunnel_thread_.reset();
    }
    tunnel_remote_port_ = 0;
    tunnel_public_endpoint_.clear();
}

// -------------------------------------------------------------
// UPnP Port Forwarding
// -------------------------------------------------------------

bool P2PManager::try_upnp_port_forward(uint16_t port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;

    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast, sizeof(broadcast));

#ifdef _WIN32
    DWORD tv = 1200;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
    struct timeval tv{1, 200000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(1900);
    dest.sin_addr.s_addr = inet_addr("239.255.255.250");

    std::string msg1 = 
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n";

    std::string msg2 = 
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: urn:schemas-upnp-org:service:WANIPConnection:1\r\n\r\n";

    std::string msg3 = 
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:2\r\n\r\n";

    std::string msg4 = 
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: urn:schemas-upnp-org:service:WANIPConnection:2\r\n\r\n";

    sendto(sock, msg1.c_str(), msg1.size(), 0, (struct sockaddr*)&dest, sizeof(dest));
    sendto(sock, msg2.c_str(), msg2.size(), 0, (struct sockaddr*)&dest, sizeof(dest));
    sendto(sock, msg3.c_str(), msg3.size(), 0, (struct sockaddr*)&dest, sizeof(dest));
    sendto(sock, msg4.c_str(), msg4.size(), 0, (struct sockaddr*)&dest, sizeof(dest));

    bool mapped = false;

    for (int attempt = 0; attempt < 10; ++attempt) {
        char buf[2048] = {0};
        struct sockaddr_in from{};
#ifdef _WIN32
        int from_len = sizeof(from);
#else
        socklen_t from_len = sizeof(from);
#endif
        auto n = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&from, &from_len);
        if (n <= 0) break;

        std::string resp(buf, n);
        auto loc_pos = resp.find("LOCATION:");
        if (loc_pos == std::string::npos) {
            loc_pos = resp.find("Location:");
        }
        if (loc_pos == std::string::npos) continue;

        auto line_end = resp.find("\r\n", loc_pos);
        if (line_end == std::string::npos) line_end = resp.find("\n", loc_pos);
        std::string location_url = resp.substr(loc_pos + 9, line_end - (loc_pos + 9));
        location_url.erase(0, location_url.find_first_not_of(" \t"));
        location_url.erase(location_url.find_last_not_of(" \t\r\n") + 1);

        if (location_url.empty()) continue;

        auto desc_res = cpr::Get(cpr::Url{location_url}, cpr::Timeout{2500});
        if (desc_res.status_code != 200 || desc_res.text.empty()) continue;

        std::string service_type = "urn:schemas-upnp-org:service:WANIPConnection:1";
        auto s_pos = desc_res.text.find(service_type);
        if (s_pos == std::string::npos) {
            service_type = "urn:schemas-upnp-org:service:WANPPPConnection:1";
            s_pos = desc_res.text.find(service_type);
        }
        if (s_pos == std::string::npos) continue;

        auto ctrl_pos = desc_res.text.find("<controlURL>", s_pos);
        if (ctrl_pos == std::string::npos) continue;
        auto ctrl_end = desc_res.text.find("</controlURL>", ctrl_pos);
        if (ctrl_end == std::string::npos) continue;

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
            cpr::Timeout{2500}
        );

        if (soap_res.status_code != 200) {
            std::string soap_body_leased = soap_body;
            auto lease_pos = soap_body_leased.find("<NewLeaseDuration>0</NewLeaseDuration>");
            if (lease_pos != std::string::npos) {
                soap_body_leased.replace(lease_pos, 38, "<NewLeaseDuration>86400</NewLeaseDuration>");
                soap_res = cpr::Post(
                    cpr::Url{control_url},
                    cpr::Header{
                        {"Content-Type", "text/xml; charset=\"utf-8\""},
                        {"SOAPAction", "\"" + service_type + "#AddPortMapping\""}
                    },
                    cpr::Body{soap_body_leased},
                    cpr::Timeout{2500}
                );
            }
        }

        if (soap_res.status_code == 200 || soap_res.text.find("AddPortMappingResponse") != std::string::npos) {
            std::cout << "[P2P] UPnP AddPortMapping succeeded for port " << port << " on " << control_url << std::endl;
            mapped = true;
            break;
        }
    }

    CLOSE_SOCKET(sock);
    return mapped;
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

// -------------------------------------------------------------
// Host Session Management
// -------------------------------------------------------------

void P2PManager::start_host_signaling() {
    stop_host_signaling();
    host_signaling_running_ = true;

    host_signaling_thread_ = std::make_unique<std::thread>([this]() {
        while (host_signaling_running_) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!host_signaling_running_) break;

            std::string server_id;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!is_running_ || status_.role != SessionRole::Host) break;
                server_id = status_.server_id;
            }

            // Sync active in-game Minecraft players from server log parser
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
            std::this_thread::sleep_for(std::chrono::seconds(5));
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

// -------------------------------------------------------------
// Client Local TCP Proxy
// -------------------------------------------------------------

void P2PManager::start_client_proxy(uint16_t listen_port, const std::string& primary_host_ip, const std::string& fallback_host_ip, uint16_t target_host_port) {
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

    while (bind(proxy_listen_socket_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        bound_port++;
        if (bound_port > listen_port + 10) {
            std::cerr << "[P2P] Failed to bind local client proxy listener." << std::endl;
            CLOSE_SOCKET(proxy_listen_socket_);
            proxy_listen_socket_ = -1;
            return;
        }
        addr.sin_port = htons(bound_port);
    }

    if (listen(proxy_listen_socket_, 5) < 0) {
        CLOSE_SOCKET(proxy_listen_socket_);
        proxy_listen_socket_ = -1;
        return;
    }

    status_.local_proxy_port = bound_port;
    proxy_running_ = true;

    client_listener_thread_ = std::make_unique<std::thread>([this, bound_port, primary_host_ip, fallback_host_ip, target_host_port]() {
        std::cout << "[P2P] Local TCP proxy listening on 127.0.0.1:" << bound_port 
                  << " -> Target: " << primary_host_ip << ":" << target_host_port << std::endl;

        while (proxy_running_) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            int sock = proxy_listen_socket_;
            if (sock < 0) break;
            FD_SET(sock, &read_fds);

            struct timeval tv{0, 500000}; // 500ms
            int sel = select(sock + 1, &read_fds, nullptr, nullptr, &tv);
            if (sel <= 0) {
                if (!proxy_running_) break;
                continue;
            }

            struct sockaddr_in client_addr{};
#ifdef _WIN32
            int client_len = sizeof(client_addr);
#else
            socklen_t client_len = sizeof(client_addr);
#endif
            int client_sock = accept(proxy_listen_socket_, (struct sockaddr*)&client_addr, &client_len);
            if (client_sock < 0) {
                if (!proxy_running_) break;
                continue;
            }

            std::cout << "[P2P] Minecraft client connected to local proxy! Attempting connection to host..." << std::endl;

            std::thread([this, client_sock, primary_host_ip, fallback_host_ip, target_host_port]() {
                std::vector<std::string> candidates;
                if (!primary_host_ip.empty()) candidates.push_back(primary_host_ip);
                if (!fallback_host_ip.empty() && fallback_host_ip != primary_host_ip) {
                    candidates.push_back(fallback_host_ip);
                }
                std::string my_ip = discover_local_ip();
                if (primary_host_ip == my_ip || fallback_host_ip == my_ip || primary_host_ip == "127.0.0.1" || fallback_host_ip == "127.0.0.1") {
                    if (std::find(candidates.begin(), candidates.end(), "127.0.0.1") == candidates.end()) {
                        candidates.push_back("127.0.0.1");
                    }
                }

                int remote_sock = -1;
                std::string connected_ip;
                for (const auto& candidate : candidates) {
                    std::cout << "[P2P] Probing host endpoint " << candidate << ":" << target_host_port << "..." << std::endl;
                    remote_sock = connect_with_timeout(candidate, target_host_port, 2500);
                    if (remote_sock >= 0) {
                        connected_ip = candidate;
                        std::cout << "[P2P] Successfully connected to host at " << candidate << ":" << target_host_port << "!" << std::endl;
                        break;
                    }
                }

                if (remote_sock < 0) {
                    std::string cand_str;
                    for (size_t i = 0; i < candidates.size(); ++i) {
                        if (i > 0) cand_str += ", ";
                        cand_str += candidates[i];
                    }
                    std::string err = "Failed to connect to remote host (tried " + cand_str + " on port " + std::to_string(target_host_port) + "). Ensure the host Minecraft server is running.";
                    std::cerr << "[P2P] " << err << std::endl;

                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        status_.error_message = err;
                        status_.host_reachable = false;
                    }

                    CLOSE_SOCKET(client_sock);
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    status_.error_message = "";
                    status_.host_reachable = true;
                    client_primary_host_ip_ = connected_ip;
                }

                int nodelay = 1;
                setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
                setsockopt(remote_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

                std::cout << "[P2P] Active bidirectional tunnel established between Minecraft client and host (" << connected_ip << ":" << target_host_port << ")!" << std::endl;

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

                CLOSE_SOCKET(client_sock);
                CLOSE_SOCKET(remote_sock);
                std::cout << "[P2P] Local proxy client connection closed." << std::endl;
            }).detach();
        }
    });
}

void P2PManager::stop_client_proxy() {
    proxy_running_ = false;
    if (proxy_listen_socket_ >= 0) {
        CLOSE_SOCKET(proxy_listen_socket_);
        proxy_listen_socket_ = -1;
    }
    if (client_listener_thread_ && client_listener_thread_->joinable()) {
        client_listener_thread_->join();
        client_listener_thread_.reset();
    }
}

// -------------------------------------------------------------
// Public Host / Client Session Control
// -------------------------------------------------------------

nlohmann::json P2PManager::start_host(const std::string& server_id, uint16_t target_port) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (is_running_) {
        if (status_.role == SessionRole::Host && (server_id.empty() || status_.server_id == server_id)) {
            return {
                {"status", "ok"},
                {"room_code", status_.room_code},
                {"lan_room_code", status_.lan_room_code},
                {"public_endpoint", status_.public_endpoint},
                {"local_endpoint", status_.local_endpoint},
                {"target_port", status_.target_port},
                {"upnp_active", status_.upnp_active},
                {"server_listening", status_.server_listening}
            };
        }
        return {{"error", "A P2P session is already running. Please stop it first."}};
    }

    std::string active_server_id = server_id;
    uint16_t active_port = target_port;

    // Detect running server if server_id is "default" or empty
    if (active_server_id.empty() || active_server_id == "default") {
        try {
            auto servers = get_servers();
            for (const auto& s : servers) {
                if (s.value("status", "") == "RUNNING") {
                    active_server_id = s.value("id", "");
                    if (target_port == 25565 && s.contains("port")) {
                        active_port = static_cast<uint16_t>(s.value("port", 25565));
                    }
                    std::cout << "[P2P] Auto-associated with running server: " << active_server_id << " on port " << active_port << std::endl;
                    break;
                }
            }
        } catch (...) {}
    }

    status_.role = SessionRole::Host;
    status_.state = ConnectionState::Connecting;
    status_.server_id = active_server_id;
    status_.target_port = active_port;
    status_.connected_peers.clear();
    status_.error_message = "";

    std::string local_ip = discover_local_ip();
    status_.local_endpoint = local_ip + ":" + std::to_string(active_port);
    status_.lan_room_code = encode_endpoint_code(local_ip, active_port);

    // Try UPnP port forward as local router optimization
    bool upnp_ok = try_upnp_port_forward(active_port);
    status_.upnp_active = upnp_ok;
    status_.server_listening = is_port_listening_locally(active_port);

    // Start zero-account public reverse tunnel
    bool tunnel_ok = start_public_tunnel(active_port);
    if (tunnel_ok) {
        status_.public_endpoint = tunnel_public_endpoint_;
        status_.room_code = encode_endpoint_code(BORE_FALLBACK_IP, tunnel_remote_port_);
    } else {
        std::cout << "[P2P] Reverse tunnel unavailable, falling back to STUN / UPnP." << std::endl;
        status_.public_endpoint = discover_public_endpoint(active_port);
        if (status_.public_endpoint.empty()) {
            status_.public_endpoint = status_.local_endpoint;
        }
        std::string pub_ip = status_.public_endpoint;
        auto colon_pos = pub_ip.find(':');
        if (colon_pos != std::string::npos) pub_ip = pub_ip.substr(0, colon_pos);
        status_.room_code = encode_endpoint_code(pub_ip, active_port);
        if (status_.room_code.empty()) {
            status_.room_code = status_.lan_room_code.empty() ? generate_room_code() : status_.lan_room_code;
        }
    }

    is_running_ = true;
    status_.state = ConnectionState::Connected;

    start_host_signaling();

    std::cout << "[P2P] Host session started for server " << active_server_id
              << " | Room Code: " << status_.room_code 
              << " | LAN Code: " << status_.lan_room_code
              << " | Public: " << status_.public_endpoint
              << " | LAN: " << status_.local_endpoint
              << " | Tunnel: " << (tunnel_ok ? "Active" : "Inactive")
              << " | UPnP: " << (upnp_ok ? "Active" : "Inactive")
              << " | Server Listening: " << (status_.server_listening ? "Yes" : "No") << std::endl;

    return {
        {"status", "ok"},
        {"room_code", status_.room_code},
        {"lan_room_code", status_.lan_room_code},
        {"public_endpoint", status_.public_endpoint},
        {"local_endpoint", status_.local_endpoint},
        {"target_port", active_port},
        {"upnp_active", upnp_ok},
        {"server_listening", status_.server_listening}
    };
}

nlohmann::json P2PManager::stop_host() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_running_ || status_.role != SessionRole::Host) {
        return {{"status", "ok"}};
    }

    stop_host_signaling();
    stop_public_tunnel();
    delete_upnp_port_forward(status_.target_port);

    is_running_ = false;
    status_.role = SessionRole::None;
    status_.state = ConnectionState::Idle;
    status_.room_code = "";
    status_.lan_room_code = "";
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
                {"local_endpoint", "127.0.0.1:" + std::to_string(status_.local_proxy_port)},
                {"target_endpoint", client_primary_host_ip_ + ":" + std::to_string(client_target_host_port_)},
                {"host_reachable", status_.host_reachable}
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

    std::string primary_ip;
    std::string fallback_ip;
    uint16_t target_port = 25565;
    bool is_direct_ip = false;

    // 1. Try decoding self-contained encoded room code (BCN-XXXX-XXXX-XXXX)
    if (decode_endpoint_code(clean_code, primary_ip, target_port)) {
        is_direct_ip = true;
        std::cout << "[P2P] Decoded self-contained room code " << clean_code << " -> " << primary_ip << ":" << target_port << std::endl;
    }

    // 2. Check if input is a direct host:port or host (e.g. bore.pub:12345 or 192.168.0.11:25565)
    if (!is_direct_ip) {
        auto colon_idx = clean_code.find(':');
        if (colon_idx != std::string::npos && clean_code.rfind("BCN-", 0) != 0 && clean_code.rfind("bcn-", 0) != 0) {
            primary_ip = clean_code.substr(0, colon_idx);
            try {
                target_port = static_cast<uint16_t>(std::stoi(clean_code.substr(colon_idx + 1)));
            } catch (...) {
                target_port = 25565;
            }
            is_direct_ip = true;
        } else if (clean_code.find('.') != std::string::npos && clean_code.rfind("BCN-", 0) != 0 && clean_code.rfind("bcn-", 0) != 0) {
            primary_ip = clean_code;
            target_port = 25565;
            is_direct_ip = true;
        }
    }

    if (!is_direct_ip) {
        return {{"error", "Could not resolve room code. Please use the Host's Room Code (BCN-XXXX-XXXX-XXXX) or Host Address (bore.pub:PORT or IP:PORT)."}};
    }

    // Subnet and local loopback detection
    std::string my_local_ip = discover_local_ip();
    if (!my_local_ip.empty()) {
        if (my_local_ip == primary_ip || primary_ip == "127.0.0.1") {
            primary_ip = "127.0.0.1";
            std::cout << "[P2P] Client and Host on same local machine (" << my_local_ip << "). Using loopback 127.0.0.1." << std::endl;
        } else if (!fallback_ip.empty()) {
            auto last_dot_my = my_local_ip.rfind('.');
            auto last_dot_fb = fallback_ip.rfind('.');
            if (last_dot_my != std::string::npos && last_dot_fb != std::string::npos) {
                if (my_local_ip.substr(0, last_dot_my) == fallback_ip.substr(0, last_dot_fb)) {
                    std::swap(primary_ip, fallback_ip);
                    std::cout << "[P2P] Prioritized local LAN endpoint: " << primary_ip << std::endl;
                }
            }
        }
    }

    // Proactive Host Reachability Probe
    int probe_sock = connect_with_timeout(primary_ip, target_port, 2500);
    bool host_reachable = false;
    if (probe_sock >= 0) {
        host_reachable = true;
        CLOSE_SOCKET(probe_sock);
        std::cout << "[P2P] Host reachability confirmed on " << primary_ip << ":" << target_port << std::endl;
    } else if (!fallback_ip.empty() && fallback_ip != primary_ip) {
        probe_sock = connect_with_timeout(fallback_ip, target_port, 2500);
        if (probe_sock >= 0) {
            host_reachable = true;
            CLOSE_SOCKET(probe_sock);
            std::swap(primary_ip, fallback_ip);
            std::cout << "[P2P] Host reachability confirmed on fallback " << primary_ip << ":" << target_port << std::endl;
        }
    }

    status_.host_reachable = host_reachable;
    if (!host_reachable) {
        status_.error_message = "Local bridge listening on 127.0.0.1:" + std::to_string(local_listen_port) +
            ", but host at " + primary_ip + ":" + std::to_string(target_port) +
            " did not answer. Ensure the host session is active.";
        std::cout << "[P2P] " << status_.error_message << std::endl;
    }

    client_primary_host_ip_ = primary_ip;
    client_fallback_host_ip_ = fallback_ip;
    client_target_host_port_ = target_port;

    status_.role = SessionRole::Client;
    status_.state = ConnectionState::Connecting;
    status_.room_code = clean_code;
    status_.local_proxy_port = local_listen_port;

    start_client_proxy(local_listen_port, primary_ip, fallback_ip, target_port);

    is_running_ = true;
    status_.state = ConnectionState::Connected;

    start_client_heartbeat();

    std::cout << "[P2P] Client bridged to " << clean_code 
              << " (" << primary_ip << ":" << target_port << ") via local proxy 127.0.0.1:" << status_.local_proxy_port 
              << " | Host Reachable: " << (host_reachable ? "Yes" : "No") << std::endl;

    return {
        {"status", "ok"},
        {"room_code", clean_code},
        {"local_endpoint", "127.0.0.1:" + std::to_string(status_.local_proxy_port)},
        {"target_endpoint", primary_ip + ":" + std::to_string(target_port)},
        {"host_reachable", host_reachable},
        {"warning", host_reachable ? "" : status_.error_message}
    };
}

nlohmann::json P2PManager::leave_session() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_running_ || status_.role != SessionRole::Client) {
        return {{"status", "ok"}};
    }

    stop_client_heartbeat();
    stop_client_proxy();

    is_running_ = false;
    status_.role = SessionRole::None;
    status_.state = ConnectionState::Idle;
    status_.room_code = "";
    status_.connected_peers.clear();

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

    bool listening = false;
    if (status_.role == SessionRole::Host) {
        listening = is_port_listening_locally(status_.target_port);
        status_.server_listening = listening;
    }

    return {
        {"role", role_str},
        {"status", state_str},
        {"room_code", status_.room_code},
        {"lan_room_code", status_.lan_room_code},
        {"server_id", status_.server_id},
        {"target_port", status_.target_port},
        {"local_proxy_port", status_.local_proxy_port},
        {"public_endpoint", status_.public_endpoint},
        {"local_endpoint", status_.local_endpoint},
        {"upnp_active", status_.upnp_active},
        {"server_listening", status_.server_listening},
        {"host_reachable", status_.host_reachable},
        {"error", status_.error_message},
        {"active_connections", peers_json}
    };
}

} // namespace beacon::p2p
