#include "../utils/utils.h"
#include <string>
#include <filesystem>


#include <boost/process/v1.hpp>

namespace fs = std::filesystem;
namespace bp = boost::process::v1; // Note: point namespace alias directly to v1

void start_server( const std::string& server_id) {
    std::string resource_path = get_resource_path();
    fs::path resource_dir(resource_path);
    fs::path server_dir = resource_dir/ "servers"/ server_id;
    fs::path binary_dir = resource_dir / "servers" / server_id / "server.jar";

    // Spawn server.jar subprocess
    bp::child c(
            bp::search_path("java"),
            "-jar",
            binary_dir.string(),
            bp::start_dir(server_dir.string())
    );

    c.wait();
}
