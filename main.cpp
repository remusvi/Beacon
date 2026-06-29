#include <iostream>
#include <string>
#include "webview.h"
#include "index_html.h"
#include "core/servers/servers.h"
#include "./core/servers/serverEntity.h"

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

            auto server_data = jlib::json::parse(req);

            std::string id    = server_data["uuid"];
            std::string name    = server_data["name"];
            std::string version = server_data["version"];
            int memory          = server_data["memory"];
            int port            = server_data["port"];
            bool online         = server_data["online"];

            std::string type_str = server_data["type"];
            Provider provider = (type_str == "vanilla") ? Provider::Vanilla : Provider::Forge;

            std::cout << id << std::endl;
            add_server(id, name, provider, version, memory, port, online);

            return "Ok";
        });
    }catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
    main_window.run();
    return 0;
}