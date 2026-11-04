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
    std::string lan_room_code;
    std::string server_id;
    uint16_t target_port = 25565;
    uint16_t local_proxy_port = 25565;
    std::string public_endpoint;
    std::string local_endpoint;
    std::string error_message;
    bool upnp_active = false;
    bool server_listening = false;
    bool host_reachable = false;
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

    // UPnP discovery helpers
    bool try_upnp_port_forward(uint16_t port);
    bool delete_upnp_port_forward(uint16_t port);

    // Endpoint code encoding / decoding (Self-contained zero-cloud codes)
    static std::string encode_endpoint_code(const std::string& ip, uint16_t port);
    static bool decode_endpoint_code(const std::string& code, std::string& out_ip, uint16_t& out_port);
    static bool is_port_listening_locally(uint16_t port);

private:
    P2PManager();
    ~P2PManager();

    std::string generate_room_code();
    std::string discover_public_endpoint(uint16_t local_port);
    std::string discover_local_ip();

    // Client local TCP proxy listener
    void start_client_proxy(uint16_t listen_port, const std::string& primary_host_ip, const std::string& fallback_host_ip, uint16_t target_host_port);
    void stop_client_proxy();
    int connect_with_timeout(const std::string& ip, uint16_t port, int timeout_ms = 2500);

    // Background signaling workers
    void start_host_signaling();
    void stop_host_signaling();

    void start_client_heartbeat();
    void stop_client_heartbeat();

    std::mutex mutex_;
    SessionStatus status_;
    std::atomic<bool> is_running_{false};

    // Client Proxy state
    std::atomic<bool> proxy_running_{false};
    int proxy_listen_socket_{-1};
    std::unique_ptr<std::thread> client_listener_thread_;
    std::string client_primary_host_ip_;
    std::string client_fallback_host_ip_;
    uint16_t client_target_host_port_{25565};

    // Host signaling state
    std::atomic<bool> host_signaling_running_{false};
    std::unique_ptr<std::thread> host_signaling_thread_;

    // Client heartbeat state
    std::atomic<bool> client_heartbeat_running_{false};
    std::unique_ptr<std::thread> client_heartbeat_thread_;
    std::string my_client_peer_id_;

    // UPnP state
    std::string upnp_control_url_;
    std::string upnp_service_type_;
};

} // namespace beacon::p2p
