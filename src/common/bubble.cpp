#include "krkrvita/bubble.hpp"

#include "krkrvita/pe_resources.hpp"
#include "krkrvita/png.hpp"
#include "krkrvita/sfo.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace krkrvita {

std::string bubble_title_id(const GameDescriptor& game) {
    const auto seed = static_cast<unsigned long>(
        std::stoul(game.fingerprint.substr(0, 8), nullptr, 16));
    std::ostringstream id;
    id << "KRVG" << std::setfill('0') << std::setw(5) << (seed % 100000UL);
    return id.str();
}

bool is_vita_title_id(const std::string& title_id) {
    if (title_id.size() != 9) return false;
    return std::all_of(title_id.begin(), title_id.begin() + 4,
                       [](unsigned char value) { return value >= 'A' && value <= 'Z'; }) &&
           std::all_of(title_id.begin() + 4, title_id.end(),
                       [](unsigned char value) { return value >= '0' && value <= '9'; });
}

bool stage_bubble(const BubbleSpec& spec,
                  const std::filesystem::path& template_root,
                  const std::filesystem::path& staging_root,
                  std::string* error) {
    try {
        if (!is_vita_title_id(spec.title_id) || spec.game_id.empty()) {
            throw std::runtime_error("invalid bubble title or game ID");
        }
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
