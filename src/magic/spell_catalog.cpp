#include "gbengine/magic/spell_catalog.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
namespace GBEngine {
SpellId::SpellId(std::uint8_t id):value(id) {
    if(id<1 || id>56) throw DecodeError("Regular spell ID outside native table");
}
ItemCastId::ItemCastId(std::uint8_t id):value(id) {
    if(id<57 || id>67) throw DecodeError("Item casting index outside native table");
}
ItemCastDefinition::ItemCastDefinition(ItemCastId id,std::span<const std::uint8_t> bytes,SpellProvenance source)
    :id_(id),provenance_(std::move(source)) {
    if(bytes.size()!=bytes_.size()) throw DecodeError("Invalid native item casting definition width");
    std::copy(bytes.begin(),bytes.end(),bytes_.begin());
    if(school()!=2 || level()!=6) throw DecodeError("Invalid item-only caster family or level");
}
SpellDefinition::SpellDefinition(SpellId id,std::span<const std::uint8_t> bytes,SpellProvenance source)
    :id_(id),provenance_(std::move(source)),name_id_(pascal_string_id("START.EXE:module",0xf0a9+41u*id.value)) {
    if(bytes.size()!=bytes_.size()) throw DecodeError("Invalid native spell definition width");
    std::copy(bytes.begin(),bytes.end(),bytes_.begin());
    if(school()>1 || level()<1 || level()>7) throw DecodeError("Invalid regular spell school or level");
}
std::string_view SpellDefinition::name() const {return StringStore::active().get(name_id_);}
SpellCatalog SpellCatalog::load(const IFileSystem& files) {
    const auto source=files.read_asset("START.EXE");const auto module=decode_start_data(source);
    constexpr std::size_t first=0xf9ba+16, count=56, width=16;
    if(module.size()<first+67*width) throw DecodeError("Native spell definition table is truncated");
    const auto source_hash=sha256(source),module_hash=sha256(module);
    SpellCatalog catalog;catalog.definitions_.reserve(count);
    for(std::uint8_t value=1;value<=count;++value) {
        const auto at=0xf9ba+width*value;
        SpellDefinition definition(SpellId(value),std::span<const std::uint8_t>(module).subspan(at,width),
            {"START.EXE",at,source_hash,module_hash});
        if(definition.name().empty()) throw DecodeError("Native spell name is missing");
        catalog.definitions_.push_back(std::move(definition));
    }
    catalog.item_definitions_.reserve(11);
    for(std::uint8_t value=57;value<=67;++value) {
        const auto at=0xf9ba+width*value;
        catalog.item_definitions_.push_back(ItemCastDefinition(ItemCastId(value),std::span<const std::uint8_t>(module).subspan(at,width),
            {"START.EXE",at,source_hash,module_hash}));
    }
    catalog.native_module_=module;
    return catalog;
}
const ItemCastDefinition& SpellCatalog::item_definition(ItemCastId id) const {
    const ItemCastId checked(id.value);
    if(item_definitions_.size()!=11) throw DecodeError("Item casting catalog is not loaded");
    return item_definitions_.at(static_cast<std::size_t>(checked.value-57));
}
const SpellDefinition& SpellCatalog::definition(SpellId id) const {
    const SpellId checked(id.value);
    if(definitions_.size()!=56) throw DecodeError("Spell catalog is not loaded");
    return definitions_.at(static_cast<std::size_t>(checked.value-1));
}
}
