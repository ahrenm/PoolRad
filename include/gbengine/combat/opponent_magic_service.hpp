#pragma once
#include "gbengine/combat/casting_service.hpp"
#include "gbengine/combat/magic_service.hpp"
#include "gbengine/combat/targeting_service.hpp"

namespace GBEngine {
struct OpponentMagicContext {
    // area1 native+1CA = WORD VM49E5. This is not resident DS49E5.
    bool casting_blocked=false;
    // Native transient6E30 permits ordinary party members to choose spells;
    // high-bit morale/NPC84 actors do not require it.
    bool automatic_party_casting=false;
};
struct OpponentMagicTargets {
    // Empty means genuine automatic targeting failure. The caller follows
    // source Cancel Y/resource/Quit rather than retrying selection or RNG.
    std::optional<CombatMagicIntent> intent;
    // Last native extA/C link, including a rejected final candidate. The
    // driver owns its stable identity independently of tactical scalar bytes.
    std::optional<CombatActorIdentity> retained_target;
    std::optional<CombatPosition> cursor;
};
class OpponentMagicService {
    CombatSpatialService spatial_;
    CombatTargetingService targeting_;
    CombatAttackService attacks_;
    std::array<std::int8_t,9> near_x_{},near_y_{};
    std::array<std::uint8_t,4> helpless_{};
    std::array<bool,256> cure_body_status_{};
    bool loaded_=false;
    static std::size_t find(const DeploymentState&,CombatActorIdentity);
    std::vector<CombatTarget> visible_at(const DeploymentState&,CombatPosition,unsigned) const;
    std::vector<CombatTarget> opposing(const DeploymentState&,CombatActorIdentity,unsigned) const;
    std::optional<CombatActorIdentity> cure_target(const DeploymentState&,CombatActorIdentity) const;
    bool friendly_fire(const DeploymentState&,CombatActorIdentity,CombatPosition,unsigned) const;
    bool eligible(const DeploymentState&,CombatActorIdentity,std::uint8_t,unsigned,const SpellCatalog&,bool item) const;
    std::optional<CombatActorIdentity> acquire(const DeploymentState&,CombatActorIdentity,unsigned,std::mt19937&) const;
    OpponentMagicTargets targets_metadata(const DeploymentState&,CombatActorIdentity,std::uint8_t,
        bool,std::uint8_t,std::optional<ItemOrigin>,const SpellCatalog&,std::mt19937&) const;
public:
    // Shared by native58 acquisition and its approved resistance context.
    static constexpr std::uint8_t breath_metadata=51;
    static OpponentMagicService load(const IFileSystem&);
    // Native03E4 and0545 always draw their initial d7, even with no inventory,
    // capability, opposition or permitted automatic party casting. Separate
    // calls preserve queued-cast/Turn placement between these driver passes.
    std::optional<CombatCastSchedule> select_item(const DeploymentState&,CombatActorIdentity,
        const ItemCatalog&,const SpellCatalog&,const OpponentMagicContext&,std::mt19937&) const;
    bool wants_turn(const DeploymentState&,CombatActorIdentity) const;
    std::optional<CombatCastSchedule> select_spell(const DeploymentState&,CombatActorIdentity,
        const ItemCatalog&,const SpellCatalog&,const OpponentMagicContext&,std::mt19937&) const;
    // Native phase14 breath uses automatic metadata51 acquisition without a
    // memorized cast, item, resource consumption or spell51 execution.
    OpponentMagicTargets breath_targets(const DeploymentState& state,CombatActorIdentity actor,
        const SpellCatalog& spells,std::mt19937& rng) const {
        auto result=targets_metadata(state,actor,breath_metadata,false,0,{},spells,rng);
        //20C3 initializes6DB9/A to the selected actor. Failed1E16 keeps it;
        //metadata51's mode8 single accepted result supplies its final cursor.
        result.cursor=result.intent?result.intent->centre:state.actors[find(state,actor)].position;
        return result;
    }
    // Genuine automatic326D1E16/20C3. Called only when a typed cast executes,
    // so delayed casts use live positions, levels and opposing state.
    OpponentMagicTargets targets(const DeploymentState&,const CombatCastPending&,
        const SpellCatalog&,std::mt19937&) const;
};
}
