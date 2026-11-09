#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace GBEngine {
// Display metadata only: never a substitute for validating the selected campaign.
struct SaveSummary {
    unsigned disk{},script{};
    std::optional<unsigned> geometry;
    bool overland=false;
    std::array<std::uint16_t,7> clock{};
    std::string_view zone_title() const {
        if(overland) {
            if(disk==6 && script==25)return "Western Wilderness";
            if(disk==7 && script==26)return "Central Wilderness";
            if(disk==8 && script==27)return "Eastern Wilderness";
            return "Unknown area";
        }
        struct Zone {unsigned disk,script,geometry;std::string_view title;};
        // Game/cluebook titles. Shared scripts require the actual retained GEO,
        // rather than the VM's possibly stale geometry selector. Castle outer
        // quadrants intentionally share the user-approved broader title.
        static constexpr Zone zones[]{
            {3,0,0,"New Phlan"},{2,20,20,"Slums"},
            {4,21,21,"Sokal Keep"},{8,29,29,"Kuto's Well"},
            {8,29,32,"Kuto's Well Catacombs"},{2,15,15,"Mendor's Library"},
            {1,18,18,"Podol Plaza"},{4,2,2,"Cadorna Textile House"},
            {1,24,24,"Wealthy Area"},{1,24,31,"Temple of Bane"},
            {3,14,14,"Kovel Mansion"},{7,17,17,"Nomad Camp"},
            {8,13,13,"Kobold Caves"},{7,22,22,"Sorcerer's Island Level 1"},
            {7,23,23,"Sorcerer's Island Level 2"},
            {8,16,16,"Lizard Man Keep"},{8,16,30,"Lizard Man Keep Catacombs"},
            {6,1,1,"Buccaneers Base"},{6,28,28,"Outpost of Zhentil Keep"},
            {4,10,10,"Valhingen Graveyard"},{2,9,9,"Stojanow Gate"}
        };
        if(geometry)for(const auto& zone:zones)
            if(disk==zone.disk && script==zone.script && *geometry==zone.geometry)return zone.title;
        if(disk==5) {
            if(script==5)return "Valjevo Castle Maze";
            if(script==7)return "Valjevo Castle Inner Tower";
            if(script==3 || script==4 || script==6)return "Valjevo Castle";
        }
        return "Unknown area";
    }
    std::string elapsed_time() const {
        // Current fresh campaigns begin at06:00, day/month/year zero. The
        // original schema has no separate start timestamp. Never manufacture
        // negative elapsed time for legacy/imported clocks preceding that origin.
        if(clock[0]>=10 || clock[1]>=10 || clock[2]>=6 || clock[3]>=24 ||
           clock[4]>=30 || clock[5]>=12)return "---:--:--";
        const auto minutes=((((static_cast<std::uint64_t>(clock[6])*12+clock[5])*30+
            clock[4])*24+clock[3])*60+10*clock[2]+clock[1]);
        if(minutes<6*60)return "---:--:--";
        const auto elapsed=minutes-6*60;
        auto days=std::to_string(elapsed/(24*60));
        if(days.size()<3)days.insert(0,3-days.size(),'0');
        const auto hour=elapsed/60%24,minute=elapsed%60;
        return days+":"+(hour<10?"0":"")+std::to_string(hour)+":"+
            (minute<10?"0":"")+std::to_string(minute);
    }
};
}
