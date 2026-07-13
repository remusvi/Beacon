#include <ostream>
#include <string_view>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <format>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include "webview.h"
#include "../servers/serverEntity.h"
#include <iostream>
#include <system_error>


std::error_code ec;
namespace fs = std::filesystem;

// Removed empty globals. Better to keep paths localized or passed cleanly.

std::string get_resource_path() {
    fs::path my_path = fs::current_path();
    //fs::path my_path(path);
#if defined(_WIN32) || defined(_WIN64)
    my_path /= "resources";
#else
    my_path /= "Resources";
#endif
    return my_path.string();
}

std::string ensure_jar_exists(const std::string& path, const std::string& version, Provider provider) {
    fs::path cache_dir(path);
        cache_dir /= "cache";

        if (!fs::exists(cache_dir)) {
            fs::create_directories(cache_dir);
        }

        std::string jar_name = std::format("server-{}.jar", version);
        fs::path target_file_path = cache_dir / jar_name;
        std::error_code ec;

        if (fs::is_symlink(target_file_path) || fs::status(target_file_path, ec).type() == fs::file_type::not_found) {
            if (fs::is_symlink(target_file_path)) {
                fs::remove(target_file_path);
            }
        }


        if (fs::exists(target_file_path, ec) && fs::file_size(target_file_path, ec) > 0) {
            return target_file_path.string();
        } else {

            fs::remove(target_file_path, ec);
        }

        std::string download_url = "";
        std::cout << "Provider is: " << provider << std::endl;
        switch(provider) {
            case Provider::Vanilla: {
                cpr::Response response = cpr::Get(
                    cpr::Url{"https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"},
                    cpr::Header{{"User-Agent", "Mozilla/5.0"}},
                    cpr::Timeout{10000}
                );

                if (response.status_code != 200) {
                    throw std::runtime_error("Failed to fetch version manifest from Mojang API");
                }

                nlohmann::json manifest = nlohmann::json::parse(response.text);
                std::string version_url = "";

                for (const auto& entry : manifest.value("versions", nlohmann::json::array())) {
                    if (entry.value("id", "") == version) {
                        version_url = entry.value("url", "");
                        break;
                    }
                }

                if (version_url.empty()) {
                    throw std::runtime_error("Vanilla version " + version + " not found in manifest");
                }

                cpr::Response meta_response = cpr::Get(
                    cpr::Url{version_url},
                    cpr::Header{{"User-Agent", "Mozilla/5.0"}},
                    cpr::Timeout{10000}
                );
                if (meta_response.status_code != 200) {
                    throw std::runtime_error("Failed to fetch version metadata from " + version_url);
                }


                try {
                    nlohmann::json meta = nlohmann::json::parse(meta_response.text);


                    if (meta.contains("downloads") && meta["downloads"].contains("server")) {
                        download_url = meta["downloads"]["server"].value("url", "");
                    }

                    if (download_url.empty()) {
                        throw std::runtime_error("No Vanilla server download link found in parsed metadata");
                    }

                } catch (const nlohmann::json::exception& e) {
                    std::cerr << "[JSON Debug] Parsing failed: " << e.what() << std::endl;
                    throw std::runtime_error("Internal JSON error while parsing version metadata: " + std::string(e.what()));
                }

                break;
            }
            default:
                throw std::runtime_error("Unsupported provider");
        }
            if (!download_url.empty()) {
                    std::cout << "Downloading from: " << download_url << std::endl;

                    std::ofstream download_file(target_file_path, std::ios::binary);
                    if (!download_file.is_open()) {
                        throw std::runtime_error("Failed to open local file stream for writing download: " + target_file_path.string());
                    }

                    cpr::Response download_res = cpr::Download(download_file, cpr::Url{download_url});

                    if (download_res.status_code != 200) {
                        download_file.close();
                        fs::remove(target_file_path, ec);
                        throw std::runtime_error("Failed downloading server JAR from Mojang. HTTP Status: " + std::to_string(download_res.status_code));
                    }
                } else {
                    throw std::runtime_error("Download process aborted: download URL was left empty.");
                }

                return target_file_path.string();
}
