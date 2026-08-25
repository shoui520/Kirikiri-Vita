#include "krkrvita/psb.hpp"
#include "krkrvita/read_all.hpp"
#include "krkrvita/xp3_archive.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

std::vector<std::uint8_t> minimal_psb() {
    std::vector<std::uint8_t> bytes(61, 0);
    bytes[0] = 'P'; bytes[1] = 'S'; bytes[2] = 'B';
    bytes[4] = 2;
    write_u32(bytes, 8, 40);
    write_u32(bytes, 12, 40);
    write_u32(bytes, 16, 49);
    write_u32(bytes, 20, 52);
    write_u32(bytes, 24, 52);
    write_u32(bytes, 28, 55);
    write_u32(bytes, 32, 58);
    write_u32(bytes, 36, 59);
    for (const auto offset : {40u, 43u, 46u, 49u, 52u, 55u}) {
        bytes[offset] = 0x0d;
        bytes[offset + 1] = 0;
        bytes[offset + 2] = 0x0c;
    }
    bytes[59] = 0x05;
    bytes[60] = 42;
    return bytes;
}

struct TreeStats {
    std::size_t nodes = 0;
    std::size_t arrays = 0;
    std::size_t objects = 0;
    std::size_t binaries = 0;
};

struct ShortReader {
    const std::vector<std::uint8_t>& bytes;
    std::uint32_t max_per_read;
    std::size_t position = 0;

    std::int64_t Read(std::uint8_t* output, std::uint32_t amount) {
        const auto count = std::min<std::size_t>(
            std::min(amount, max_per_read), bytes.size() - position);
        std::copy(bytes.begin() + position, bytes.begin() + position + count,
                  output);
        position += count;
        return static_cast<std::int64_t>(count);
    }
};

void test_short_read_join() {
    const std::vector<std::uint8_t> bytes = {0, 1, 2, 3, 4, 5, 6};
    std::vector<std::uint8_t> output(bytes.size(), 0xff);
    ShortReader reader{bytes, 2};
    require(krkrvita::read_all_stream(reader, output.data(),
                                       static_cast<std::uint32_t>(output.size())) ==
                output.size(),
            "short stream was not joined");
    require(output == bytes, "short stream join changed bytes");
}

void count_tree(const krkrvita::PsbValuePtr& value, TreeStats& stats) {
    require(static_cast<bool>(value), "PSB tree contains a null node");
    ++stats.nodes;
    if (value->type == krkrvita::PsbValue::Type::array) {
        ++stats.arrays;
        for (const auto& child : value->array) count_tree(child, stats);
    } else if (value->type == krkrvita::PsbValue::Type::object) {
        ++stats.objects;
        for (const auto& member : value->object) count_tree(member.second, stats);
    } else if (value->type == krkrvita::PsbValue::Type::binary) {
        ++stats.binaries;
        require(static_cast<bool>(value->binary), "PSB binary has no storage");
    }
}

std::vector<std::uint8_t> read_entry(const std::filesystem::path& archive_path,
                                     std::string_view name,
                                     std::size_t limit) {
    std::string error;
    const auto archive = krkrvita::Xp3Archive::open(archive_path, &error);
    require(archive.has_value(), "cannot open XP3: " + error);
    const auto* entry = archive->find(name);
    require(entry != nullptr, "XP3 entry is missing: " + std::string(name));
    auto bytes = archive->read(*entry, limit, nullptr, &error);
    require(bytes.has_value(), "cannot read XP3 entry: " + error);
    return std::move(*bytes);
}

void test_external_psb_corpus() {
    const char* corpus = std::getenv("KRKRVITA_TEST_PSB_GAME_DIR");
    if (!corpus || !*corpus) return;
    const std::filesystem::path game = corpus;
    if (!std::filesystem::is_directory(game)) return;

    std::string error;
    auto drop = read_entry(game / "video.xp3", "drop.psb", 1024u * 1024u);
    krkrvita::PsbDocument document;
    require(krkrvita::parse_psb(drop.data(), drop.size(), document, &error),
            "drop.psb: " + error);
    require(document.version == 2 && !document.mdf_compressed,
            "drop.psb envelope changed");
    TreeStats drop_stats;
    count_tree(document.root, drop_stats);
    require(drop_stats.nodes > 100 && drop_stats.objects > 10 &&
                drop_stats.binaries > 0,
            "drop.psb object graph is unexpectedly incomplete");

    auto database = read_entry(game / "scenario.xp3", "scene.sdb",
                               8u * 1024u * 1024u);
    sqlite3* handle = nullptr;
    require(sqlite3_open(":memory:", &handle) == SQLITE_OK,
            "cannot create SQLite PSB fixture database");
    const auto close_database = [&] { sqlite3_close(handle); };
    if (sqlite3_deserialize(handle, "main", database.data(),
                            static_cast<sqlite3_int64>(database.size()),
                            static_cast<sqlite3_int64>(database.size()),
                            SQLITE_DESERIALIZE_READONLY) != SQLITE_OK) {
        close_database();
        throw std::runtime_error("cannot deserialize scene.sdb");
    }
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(handle,
            "select id,data from scene where typeof(data)='blob' order by id",
            -1, &statement, nullptr) != SQLITE_OK) {
        sqlite3_finalize(statement);
        close_database();
        throw std::runtime_error("cannot read scene PSB blob");
    }
    int row_count = 0;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* blob = static_cast<const std::uint8_t*>(
            sqlite3_column_blob(statement, 1));
        const auto blob_size = sqlite3_column_bytes(statement, 1);
        require(blob && blob_size > 8, "scene PSB blob is empty");
        document = {};
        const bool parsed = krkrvita::parse_psb(
            blob, static_cast<std::size_t>(blob_size), document, &error);
        require(parsed, "scene MDF PSB row " +
                            std::to_string(sqlite3_column_int(statement, 0)) +
                            ": " + error);
        require(document.version == 2 && document.mdf_compressed,
                "scene database PSB envelope changed");
        ++row_count;
    }
    sqlite3_finalize(statement);
    close_database();
    require(row_count >= 400, "scene database PSB coverage is unexpectedly small");

    // scene.sdb only holds the per-scene "data" column. The blobs the game
    // actually feeds to PSBFile come from scenedata.sdb's text.state, which
    // start.ks(9) [scenestart] reaches through KAGEnvPlayer.restore(). Those
    // are encoded with no embedded resources, so offsetChunkData equals the
    // document size -- a case scene.sdb never exercises.
    auto text_database = read_entry(game / "scenario.xp3", "scenedata.sdb",
                                    128u * 1024u * 1024u);
    handle = nullptr;
    require(sqlite3_open(":memory:", &handle) == SQLITE_OK,
            "cannot create SQLite scenedata fixture database");
    if (sqlite3_deserialize(handle, "main", text_database.data(),
                            static_cast<sqlite3_int64>(text_database.size()),
                            static_cast<sqlite3_int64>(text_database.size()),
                            SQLITE_DESERIALIZE_READONLY) != SQLITE_OK) {
        close_database();
        throw std::runtime_error("cannot deserialize scenedata.sdb");
    }
    statement = nullptr;
    if (sqlite3_prepare_v2(handle,
            "select scene,idx,state from text where state is not null",
            -1, &statement, nullptr) != SQLITE_OK) {
        sqlite3_finalize(statement);
        close_database();
        throw std::runtime_error("cannot read scenedata state blob");
    }
    int state_count = 0;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* blob = static_cast<const std::uint8_t*>(
            sqlite3_column_blob(statement, 2));
        const auto blob_size = sqlite3_column_bytes(statement, 2);
        require(blob && blob_size > 8, "scene state blob is empty");
        document = {};
        const bool parsed = krkrvita::parse_psb(
            blob, static_cast<std::size_t>(blob_size), document, &error);
        if (!parsed) {
            sqlite3_finalize(statement);
            close_database();
            throw std::runtime_error(
                "scenedata state row scene=" +
                std::to_string(sqlite3_column_int(statement, 0)) + " idx=" +
                std::to_string(sqlite3_column_int(statement, 1)) + ": " + error);
        }
        require(document.root &&
                    document.root->type == krkrvita::PsbValue::Type::object,
                "scene state PSB root is not an object");
        require(!document.root->object.empty(),
                "scene state PSB object has no members");
        ++state_count;
    }
    sqlite3_finalize(statement);
    close_database();
    require(state_count > 50000,
            "scenedata state coverage is unexpectedly small: " +
                std::to_string(state_count));
}

} // namespace

int main() {
    try {
        auto bytes = minimal_psb();
        krkrvita::PsbDocument document;
        std::string error;
        require(krkrvita::parse_psb(bytes.data(), bytes.size(), document, &error),
                error);
        require(document.version == 2 && document.root &&
                    document.root->type == krkrvita::PsbValue::Type::integer &&
                    document.root->integer == 42,
                "minimal PSB value changed");

        for (const auto size : {0u, 3u, 39u, 40u, 52u, 59u, 60u}) {
            require(!krkrvita::parse_psb(bytes.data(), size, document, &error),
                    "truncated PSB was accepted at " + std::to_string(size));
        }
        auto bad_offset = bytes;
        write_u32(bad_offset, 36, 0xffffffffu);
        require(!krkrvita::parse_psb(bad_offset.data(), bad_offset.size(),
                                     document, &error),
                "out-of-range PSB entry offset was accepted");

        // A table offset must address a real byte, because a table starts with
        // a type byte. One past the end is still rejected.
        for (const auto table_field : {12u, 16u, 24u, 28u, 36u}) {
            auto past_end = bytes;
            write_u32(past_end, table_field,
                      static_cast<std::uint32_t>(past_end.size()));
            require(!krkrvita::parse_psb(past_end.data(), past_end.size(),
                                         document, &error),
                    "PSB table offset at the end of the document was accepted "
                    "at field " + std::to_string(table_field));
        }

        // Payload bases are different: an empty region legitimately begins one
        // past the last byte. Rejecting that broke every external scene
        // state, so pin both the accepted and the rejected side.
        for (const auto data_field : {20u, 32u}) {
            auto at_end = bytes;
            write_u32(at_end, data_field,
                      static_cast<std::uint32_t>(at_end.size()));
            require(krkrvita::parse_psb(at_end.data(), at_end.size(), document,
                                        &error),
                    "empty PSB payload region was rejected at field " +
                        std::to_string(data_field) + ": " + error);
            auto beyond_end = bytes;
            write_u32(beyond_end, data_field,
                      static_cast<std::uint32_t>(beyond_end.size() + 1));
            require(!krkrvita::parse_psb(beyond_end.data(), beyond_end.size(),
                                         document, &error),
                    "PSB payload base past the document was accepted at field " +
                        std::to_string(data_field));
        }

        test_short_read_join();
        test_external_psb_corpus();
        std::cout << "PSB parser contracts passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
