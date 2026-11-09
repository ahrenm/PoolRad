#pragma once
#include "gbengine/party/party.hpp"
#include <random>

namespace GBEngine {
struct CreationChoices {std::uint8_t race=7,sex=0,character_class=2,alignment=0;};
struct CharacterDraft {Character character;CreationChoices choices;std::mt19937 random;};
// Owned source tables and candidate RNG. Choosing/cancelling a draft never
// changes a live party, campaign generator or original character sidecar.
class CharacterCreationService {
    Bytes module_,overlay_;
    std::uint8_t byte(unsigned) const;
    std::uint16_t word(unsigned) const;
public:
    static CharacterCreationService load(const IFileSystem&);
    std::vector<std::uint8_t> classes(std::uint8_t race) const;
    std::vector<std::uint8_t> alignments(std::uint8_t character_class) const;
    CharacterDraft begin(CreationChoices,const std::mt19937&) const;
    void reroll(CharacterDraft&) const;
    Character finish(const CharacterDraft&,std::string_view name,std::uint8_t head=1,std::uint8_t body=1) const;
    Party assemble(const IFileSystem&,std::span<const Character>) const;
    void append(Party&,const Character&) const;
    void remove(Party&,std::size_t) const;
    void adjust(Party&,std::size_t,unsigned field,bool increase) const;
    void finish_modification(Party&,std::size_t,const Character& baseline,bool keep) const;
    // Distinct character-library artifact, never a PortSave or a DOS export.
    Bytes encode_character(const IFileSystem&,const Character&) const;
    Character decode_character(const IFileSystem&,std::span<const std::uint8_t>) const;
};
}
