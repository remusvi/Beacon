#include <cstdint>
#include <iostream>
#include <string>
#include "build/_deps/json-src/include/nlohmann/json_fwd.hpp"
#include "webview.h"
#include "index_html.h"
#include "core/servers/servers.h"
#include "./core/servers/serverEntity.h"

Provider determine_provider(const std::string& type_str) {
    if (type_str == "vanilla" || "Vanilla") return Provider::Vanilla;
    if (type_str == "forge")   return Provider::Forge;
    if (type_str == "fabric")  return Provider::Fabric;
    if (type_str == "paper")   return Provider::Paper;

    throw std::runtime_error("Unknown provider type: " + type_str);
}


int main() {
    webview::webview main_window(true, nullptr);
    main_window.set_title("Beacon");
    main_window.set_size(1280, 720, WEBVIEW_HINT_NONE);

    const std::string html_content(reinterpret_cast<const char*>(INDEX_HTML_BYTES), INDEX_HTML_SIZE);

    main_window.set_html(html_content);


    try {
        main_window.bind("getServers", [](const std::string& req) -> std::string {
            nlohmann::json servers = get_servers().dump();

            return servers;
        });

        main_window.bind("addServer", [](const std::string& req) -> std::string{

             auto data = jlib::json::parse(req);
             auto server_data = jlib::json::parse(data[0].get<std::string>());
             std::cout << server_data << std::endl;

            std::string id    = server_data["id"];
            std::string name    = server_data["name"];
            std::string version = server_data["version"];
            std::int32_t memory          = server_data["memory"];
            std::int32_t port            = server_data["port"];
            bool online         = server_data["online"];

            std::string type_str = server_data["type"];



            Provider provider = determine_provider(type_str);


            add_server(id, name, provider, version, memory,port, online);

            return "Ok";
        });
    }catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
    std::string i = "1234";
    std::string& t = i;
    start_server(t);
    main_window.run();
    return 0;
}
