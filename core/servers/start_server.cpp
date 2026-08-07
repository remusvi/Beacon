#include "../utils/utils.h"
#include <string>
#include <filesystem>
#include <boost/process.hpp>

namespace fs = std::filesystem;
namespace bp = boost::process;

void start_server(int &server_id){
    //find server first
    std::string resource_path = get_resource_path();
    std::string id_string = std::to_string(server_id);
    fs::path resource_dir(resource_path);
    fs::path binary_dir = resource_dir / "servers"/ id_string / "server.jar";

    //spawn server.jar subprocess
    //



}
