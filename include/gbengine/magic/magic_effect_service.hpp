#pragma once
#include "gbengine/magic/spell_catalog.hpp"
#include "gbengine/party/party.hpp"
#include "gbengine/effects/effect_lifecycle.hpp"
#include "gbengine/magic/native_effect_context.hpp"
#include <random>
namespace GBEngine {
struct PeriodicEffectBindings;
enum class MagicDomain { exploration,combat,quest };
enum class MagicOrigin { memorized,item,script,innate };
enum class MagicTargetKind { none,party_member,combatant,tile,party };
struct MagicTarget {
    MagicTargetKind kind=MagicTargetKind::none;
    std::size_t id{};
    int x{},y{};
    auto operator<=>(const MagicTarget&) const =default;
};
struct MagicContext {
    MagicDomain domain=MagicDomain::exploration;
    bool casting_blocked=false;
};
struct MagicRequest {
    SpellId spell;
    MagicOrigin origin;
    MagicContext context;
    std::size_t caster;
    MagicTarget target;
    std::uint8_t caster_level;
    std::optional<ItemOrigin> item_origin={};
    ItemId* next_item_id=nullptr;
};
struct ItemMagicRequest {
    ItemCastId cast;
    MagicContext context;
    std::size_t caster;
    MagicTarget target;
    ItemOrigin item_origin;
    ItemId* next_item_id=nullptr;
};
enum class MagicStatus { applied,rejected,unresolved };
struct MagicFeedback {
    std::size_t recipient{};
    std::string text_id;
    // Native 10BA:1FEB effect branch; plain 16EC notices must not flash an icon.
    // Exploration consumes the same text but has no tactical recipient artwork.
    bool recipient_animation=false;
    auto operator<=>(const MagicFeedback&) const =default;
};
struct MagicResult {
    MagicStatus status=MagicStatus::unresolved;
    std::string message;
    std::optional<std::uint8_t> rolled_amount;
    // Native recipient messages in handler call order. An applied cast can
    // legitimately have no message, or report a resolved resistance veto.
    std::vector<MagicFeedback> feedback{};
    bool applied() const {return status==MagicStatus::applied;}
};
// All mutations and randomness are confined to caller-owned staging. A future
// combat adapter must bind real combatants, saves/areas and presentation; an
// unbound combat target is never translated into party member zero.
class IMagicEffectResolver {
public:
    virtual ~IMagicEffectResolver()=default;
    virtual MagicResult resolve(const MagicRequest&,std::span<Character>,std::mt19937&)=0;
    virtual MagicResult resolve_item(const ItemMagicRequest&,std::span<Character>,std::mt19937&) {
        return {MagicStatus::unresolved,"Item-only casting handler is not bound",{}};
    }
};
class MagicEffectService final : public IMagicEffectResolver, public IEffectExpiryResolver {
    friend class MonsterGazeService;
    friend class ShrineService;
    friend class CombatMagicService;
    friend class CombatMagicEffects;
    friend class CombatCloudService;
    const SpellCatalog& catalog_;
    const ItemCatalog* items_{};
    std::mt19937* random_{};
    std::uint8_t execution_mode_=1;
    static std::uint32_t roll(std::mt19937&,std::uint32_t sides);
    static bool application_queries(Character&,std::uint8_t,std::uint8_t,std::mt19937&);
    bool append_marker(Character&,const SpellDefinition&,const MagicRequest&,std::mt19937&,std::uint8_t context,std::uint8_t reversal=0);
    static bool reverse_native(Character&,const CharacterEffect&,std::optional<std::size_t>,bool suppress_periodic,NativeEffectContext={});
    static bool remove_native(Character&,std::size_t,bool suppress_periodic=false,NativeEffectContext={});
    static void haste_activation_queries(Character&);
    MagicResult resolve_weapon_magic(const MagicRequest&,std::span<Character>,std::mt19937&);
    static bool reverse_hammer(Character&,const ItemCatalog&);
    static PeriodicEffectBindings periodic_bindings(NativeEffectContext);
    static void set_native_status(Character&,std::uint8_t,EffectExpiryEdits&,NativeEffectContext);
    MagicResult resolve_poison_magic(const MagicRequest&,std::span<Character>,std::mt19937&);
    MagicResult resolve_reading_item(const ItemMagicRequest&,std::span<Character>,std::mt19937&);
    // Only the tactical adapter can bind combatant indices to its owned roster.
    MagicResult resolve_bound(const MagicRequest&,std::span<Character>,std::mt19937&,bool combat_bound);
    MagicResult resolve_item_bound(const ItemMagicRequest&,std::span<Character>,std::mt19937&,bool combat_bound);
public:
    explicit MagicEffectService(const SpellCatalog& catalog,const ItemCatalog* items=nullptr,std::mt19937* random=nullptr,std::uint8_t execution_mode=1):catalog_(catalog),items_(items),random_(random),execution_mode_(execution_mode) {}
    MagicResult resolve(const MagicRequest&,std::span<Character>,std::mt19937&) override;
    MagicResult resolve_item(const ItemMagicRequest&,std::span<Character>,std::mt19937&) override;
    static bool has_effect(const Character&,std::uint8_t code);
    static std::optional<bool> party_detects_magic(const Party&);
    // Work on caller staging. Removal is first-match unless an exact owned
    // index is supplied. Unknown reversal callbacks must remain unresolved.
    static bool remove_effect(Character&,std::uint8_t code,NativeEffectContext={});
    static bool remove_effect_at(Character&,std::size_t index,NativeEffectContext={});
    static bool reverse_effect(Character&,const CharacterEffect&,std::optional<std::size_t> exact_index={},NativeEffectContext={});
    static std::uint8_t install_strength(Character&,std::uint8_t strength,std::uint8_t exceptional);
    bool reverse(const EffectExpiryRequest&,Character&) override;
    bool reverse_with_edits(const EffectExpiryRequest&,Character&,EffectExpiryEdits&) override;
    bool allows_context_updates() const override {return true;}
};
}
