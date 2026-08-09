#include "../utils/utils.h"
#include <string>
#include <filesystem>

// Force Boost.Process v1 API
#define BOOST_PROCESS_V1
#include <boost/process.hpp>

namespace fs = std::filesystem;
namespace bp = boost::process;

void start_server(int &server_id) {
    std::string resource_path = get_resource_path();
    std::string id_string = std::to_string(server_id);
    fs::path resource_dir(resource_path);
    fs::path binary_dir = resource_dir / "servers" / id_string / "server.jar";

    // Spawn server process using v1 API
    bp::child c(bp::search_path("java"), "-jar", binary_dir.string());

    c.wait();
}
