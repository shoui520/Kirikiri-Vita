#include "krkrvita/bubble.hpp"
#include "krkrvita/filter_heuristic.hpp"
#include "krkrvita/game.hpp"
#include "krkrvita/patch_manifest.hpp"
#include "krkrvita/patch_repository.hpp"
#include "krkrvita/sfo.hpp"
#include "krkrvita/sha256.hpp"
#include "krkrvita/storage.hpp"
#include "krkrvita/text_codec.hpp"
#include "krkrvita/xp3_filter_vm.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using namespace krkrvita;

void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

void test_sha256() {
    Sha256 hash;
    hash.update("abc");
    check(Sha256::hex(hash.finish()) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 vector failed");
}

void test_normalize() {
    check(normalize_game_name(" ＡＢＣ／色情・教団！ ") == "abc色情教団",
          "game title normalization failed");
}

void test_vita_title_id() {
    GameDescriptor game;
    game.fingerprint = "44bb539bf9510882";
    check(bubble_title_id(game) == "KRVG27323", "legal Vita title ID derivation failed");
    check(is_vita_title_id("KRVG27323"), "legal Vita title ID rejected");
    check(!is_vita_title_id("K44BB539"), "hex Vita title ID accepted");
    check(!is_vita_title_id("A-K44BB53"), "punctuated Vita title ID accepted");
}

void test_manifest_and_resolver() {
    constexpr auto source = R"JS(
      var all_data = [
        [1520434816, "ORCSOFT／DWARFSOFT", "色情教団", "色情教団",
          ["ORCSOFT／DWARFSOFT/色情教団/patch.tjs",
           "ORCSOFT／DWARFSOFT/色情教団/xp3filter.tjs"]],
        [1, "Other", "Different Game", "Different Game", ["Other/xp3filter.tjs"]]
      ];
    )JS";
    const auto manifest = PatchManifest::parse(source);
    check(manifest.entries().size() == 2, "manifest entry count failed");
    GameDescriptor game;
    game.directory_name = "色情教団";
    game.display_name = game.directory_name;
    game.executable_stem = "sikijokyodan";
    const auto resolution = PatchResolver::resolve(game, manifest);
    check(resolution.automatic, "sample patch was not selected automatically");
    check(resolution.best() && resolution.best()->entry->brand == "ORCSOFT／DWARFSOFT",
          "wrong patch selected");
    check(is_safe_patch_path("Brand/Game/xp3filter.tjs"), "safe patch rejected");
    check(!is_safe_patch_path("../xp3filter.tjs"), "traversal patch accepted");
    check(is_safe_patch_path("../patch/old_core_patch/Override2.tjs"),
          "shared compatibility patch rejected");
    check(is_safe_patch_path(
              "https://github.com/zeas2/Kirikiroid2_patch/releases/download/tag/patch2.xp3"),
          "trusted release XP3 rejected");
    check(!is_safe_patch_path("https://example.com/patch2.xp3"),
          "untrusted patch URL accepted");
    check(!is_safe_patch_path("Brand/tool.exe"), "executable patch accepted");
}

class MemoryHttpClient final : public HttpClient {
public:
    std::unordered_map<std::string, std::vector<std::uint8_t>> responses;
    int requests = 0;

    HttpResult get(std::string_view url) override {
        ++requests;
        const auto found = responses.find(std::string(url));
        if (found == responses.end()) return {404, {}, "not found"};
        return {200, found->second, {}};
    }
};

void test_patch_cache_integrity() {
    const auto root = std::filesystem::temp_directory_path() / "krkrvita-patch-cache-test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    PatchEntry entry;
    entry.files = {"Brand/Game/xp3filter.tjs"};
    const std::string script =
        "Storages.setXP3ArchiveExtractionFilter(function(h,o,b,l){b.xor(0,l,h);});";
    MemoryHttpClient http;
    http.responses[patch_file_url(entry.files.front())] =
        std::vector<std::uint8_t>(script.begin(), script.end());
    PatchRepository repository(root);
    auto fetched = repository.fetch_bundle(http, entry);
    check(fetched.size() == 1 && http.requests == 1, "patch was not fetched");
    std::ofstream(fetched.front().cache_path, std::ios::binary | std::ios::trunc)
        << "corrupt";
    fetched = repository.fetch_bundle(http, entry);
    check(http.requests == 2, "corrupt cached patch was trusted");
    std::ifstream repaired(fetched.front().cache_path, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(repaired)),
                               std::istreambuf_iterator<char>());
    check(contents == script, "corrupt patch cache was not repaired");
    std::filesystem::remove_all(root, ec);
}

void test_filter_detection() {
    const std::vector<std::uint8_t> png =
        {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 0};
    const std::vector<std::uint8_t> ogg = {'O', 'g', 'g', 'S', 0, 2, 0, 0, 0, 0};
    std::vector<FilterSample> samples = {
        {0x123456a5, 0, "image.png", png},
        {0xabcdef3c, 0, "voice.ogg", ogg},
    };
    FilterRule encrypt{FilterOperation::XorHash};
    for (auto& sample : samples) encrypt.apply(sample.hash, 0, sample.bytes);
    const auto detected = FilterHeuristic::detect(samples);
    check(detected.has_value(), "hash XOR filter not detected");
    check(detected->operation == FilterOperation::XorHash, "wrong filter detected");
    check(detected->to_tjs().find("b.xor(0,l,h)") != std::string::npos,
          "wrong filter script generated");
}

void test_yuri_filter_vm() {
    Xp3FilterVm vm;
    std::string error;
    check(vm.load("Storages.setXP3ArchiveExtractionFilter(function(h,o,b,l){b.xor(0,l,h);});",
                  &error), "Yuri TJS VM rejected hash-XOR filter");
    const std::uint32_t hash = 0x123456a5;
    std::vector<std::uint8_t> bytes = {0x89, 'P', 'N', 'G'};
    for (auto& byte : bytes) byte ^= static_cast<std::uint8_t>(hash);
    check(vm.decode(hash, 0, bytes, "test.png", &error), "Yuri filter execution failed");
    check(bytes == std::vector<std::uint8_t>({0x89, 'P', 'N', 'G'}),
          "Yuri filter produced the wrong bytes");
}

void test_sfo() {
    const auto bytes = ParamSfo::bubble("Test Game", "KRVG12345").encode();
    check(bytes.size() > 128, "SFO unexpectedly small");
    check(bytes[0] == 0 && bytes[1] == 'P' && bytes[2] == 'S' && bytes[3] == 'F',
          "SFO magic failed");
    check(std::search(bytes.begin(), bytes.end(), "KRVG12345", "KRVG12345" + 9) != bytes.end(),
          "SFO title ID missing");
}

void test_text_codec() {
    // Mode 1 swaps adjacent bits of every UTF-16 code unit.
    std::vector<std::uint8_t> encoded = {0xfe, 0xfe, 1, 0xff, 0xfe};
    for (const std::uint16_t plain : {std::uint16_t('A'), std::uint16_t(0x3042)}) {
        const auto cipher = static_cast<std::uint16_t>(((plain & 0xaaaa) >> 1) |
                                                       ((plain & 0x5555) << 1));
        encoded.push_back(static_cast<std::uint8_t>(cipher));
        encoded.push_back(static_cast<std::uint8_t>(cipher >> 8));
    }
    std::string decoded;
    check(decode_kirikiri_text(encoded, decoded), "mode-1 text decode failed");
    check(decoded == "A\xe3\x81\x82", "mode-1 text decoded incorrectly");
}

void test_loose_storage() {
    const auto root = std::filesystem::temp_directory_path() / "krkrvita-storage-test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "game/system");
    std::filesystem::create_directories(root / "patch");
    {
        std::ofstream(root / "game/system/Test.tjs", std::ios::binary) << "game";
        std::ofstream(root / "game/loose.bin", std::ios::binary) << "data";
        std::ofstream(root / "patch/loose.bin", std::ios::binary) << "patch";
    }
    GameProfile profile;
    profile.game_id = "test";
    profile.game_path = root / "game";
    profile.patch_root = root / "patch";
    std::string error;
    auto storage = GameStorage::mount(profile, &error);
    check(storage.has_value(), "loose storage mount failed");
    storage->add_auto_path("system/");
    check(storage->exists("test.tjs"), "storage auto path failed");
    const auto script = storage->read_script("TEST.TJS", &error);
    check(script && *script == "game", "case-insensitive script read failed");
    const auto override = storage->read("loose.bin", 32, &error);
    check(override && std::string(override->begin(), override->end()) == "patch",
          "patch loose file did not override game file");
    check(!storage->read("../outside", 32, &error), "storage traversal was accepted");
    std::filesystem::remove_all(root, ec);
}

} // namespace

int main() {
    try {
        test_sha256();
        test_normalize();
        test_vita_title_id();
        test_manifest_and_resolver();
        test_patch_cache_integrity();
        test_filter_detection();
        test_yuri_filter_vm();
        test_sfo();
        test_text_codec();
        test_loose_storage();
        std::cout << "all tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "test failure: " << exception.what() << '\n';
        return 1;
    }
}
