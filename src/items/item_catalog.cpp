#include "gbengine/items/item_catalog.hpp"
#include "gbengine/assets/decoders.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>

namespace GBEngine {
ItemDefinition ItemDefinition::decode(std::span<const std::uint8_t> bytes) {
    if(bytes.size()!=16) throw DecodeError("Invalid item definition width");
    ItemDefinition result;std::copy(bytes.begin(),bytes.end(),result.bytes_.begin());return result;
}
std::array<ItemDefinition,128> ItemCatalog::decode_definitions(std::span<const std::uint8_t> bytes) {
    // Native 2E80:002D seeks two bytes, then loads the type table at DS:5582.
    // Preserve the selected file's two-byte prefix as opaque, not a count.
    if(bytes.size()!=2+128*16) throw DecodeError("Unsupported ITEMS table length");
    std::array<ItemDefinition,128> result;
    for(std::size_t i=0;i<result.size();++i) result[i]=ItemDefinition::decode(bytes.subspan(2+16*i,16));
    return result;
}
const ItemDefinition& ItemCatalog::definition(std::uint8_t type) const {
    if(type>=definitions_.size()) throw DecodeError("Unknown item type outside ITEMS table");
    return definitions_[type];
}
std::string_view ItemCatalog::spell_name(std::uint8_t spell) {
    if(spell<1 || spell>56) throw DecodeError("Unknown native scroll spell identifier");
    return StringStore::active().get(pascal_string_id("START.EXE:module",0xf0a9+41u*spell));
}
ItemCatalog ItemCatalog::load(const IFileSystem& files) {
    ItemCatalog result;result.definitions_=decode_definitions(files.read_asset("ITEMS"));
    const auto module=decode_start_data(files.read_asset("START.EXE"));
    for(std::size_t i=0;i<result.generated_item_words_.size();++i)
        result.generated_item_words_[i]=read_u16(module,0xc7c0+0x970+2*i);
    for(std::size_t i=0;i<5;++i) result.coin_rates_[i]=read_u32(module,0xc7c0+0xd24+4*i);
    if(result.coin_rates_!=std::array<std::uint32_t,5>{1,10,100,200,1000}) throw DecodeError("Unsupported native currency table");
    unsigned archives=0;
    for(const auto& file:files.list_assets()) {
        if(file.size()!=9 || !file.starts_with("ITEM") || file[4]<'1' || file[4]>'8' || !file.ends_with(".DAX")) continue;
        ++archives;const auto disk=static_cast<unsigned>(file[4]-'0');const DaxArchive archive(files.read_asset(file));
        for(const auto& record:archive.entries) {
            if(record.data.size()%63) throw DecodeError("Truncated ITEM archive template record");
            for(std::size_t at=0;at<record.data.size();at+=63) {
                auto item=Item::decode(std::span(record.data).subspan(at,63),{file,record.id,at});
                (void)result.definition(item.type());
                result.templates_.emplace(ItemTemplateId{disk,record.id,at/63},std::move(item));
            }
        }
    }
    if(archives!=8) throw DecodeError("Item catalog requires all eight ITEM archives");
    return result;
}
}
