#include "../utils/utils.h"
#include <string>
#include <filesystem>

// Explicitly include v1 process headers for Boost 1.87+
#include <boost/process/v1.hpp>

namespace fs = std::filesystem;
namespace bp = boost::process::v1; // Note: point namespace alias directly to v1

void start_server(std::string& server_id) {
    std::string resource_path = get_resource_path();
    fs::path resource_dir(resource_path);
    fs::path server_dir = resource_dir/ "servers"/ server_id;
    fs::path binary_dir = resource_dir / "servers" / server_id / "server.jar";

    // Spawn server.jar subprocess
    bp::child c(
            bp::search_path("java"),
            "-jar",
            "/Users/adafaralph/dev/Beacon2.0/Resources/servers/56eb1aac-eb5c-497e-8843-355264cbdedd/server.jar",
            bp::start_dir("/Users/adafaralph/dev/Beacon2.0/Resources/servers/56eb1aac-eb5c-497e-8843-355264cbdedd")
    );

    c.wait();
}
