#pragma once
#include "gbengine/combat/encounter.hpp"

namespace GBEngine {
struct SpecialCombatPreparation {
    EncounterState encounter;
    Party party;
    // The caller publishes this source word together with resident84D4 and
    // owned participants, before acknowledging the original CALL request.
    std::uint16_t vm_mode{};
};
class SpecialCombatService {
public:
    // Selected persistent actor only; creature selection needs its separately
    // owned native roster binding. No random draws or durable member changes.
    static SpecialCombatPreparation stage(const IFileSystem&,unsigned resource_disk,
        const Party&,const EncounterState&,std::size_t selected_party_index,bool surrogate);
};
}
