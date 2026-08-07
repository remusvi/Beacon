#include "../utils/utils.h"
#include <string>
#include <filesystem>
#include <boost/process.hpp>

namespace fs = std::filesystem;
namespace bp = bost::process;

void start_server(int &server_id){
    //find server first
    std::string resource_path = get_resource_path();
    fs::path resource_dir(resource_path);
    fs::path binary_dir = resource_dir / "servers"/ server_id / "server.jar";

    //spawn server.jar subprocess



}
