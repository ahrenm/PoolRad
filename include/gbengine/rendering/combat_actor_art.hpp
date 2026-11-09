#pragma once
#include "gbengine/combat/deployment.hpp"
#include "gbengine/combat/encounter.hpp"

namespace GBEngine {
enum class CombatIconKind { portrait_component,encounter,effect };
// Native EGA masked images use raw color0 as transparency. Source color13
// is changed only after constructing that mask, so opaque black survives.
Image decode_combat_icon(std::span<const std::uint8_t>,CombatIconKind);
struct CombatIconSource {
    std::string archive;
    std::uint8_t record{};
    bool operator==(const CombatIconSource&) const=default;
};
struct CombatActorArtEntry {
    CombatActorIdentity actor;
    std::vector<CombatIconSource> sources;
    // First index is the source pose (record or record+128), second mirror.
    std::array<std::array<Image,2>,2> frames;
};
class CombatActorArt {
    std::vector<CombatActorArtEntry> entries_;
    std::array<Image,2> cursor_;
public:
    static CombatActorArt load(const IFileSystem&,const DeploymentState&,const EncounterState&);
    static CombatActorArt load_character(const IFileSystem&,const Character&,CombatActorIdentity={CombatActorOrigin::party,0});
    const Image& image(CombatActorIdentity,unsigned pose,unsigned facing) const;
    const Image& cursor(unsigned pose=0) const;
    const std::vector<CombatActorArtEntry>& entries() const {return entries_;}
    std::size_t size() const {return entries_.size();}
};
}
