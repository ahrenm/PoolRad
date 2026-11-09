#pragma once
#include "gbengine/platform/hal.hpp"
#include <string>
#include <stdexcept>

namespace GBEngine {
class DecodeError : public std::runtime_error { using std::runtime_error::runtime_error; };
std::uint16_t read_u16(std::span<const std::uint8_t>, std::size_t);
std::uint32_t read_u32(std::span<const std::uint8_t>, std::size_t);
Bytes decompress_dax(std::span<const std::uint8_t>, std::size_t expected_size);
// Decode the selected START.EXE's EXEPACK data without executing its code.
Bytes decode_start_data(std::span<const std::uint8_t>);
struct DaxEntry {
    std::uint8_t id;
    std::uint32_t offset;
    std::uint16_t raw_size, compressed_size;
    Bytes data;
};
class DaxArchive {
public:
    std::vector<DaxEntry> entries;
    explicit DaxArchive(std::span<const std::uint8_t>);
};
enum class AssetKind { ecl, picture, combat_picture, sprite, geometry, wall, other };
AssetKind classify_asset(std::string_view filename);
struct Image { int width, height, x, y; std::vector<std::uint32_t> pixels; };
std::vector<Image> decode_ega(std::span<const std::uint8_t>, std::string_view filename);
// Packed six-bit ECL string payload, without its type and length bytes.
std::string decode_ecl_text(std::span<const std::uint8_t>);
struct EclEntryPoints { std::array<std::uint16_t, 5> addresses; };
EclEntryPoints inspect_ecl_header(std::span<const std::uint8_t>);
enum class ScriptRegion { area1, area2, area3, script, special };
struct ScriptLocation { ScriptRegion region; std::uint16_t byte_offset; std::uint8_t storage_width; };
// Observed in GAME.OVR segment 7, code offset 0x06fb, with storage arithmetic
// corroborated by its read/write handlers at 0x0e0c and 0x0c77.
ScriptLocation locate_script_value(std::uint16_t address);

}
