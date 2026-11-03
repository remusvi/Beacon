#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <nlohmann/json.hpp>

namespace beacon::p2p {

enum class SessionRole {
    None,
    Host,
    Client
};

enum class ConnectionState {
    Idle,
    Connecting,
    Connected,
    Disconnected,
    Error
};

struct PeerInfo {
    std::string id;
    std::string endpoint;
    std::string connected_at;
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
};

struct SessionStatus {
    SessionRole role = SessionRole::None;
    ConnectionState state = ConnectionState::Idle;
    std::string room_code;
    std::string server_id;
    uint16_t target_port = 25565;
    uint16_t local_proxy_port = 25565;
    std::string public_endpoint;
    std::string error_message;
    std::vector<PeerInfo> connected_peers;
};

class P2PManager {
public:
    static P2PManager& instance();

    // Host session for a local server
    nlohmann::json start_host(const std::string& server_id, uint16_t target_port = 25565);
    nlohmann::json stop_host();

    // Client connects to a remote host session
    nlohmann::json join_session(const std::string& room_code, uint16_t local_listen_port = 25565);
    nlohmann::json leave_session();

    // Status query
    nlohmann::json get_status();

    // UPnP discovery helper
    bool try_upnp_port_forward(uint16_t port);

private:
    P2PManager();
    ~P2PManager();

    std::string generate_room_code();
    std::string discover_public_endpoint(uint16_t local_port);

    // Client local TCP proxy listener
    void start_client_proxy(uint16_t listen_port);
    void stop_client_proxy();

    std::mutex mutex_;
    SessionStatus status_;
    std::atomic<bool> is_running_{false};
    std::atomic<bool> proxy_running_{false};
    int proxy_listen_socket_{-1};

    std::unique_ptr<std::thread> client_listener_thread_;
};

} // namespace beacon::p2p
