#include "krkrvita/bubble.hpp"

#include "krkrvita/pe_resources.hpp"
#include "krkrvita/png.hpp"
#include "krkrvita/sfo.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace krkrvita {

std::string bubble_title_id(const GameDescriptor& game) {
    std::string id = "K" + game.fingerprint.substr(0, 8);
    std::transform(id.begin(), id.end(), id.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return id;
}

bool stage_bubble(const BubbleSpec& spec,
                  const std::filesystem::path& template_root,
                  const std::filesystem::path& staging_root,
                  std::string* error) {
    try {
        if (spec.title_id.size() != 9 || spec.game_id.empty()) {
            throw std::runtime_error("invalid bubble title or game ID");
        }
        std::error_code ec;
        std::filesystem::remove_all(staging_root, ec);
        std::filesystem::create_directories(staging_root / "sce_sys/livearea/contents");
        std::filesystem::copy_file(template_root / "eboot.bin", staging_root / "eboot.bin",
                                   std::filesystem::copy_options::overwrite_existing);
        for (const auto* name : {"template.xml", "bg0.png", "startup.png"}) {
            std::filesystem::copy_file(template_root / "sce_sys/livearea/contents" / name,
                staging_root / "sce_sys/livearea/contents" / name,
                std::filesystem::copy_options::overwrite_existing);
        }
        const auto package_head = template_root / "sce_sys/package/head.bin";
        if (std::filesystem::is_regular_file(package_head)) {
            std::filesystem::create_directories(staging_root / "sce_sys/package");
            std::filesystem::copy_file(package_head, staging_root / "sce_sys/package/head.bin",
                                       std::filesystem::copy_options::overwrite_existing);
        }

        PeResources pe(spec.executable);
        const auto embedded = pe.largest_icon();
        if (!embedded) throw std::runtime_error("Windows executable contains no icon");
        std::string image_error;
        const auto decoded = decode_icon(*embedded, &image_error);
        if (!decoded) throw std::runtime_error(image_error);
        if (!write_vita_indexed_png(staging_root / "sce_sys/icon0.png",
                                    fit_icon(*decoded, 128, 128), &image_error)) {
            throw std::runtime_error(image_error);
        }
        std::string sfo_error;
        if (!ParamSfo::bubble(spec.title, spec.title_id)
                 .write(staging_root / "sce_sys/param.sfo", &sfo_error)) {
            throw std::runtime_error(sfo_error);
        }
        std::ofstream game_id(staging_root / "game.id", std::ios::trunc);
        game_id << spec.game_id << '\n';
        if (!game_id) throw std::runtime_error("cannot write bubble game ID");
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

} // namespace krkrvita

