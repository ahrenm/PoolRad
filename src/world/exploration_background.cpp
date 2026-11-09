#include "gbengine/world/exploration_background.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
namespace GBEngine {
std::uint8_t ExplorationBackground::floor_color(std::uint8_t selector) {
    // Native4C8E:012F returns a defined value for every byte.
    return selector==11?8:selector==6?1:6;
}
std::optional<ExplorationBackground> ExplorationBackground::rebuild(unsigned disk,std::uint8_t record,
        const EclStateBanks& banks,std::span<const std::uint8_t> script) {
    if(disk==4 && record==10) {
        // Graveyard source profile: original palette setup, preservation/exit,
        // paired wall loader and first-visit initializer. No generic getters.
        const auto exact=[&](std::size_t at,std::size_t size,const char* hash) {
            return script.size()>=at+size && sha256(script.subspan(at,size))==hash;
        };
        if(!exact(0x9914-0x9900,0x9947-0x9914,"ac0a33b4c4e7e793a3cfb6d07c33dfb54987ed51bd5f814b347fbc48e801e767") ||
           !exact(0x9a72-0x9900,0x9b93-0x9a72,"a0e9565f43e6e518b5efbda6a62122c651cb47c97688998326e0959f73cd53a2"))
            throw DecodeError("Graveyard background source helpers are unavailable or modified");
        ExplorationBackground result;
        result.sky=banks.raw_value(0x49fd);result.alternate_sky=banks.raw_value(0x49fe);
        if(result.sky>=16 || result.alternate_sky>=16)throw DecodeError("Graveyard sky selector outside native palette table");
        // Entry4 sets resident6F69=8 and resident6F68=11. Saved4A16 is
        // the pre-entry floor, restored only on departure, never the live floor.
        result.horizon=0;result.lower_horizon=8;result.floor=floor_color(11);
        return result;
    }
    if(disk==8 && (record==13 || record==16 || record==27)) {
        // ECL8/27 also owns wilderness region4. Only its explicit cave state
        // uses the interior profile; an outdoor retained map must not gain it.
        if(record==27 && banks.raw_value(0x4a9e)!=255)return std::nullopt;
        // Castle9914 sets the outdoor/day selector and alternate underground
        // selector. Native4C8E:016D chooses between them using the RAW event
        // high bit, including GEO30's tunnels. No clock/helper replay on load.
        constexpr std::array<std::uint8_t,33> helper{
            0x09,0x00,0x08,0x01,0xfe,0x49,0x09,0x00,0x0b,0x01,0x82,0x6e,0x03,0x01,0xc9,0x49,
            0x00,0x0d,0x19,0x09,0x00,0x08,0x01,0x82,0x6e,0x09,0x01,0x82,0x6e,0x01,0xfd,0x49,0x13};
        // The large (wyvern) and small (kobold) caves share ECL8/13.
        // Its entry4 loads walls/GEO13 and selects49FE=8 before EXIT.
        constexpr std::array<std::uint8_t,27> cave_entry{
            0x37,0x00,0x11,0x00,0x12,0x00,0xff,0x21,0x00,0x0d,0x00,0x02,0x00,0xff,
            0x09,0x00,0x01,0x01,0xfb,0x49,0x09,0x00,0x08,0x01,0xfe,0x49,0x00};
        // Native cave restore AC86..ACA6 sets interior flags, loads walls17
        // and GEO27, selects49FE=8, then exits. No resident band overrides.
        constexpr std::array<std::uint8_t,33> wilderness_cave_entry{
            0x09,0x00,0x01,0x01,0xe6,0x49,0x09,0x00,0xff,0x01,0xfb,0x49,
            0x37,0x00,0x11,0x00,0x11,0x00,0x11,0x21,0x00,0x1b,0x00,0x02,0x00,0xff,
            0x09,0x00,0x08,0x01,0xfe,0x49,0x00};
        const auto at=record==27?0xac86-0x9900:0x9914-0x9900;
        const auto expected=record==27?std::span<const std::uint8_t>(wilderness_cave_entry):
            record==13?std::span<const std::uint8_t>(cave_entry):std::span<const std::uint8_t>(helper);
        if(script.size()<at+expected.size() || !std::equal(expected.begin(),expected.end(),script.begin()+at))
            throw DecodeError(record!=16?"Cave background source entry is unavailable or modified":"Castle background source helper is unavailable or modified");
        ExplorationBackground result;
        result.sky=banks.raw_value(0x49fd);result.alternate_sky=banks.raw_value(0x49fe);
        if(result.sky>=16 || result.alternate_sky>=16)throw DecodeError("Cave/castle sky selector outside native palette table");
        // 27A9:01B6 resets resident6F68=41/6F69=9. These scripts never override
        // either register. Native00C7/00FB/012F map these to15/7/6.
        result.horizon=15;result.lower_horizon=7;result.floor=floor_color(0x41);
        return result;
    }
    if(disk!=8 || record!=29)return std::nullopt;
    // Exact Kuto AF30 helper: its writes have no independent persistent input.
    // Reject modified/unknown helpers instead of inventing cache reconstruction.
    constexpr std::array<std::uint8_t,65> helper{
        0x03,0x01,0xc9,0x49,0x00,0x0e,0x1b,0x09,0x00,0x08,0x01,0xfd,0x49,0x18,0x09,0x00,
        0x0b,0x01,0xfd,0x49,0x03,0x01,0x10,0x4a,0x00,0x01,0x17,0x09,0x00,0x0a,0x01,0xfe,
        0x49,0x17,0x09,0x00,0x41,0x01,0x5f,0xc0,0x16,0x09,0x00,0x08,0x01,0x5f,0xc0,0x16,
        0x09,0x00,0x08,0x01,0xfe,0x49,0x2a,0x01,0xed,0xaf,0x01,0xdb,0x4a,0x01,0x59,0xc0,0x13};
    constexpr auto at=0xaf30-0x9900,table=0xafed-0x9900;
    if(script.size()<table+4 || !std::equal(helper.begin(),helper.end(),script.begin()+at))
        throw DecodeError("Kuto background source helper is unavailable or modified");
    const auto index=static_cast<std::uint8_t>(banks.raw_value(0x4adb));
    if(index>=4)throw DecodeError("Kuto background table index is outside the recovered source table");
    ExplorationBackground result;
    result.sky=banks.raw_value(0x49fd);result.alternate_sky=banks.raw_value(0x49fe);
    if(result.sky>=16 || result.alternate_sky>=16)throw DecodeError("Kuto sky selector outside native palette table");
    result.lower_horizon=banks.raw_value(0x4a10)==1?8:7;
    result.floor=floor_color(script[table+index]);return result;
}
}
