#include "gbengine/saves/port_save.hpp"
#include "gbengine/world/exploration_session.hpp"
#include "gbengine/items/item_catalog.hpp"
#include "gbengine/resources/string_store.hpp"
#include "gbengine/ui/glyph_text.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>

namespace GBEngine {
struct PortSaveState {
    JournalState journal;
    EclStateBanks banks;
    Bytes script;
    std::string text_origin,random;
    std::size_t loaded_size{},steps{};
    std::uint16_t pc{};
    std::array<bool,6> flags{};
    std::uint64_t sequence{};
    bool modified{},dirty{};
    Party party;
    unsigned disk{};
    std::uint8_t record{};
    int x{},y{};
    Direction facing{};
    std::optional<GeoMap> map;
    std::optional<ExplorationBackground> background; // Derived only; not part of wire format.
    std::optional<std::array<std::uint8_t,44*36>> city;
    bool city_replacement=false;
    struct Wall {unsigned disk{},slot{};std::uint8_t record{};};
    std::array<std::optional<Wall>,3> walls;
    struct Picture {unsigned disk{};std::uint8_t picture{},head{};std::size_t frame{};std::uint64_t elapsed{};};
    std::optional<Picture> picture;
    std::array<std::uint16_t,4> tile_query{};
    bool tile_available{},area{};
    std::array<std::optional<std::uint16_t>,3> call_parameters{};
    CampaignResources campaign;
    std::uint16_t rest_encounter_ticks{};
    std::optional<ExplorationSpriteSelection> sprite_selection;
    bool sprite_active=false;
    struct Sprite {unsigned disk{};std::uint8_t record{},distance{};};
    std::optional<Sprite> sprite;
    std::optional<std::uint8_t> sprite_closeup_selector;
    std::uint16_t saved_search{};
    std::string display,glyphs;
    std::vector<std::string> messages;
    std::map<std::string,std::string> sources;
};
namespace {
constexpr std::array<std::uint8_t,8> signature{'G','B','S','A','V','E',13,10};
constexpr std::size_t max_file=4*1024*1024;
#if defined(_MSVC_STL_VERSION)
constexpr std::string_view random_backend="MT19937:MSVC ";
constexpr bool random_has_cursor=false;
#elif defined(__GLIBCXX__)
constexpr std::string_view random_backend="MT19937:LIBSTDCXX ";
constexpr bool random_has_cursor=true;
#else
#error Unsupported standard library RNG snapshot representation
#endif
void require(bool value,const char* message) {if(!value) throw DecodeError(message);}
struct Writer {
    Bytes bytes;
    void u8(std::uint8_t v) {bytes.push_back(v);}
    void u16(std::uint16_t v) {u8(static_cast<std::uint8_t>(v));u8(static_cast<std::uint8_t>(v>>8));}
    void u32(std::uint32_t v) {for(unsigned i=0;i<4;++i) u8(static_cast<std::uint8_t>(v>>(8*i)));}
    void u64(std::uint64_t v) {for(unsigned i=0;i<8;++i) u8(static_cast<std::uint8_t>(v>>(8*i)));}
    void raw(std::span<const std::uint8_t> v) {bytes.insert(bytes.end(),v.begin(),v.end());}
    void text(std::string_view v) {
        require(v.size()<=max_file,"Snapshot string exceeds limit");u32(static_cast<std::uint32_t>(v.size()));
        raw({reinterpret_cast<const std::uint8_t*>(v.data()),v.size()});
    }
};
struct Reader {
    std::span<const std::uint8_t> bytes;std::size_t at{};
    std::span<const std::uint8_t> raw(std::size_t count) {
        require(count<=bytes.size()-at,"Truncated port snapshot");const auto result=bytes.subspan(at,count);at+=count;return result;
    }
    std::uint8_t u8() {return raw(1)[0];}
    bool boolean() {const auto v=u8();require(v<=1,"Invalid snapshot Boolean");return v!=0;}
    std::uint16_t u16() {const auto v=raw(2);return static_cast<std::uint16_t>(v[0]|(v[1]<<8));}
    std::uint32_t u32() {const auto v=raw(4);std::uint32_t n=0;for(unsigned i=0;i<4;++i)n|=static_cast<std::uint32_t>(v[i])<<(8*i);return n;}
    std::uint64_t u64() {const auto v=raw(8);std::uint64_t n=0;for(unsigned i=0;i<8;++i)n|=static_cast<std::uint64_t>(v[i])<<(8*i);return n;}
    std::size_t count(std::size_t maximum) {const auto n=u32();require(n<=maximum,"Snapshot collection exceeds limit");return n;}
    std::string text(std::size_t maximum) {const auto data=raw(count(maximum));return {data.begin(),data.end()};}
    template<std::size_t N> std::array<std::uint8_t,N> array() {std::array<std::uint8_t,N> result;const auto data=raw(N);std::copy(data.begin(),data.end(),result.begin());return result;}
};
std::span<const std::uint8_t> snapshot_payload(std::span<const std::uint8_t> bytes) {
    require(bytes.size()<=max_file+80,"Port snapshot exceeds file limit");Reader header{bytes};
    const auto sig=header.raw(signature.size());require(std::equal(sig.begin(),sig.end(),signature.begin()),"Invalid port save signature");
    require(header.u32()==PortSave::format_version,"Unsupported port save version; create a new save with this build");const auto size=header.count(max_file);
    const auto digest=header.raw(64);const auto payload=header.raw(size);require(header.at==bytes.size(),"Trailing port save data");
    require(std::string(digest.begin(),digest.end())==sha256(payload),"Port save checksum mismatch");return payload;
}
void origin(Writer& w,const ItemOrigin& v) {w.text(v.file);w.u8(v.archive_record.has_value());if(v.archive_record)w.u8(*v.archive_record);w.u64(v.record_offset);}
ItemOrigin origin(Reader& r) {
    ItemOrigin v;v.file=r.text(255);if(r.boolean())v.archive_record=r.u8();const auto offset=r.u64();
    require(offset<=std::numeric_limits<std::uint32_t>::max(),"Invalid item provenance offset");v.record_offset=static_cast<std::size_t>(offset);return v;
}
bool source_name(std::string_view name) {
    return name=="START.EXE" || name=="GAME.OVR" || name=="ITEMS" || name.ends_with(".DAX");
}
bool plain_filename(std::string_view name) {
    return !name.empty() && name.size()<=64 && std::all_of(name.begin(),name.end(),[](unsigned char c){return (c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.';});
}
bool hash(std::string_view value) {return value.size()==64 && std::all_of(value.begin(),value.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
void validate_item_origin(const ItemOrigin& origin,const std::map<std::string,std::string>& sources) {
    if(origin.file=="GAME.OVR:spiritual-hammer") {
        require(!origin.archive_record&&origin.record_offset==62523,"Invalid generated weapon provenance");return;
    }
    if(origin.file=="START.EXE:random-treasure") {
        require(!origin.archive_record&&origin.record_offset<128,"Invalid generated treasure provenance");return;
    }
    if(origin.file=="GAME.OVR") {
        require(!origin.archive_record&&(origin.record_offset==0x26db3||origin.record_offset==0x26a60),"Invalid appraised item provenance");return;
    }
    require(plain_filename(origin.file)&&origin.record_offset%63==0,"Invalid owned item provenance");
    require(origin.archive_record?(origin.file.ends_with(".DAX")&&sources.contains(origin.file)):
        origin.file.ends_with(".ITM"),"Unsupported owned item source identity");
}
std::mt19937 random_from(std::string_view text) {
    require(text.starts_with(random_backend),"Port save RNG backend differs from this build; use a matching compiler build or create a new save");
    text.remove_prefix(random_backend.size());
    // Library extraction alone does not bound the MT cursor or (on platforms
    // with a wider uint_fast32_t) its state words. Validate before installing it.
    std::istringstream tokens{std::string(text)};tokens.imbue(std::locale::classic());
    std::string token;bool nonzero=false;
    const auto count=std::mt19937::state_size+(random_has_cursor?1:0);
    for(std::size_t i=0;i<count;++i) {
        require(static_cast<bool>(tokens>>token),"Truncated snapshot random state");
        std::uint64_t value{};const auto [end,error]=std::from_chars(token.data(),token.data()+token.size(),value);
        require(error==std::errc{}&&end==token.data()+token.size(),"Malformed snapshot random state");
        if(i==std::mt19937::state_size) require(value<=std::mt19937::state_size,"Invalid snapshot random cursor");
        else {require(value<=std::numeric_limits<std::uint32_t>::max(),"Invalid snapshot random word");nonzero=nonzero||value!=0;}
    }
    require(nonzero,"Degenerate snapshot random state");
    require(!(tokens>>token),"Trailing snapshot random state");
    std::mt19937 result;std::istringstream stream{std::string(text)};stream.imbue(std::locale::classic());stream>>result;
    require(!stream.fail(),"Malformed snapshot random state");stream>>std::ws;require(stream.eof(),"Trailing snapshot random state");return result;
}
void validate_scene(const PortSaveState& v,const ExplorationAssets& assets,const std::optional<CityAssets>& city,
        const std::optional<PicturePresentation>& picture,const std::optional<ExplorationSpritePresentation>& sprite) {
    auto frame=std::make_unique<Framebuffer>();
    if(picture)picture->draw(*frame);
    else if(city&&v.record>=25&&v.record<=27&&v.banks.raw_value(0x49e6)==0)
        city->draw(*frame,v.banks.raw_value(0x49c3),v.banks.raw_value(0x49c4),v.record-23);
    else {
        WorldState scene;scene.map=v.map;scene.x=v.x;scene.y=v.y;scene.facing=v.facing;scene.background=v.background;
        render_exploration(*frame,scene,assets);if(sprite)sprite->draw(*frame);
    }
}
}
PortSave::PortSave(std::shared_ptr<const PortSaveState> state):state_(std::move(state)) {}
unsigned PortSave::disk() const {return state_->disk;}
std::uint8_t PortSave::record() const {return state_->record;}
PortSave PortSave::capture(const ExplorationSession& s) {
    require(s.world.background==ExplorationBackground::rebuild(s.disk(),s.record_,s.vm.state_banks(),s.vm.script_),
        "Saving cannot discard non-reconstructible exploration background state");
    require(!s.nested_camp_active(),"Saving cannot suspend a nested ECL camp invocation");
    require(!s.world.removal_activity_armed && !s.world.removal_name_armed,"Saving cannot discard armed native roster-removal latches");
    require(s.can_act() && s.campaign_time_bound_ && !s.time_blocked() && !s.arriving_ && !s.picture_token_,
        "Saving requires stable restored exploration without a pending action");
    require(s.vm.stack_.empty() && s.vm.pending_followups_.empty() && !s.vm.pending_,"Saving cannot suspend script execution");
    require(!s.expiry_resolver_,"Custom effect resolver cannot be persisted");
    require(!s.sprite_cache_ && !s.sprite_closeup_shown_ && !s.pending_sprite_distance_,"Saving cannot retain an invocation sprite cache or pending distance");
    require(!s.sprite_active_ || s.sprite_selection_,"Active exploration sprite requires a selected source");
    require(!(s.sprite_ && s.picture_),"Saving cannot retain both a picture and an exploration sprite");
    require(s.world.party.size()>=1 && s.world.party.size()<=8,"Saving requires a campaign party");
    require(GeoMap::contains(s.world.x,s.world.y)&&static_cast<unsigned>(s.world.facing)<=3,"Invalid snapshot world position or facing");
    auto v=std::make_shared<PortSaveState>();v->journal=s.journal_.state;v->banks=s.vm.state_banks();v->script=s.vm.script_;
    v->text_origin=s.vm.text_origin_;v->loaded_size=s.vm.loaded_size_;v->pc=s.vm.pc_;v->flags=s.vm.flags_;
    v->sequence=s.vm.request_sequence_;v->steps=s.vm.steps_;v->modified=s.vm.script_modified_;v->dirty=s.vm.area1_dirty_;
    std::ostringstream random;random.imbue(std::locale::classic());random<<random_backend<<s.vm.random_;v->random=random.str();
    v->party=s.world.party;v->disk=s.disk();v->record=s.record_;v->x=s.world.x;v->y=s.world.y;v->facing=s.world.facing;v->map=s.world.map;
    if(s.city_){v->city=s.city_->cells;v->city_replacement=s.city_->replacement_tiles_loaded;}
    for(std::size_t i=0;i<3;++i) {
        require(s.assets.slots_[i].has_value()==s.assets.sources_[i].has_value(),"Wall source identity is missing");
        if(const auto& src=s.assets.sources_[i])v->walls[i]=PortSaveState::Wall{src->disk,src->slot,src->record};
    }
    if(s.picture_) {
        const auto& p=*s.picture_;v->picture=PortSaveState::Picture{p.disk_,p.picture_,p.head_,p.frame_,p.elapsed_};
    }
    v->tile_query={s.world.tile_query_x,s.world.tile_query_y,s.world.tile_query_value,s.world.tile_query_result};
    v->tile_available=s.world.tile_query_available;v->area=s.area_view_;v->saved_search=s.saved_search_;
    v->call_parameters=s.world.call_parameters;
    v->campaign=s.campaign_;
    v->rest_encounter_ticks=s.rest_camp_.encounter_ticks;
    v->sprite_selection=s.sprite_selection_;v->sprite_active=s.sprite_active_;
    if(s.sprite_)v->sprite=PortSaveState::Sprite{s.sprite_->disk(),s.sprite_->record(),s.sprite_->distance()};
    v->sprite_closeup_selector=s.sprite_closeup_selector_;
    v->display=s.display_text;v->glyphs=s.glyph_text_;v->messages=s.messages;
    (void)GlyphText(v->glyphs);
    for(const auto& name:s.files_.list_assets())if(source_name(name)) {
        require(plain_filename(name),"Invalid source asset identity");v->sources.emplace(name,sha256(s.files_.read_asset(name)));
    }
    require(v->sources.contains("START.EXE") && v->sources.contains("GAME.OVR") && v->sources.contains("ITEMS"),"Snapshot requires selected binary and item-table fingerprints");
    // The codec's validation is also the capture validator. This avoids emitting
    // a snapshot which the same engine could not restore.
    const auto result=decode(PortSave(v).encode());
    // Check all bounds before drawing (including the bank-backed city cursor).
    // An idle VM can still lack the artwork needed for its first presentation.
    auto frame=std::make_unique<Framebuffer>();s.draw_view(*frame);
    return result;
}
Bytes PortSave::encode() const {
    const auto& v=*state_;Writer w;
    w.u32(static_cast<std::uint32_t>(v.sources.size()));for(const auto& [name,digest]:v.sources){w.text(name);w.text(digest);}
    w.u32(v.disk);w.u8(v.record);w.u16(static_cast<std::uint16_t>(v.x));w.u16(static_cast<std::uint16_t>(v.y));w.u8(static_cast<std::uint8_t>(v.facing));
    auto saved_banks=v.banks;pack_journal(saved_banks,v.journal);
    for(const auto n:saved_banks.area1)w.u16(n);
    for(const auto n:v.banks.area2)w.u16(n);
    for(const auto n:v.banks.area3)w.u16(n);
    w.raw(v.script);w.u32(static_cast<std::uint32_t>(v.loaded_size));w.text(v.text_origin);w.u8(v.modified);w.u8(v.dirty);w.u16(v.pc);
    for(const auto f:v.flags)w.u8(f);
    w.u64(v.sequence);w.u64(v.steps);w.text(v.random);
    w.u8(v.map.has_value());if(v.map){w.u8(v.map->id);w.raw(v.map->planes);}
    w.u8(v.city.has_value());if(v.city){w.u8(v.city_replacement);w.raw(*v.city);}
    for(const auto& wall:v.walls){w.u8(wall.has_value());if(wall){w.u32(wall->disk);w.u8(static_cast<std::uint8_t>(wall->slot));w.u8(wall->record);}}
    w.u8(v.picture.has_value());if(v.picture){w.u32(v.picture->disk);w.u8(v.picture->picture);w.u8(v.picture->head);w.u32(static_cast<std::uint32_t>(v.picture->frame));w.u64(v.picture->elapsed);}
    for(const auto n:v.tile_query)w.u16(n);
    w.u8(v.tile_available);w.u8(v.area);w.u16(v.saved_search);
    w.text(v.display);w.text(v.glyphs);w.u32(static_cast<std::uint32_t>(v.messages.size()));for(const auto& m:v.messages)w.text(m);
    const auto& p=v.party;w.u8(static_cast<std::uint8_t>(p.members_.size()));w.u8(static_cast<std::uint8_t>(p.selected_.value_or(255)));w.u64(p.next_item_id_);w.u64(p.revision_);
    for(const auto& c:p.members_) {
        w.raw(c.bytes_);w.u8(c.effects_restored_);w.u8(static_cast<std::uint8_t>(c.inventory_.size()));
        for(const auto& item:c.inventory_.items_){w.raw(item.bytes_);w.u64(item.id_);origin(w,item.origin_);}
        w.u32(static_cast<std::uint32_t>(c.bound_equipment_effects_.size()));for(const auto id:c.bound_equipment_effects_)w.u64(id);
        w.u32(static_cast<std::uint32_t>(c.effects_.size()));
        for(const auto& e:c.effects_) {
            w.raw(e.payload);w.text(e.source_file);w.u64(e.source_offset);
            w.u8(e.spell_id.has_value());if(e.spell_id)w.u8(*e.spell_id);
            w.u8(e.item_cast_id.has_value());if(e.item_cast_id)w.u8(*e.item_cast_id);
            w.u8(e.item_origin.has_value());if(e.item_origin)origin(w,*e.item_origin);
            w.u8(e.equipment_item_id.has_value());if(e.equipment_item_id)w.u64(*e.equipment_item_id);
        }
    }
    for(const auto& value:v.call_parameters){w.u8(value.has_value());if(value)w.u16(*value);}
    for(const auto amount:v.campaign.amounts)w.u32(amount);
    w.u64(v.campaign.next_item_id);w.u8(v.campaign.combat_setup);
    w.u32(static_cast<std::uint32_t>(v.campaign.items.size()));
    for(const auto& entry:v.campaign.items){w.u64(entry.id);w.raw(entry.item.bytes_);origin(w,entry.item.origin_);}
    w.u16(v.rest_encounter_ticks);
    w.u8(v.sprite_selection.has_value());
    if(v.sprite_selection){w.u8(v.sprite_selection->record);w.u8(v.sprite_selection->body);w.u8(v.sprite_active);}
    w.u8(v.sprite.has_value());if(v.sprite){w.u32(v.sprite->disk);w.u8(v.sprite->record);w.u8(v.sprite->distance);}
    w.u8(v.sprite_closeup_selector.has_value());if(v.sprite_closeup_selector)w.u8(*v.sprite_closeup_selector);
    require(w.bytes.size()<=max_file,"Port snapshot exceeds file limit");
    Writer result;result.raw(signature);result.u32(format_version);result.u32(static_cast<std::uint32_t>(w.bytes.size()));
    const auto digest=sha256(w.bytes);result.raw({reinterpret_cast<const std::uint8_t*>(digest.data()),digest.size()});result.raw(w.bytes);return result.bytes;
}
SaveSummary PortSave::inspect(std::span<const std::uint8_t> bytes) {
    Reader r{snapshot_payload(bytes)};
    // Bounded schema6 prefix walk, with no source reads, party reconstruction,
    // ECL execution or RNG deserialization. Full decode/restore remains mandatory
    // when Load is selected, including fingerprints and the complete tail.
    const auto count=r.count(256);require(count>=4,"Missing port save source identities");
    for(std::size_t i=0;i<count;++i){(void)r.text(64);(void)r.text(64);}
    SaveSummary result;result.disk=r.u32();result.script=r.u8();
    require(result.disk>=1&&result.disk<=8,"Invalid snapshot resource disk");
    (void)r.raw(5); // Interior x/y/direction.
    const auto area1=r.raw(2048);(void)r.raw(2048+1024);
    for(unsigned i=0;i<7;++i)result.clock[i]=read_u16(area1,2*(0xc6+i));
    const auto city_flag=read_u16(area1,2*0xe6);
    (void)r.raw(0x1e00);(void)r.count(0x1e00);(void)r.text(128);
    (void)r.boolean();(void)r.boolean();(void)r.u16();
    for(unsigned i=0;i<6;++i)(void)r.boolean();
    (void)r.u64();(void)r.u64();const auto random=r.text(16384);
    require(random.starts_with(random_backend),"Port save RNG backend differs from this build; use a matching compiler build or create a new save");
    if(r.boolean()){result.geometry=r.u8();(void)r.raw(1024);}
    if(r.boolean()) {
        (void)r.boolean();(void)r.raw(44*36);
        result.overland=city_flag==0 && result.script>=25 && result.script<=27;
    }
    return result;
}
PortSave PortSave::decode(std::span<const std::uint8_t> bytes) {
    const auto payload=snapshot_payload(bytes);Reader r{payload};auto v=std::make_shared<PortSaveState>();
    const auto count=r.count(256);require(count>=4,"Missing port save source identities");
    for(std::size_t i=0;i<count;++i){auto name=r.text(64);auto value=r.text(64);require(plain_filename(name)&&source_name(name)&&hash(value),"Invalid source fingerprint");require(v->sources.emplace(name,value).second,"Duplicate source fingerprint");}
    require(v->sources.contains("START.EXE")&&v->sources.contains("GAME.OVR")&&v->sources.contains("ITEMS"),"Missing selected binary or item-table fingerprints");
    v->disk=r.u32();require(v->disk>=1&&v->disk<=8,"Invalid snapshot resource disk");v->record=r.u8();v->x=r.u16();v->y=r.u16();const auto direction=r.u8();require(direction<=3,"Invalid snapshot facing");v->facing=static_cast<Direction>(direction);
    for(auto& n:v->banks.area1)n=r.u16();
    for(auto& n:v->banks.area2)n=r.u16();
    for(auto& n:v->banks.area3)n=r.u16();
    v->journal=extract_journal(v->banks);
    const auto image=r.raw(0x1e00);v->script.assign(image.begin(),image.end());v->loaded_size=r.count(0x1e00);require(v->loaded_size>=20,"Invalid loaded script length");
    Bytes record{0,0};record.insert(record.end(),image.begin(),image.begin()+static_cast<std::ptrdiff_t>(v->loaded_size));const auto entries=inspect_ecl_header(record);(void)entries;
    v->text_origin=r.text(128);v->modified=r.boolean();v->dirty=r.boolean();v->pc=r.u16();require(v->pc>=0x9900&&v->pc<=0x9900+v->loaded_size,"Invalid idle script cursor");
    for(auto& f:v->flags)f=r.boolean();
    v->sequence=r.u64();require(v->sequence<std::numeric_limits<std::uint64_t>::max(),"Exhausted script request sequence");
    const auto steps=r.u64();require(steps<=1000000000,"Invalid snapshot step count");v->steps=static_cast<std::size_t>(steps);v->random=r.text(16384);(void)random_from(v->random);
    if(r.boolean()){GeoMap map;map.id=r.u8();map.planes=r.array<1024>();v->map=map;}
    if(r.boolean()){v->city_replacement=r.boolean();v->city=r.array<44*36>();}
    for(std::size_t i=0;i<3;++i)if(r.boolean()){
        PortSaveState::Wall wall;wall.disk=r.u32();wall.slot=r.u8();wall.record=r.u8();require(wall.disk>=1&&wall.disk<=8&&wall.slot>=1&&wall.slot<=i+1,"Invalid wall asset identity");v->walls[i]=wall;
    }
    if(r.boolean()){
        PortSaveState::Picture p;p.disk=r.u32();p.picture=r.u8();p.head=r.u8();p.frame=r.count(7);p.elapsed=r.u64();require(p.disk>=1&&p.disk<=8,"Invalid picture disk");v->picture=p;
    }
    for(auto& n:v->tile_query)n=r.u16();
    v->tile_available=r.boolean();v->area=r.boolean();v->saved_search=r.u16();v->display=r.text(4096);v->glyphs=r.text(4096);
    const auto glyph_state=GlyphText(v->glyphs);
    for(std::size_t i=0;!glyph_state.native && i<v->glyphs.size();++i)if(v->glyphs[i]==1){
        require(++i<v->glyphs.size()&&static_cast<unsigned char>(v->glyphs[i])<GameFont::glyph_count,"Invalid saved glyph stream");
    }
    const auto messages=r.count(256);for(std::size_t i=0;i<messages;++i)v->messages.push_back(r.text(4096));
    auto& p=v->party;const auto members=r.u8(),selected=r.u8();require(members>=1&&members<=8&&selected<members,"Invalid snapshot roster selection");p.selected_=selected;p.next_item_id_=r.u64();p.revision_=r.u64();
    require(p.revision_<std::numeric_limits<std::uint64_t>::max(),"Exhausted party revision");std::set<ItemId> all_ids;ItemId highest=0;
    for(std::size_t n=0;n<members;++n){
        const auto raw=r.array<285>();auto c=Character::decode(raw);require(c.bytes_==raw,"Snapshot contains serialized process pointers");c.effects_restored_=r.boolean();require(c.effects_restored_,"Snapshot requires restored effect ownership");
        const auto items=r.u8();require(items<=16&&items==c.byte(0xc7),"Snapshot inventory count mismatch");std::set<ItemId> local_ids;
        for(std::size_t i=0;i<items;++i){
            const auto item_bytes=r.array<63>();auto item=Item::decode(item_bytes);require(item.bytes_==item_bytes,"Snapshot item contains process pointers or display scratch");item.id_=r.u64();item.origin_=origin(r);
            require(item.type()<128,"Invalid owned item definition index");validate_item_origin(item.origin_,v->sources);
            require(item.id_!=0&&all_ids.insert(item.id_).second,"Duplicate or null snapshot item identity");highest=std::max(highest,item.id_);local_ids.insert(item.id_);c.inventory_.items_.push_back(std::move(item));
        }
        const auto bindings=r.count(16);for(std::size_t i=0;i<bindings;++i){const auto id=r.u64();require(local_ids.contains(id)&&c.bound_equipment_effects_.insert(id).second,"Invalid equipment ownership binding");}
        const auto effects=r.count(65535);for(std::size_t i=0;i<effects;++i){
            CharacterEffect e;e.payload=r.array<5>();e.source_file=r.text(255);const auto offset=r.u64();require(offset<=std::numeric_limits<std::uint32_t>::max(),"Invalid effect source offset");e.source_offset=static_cast<std::size_t>(offset);
            if(r.boolean()){e.spell_id=r.u8();require(*e.spell_id>=1&&*e.spell_id<=56,"Invalid effect spell identity");}
            if(r.boolean()){e.item_cast_id=r.u8();require(*e.item_cast_id>=57&&*e.item_cast_id<=67,"Invalid effect item-cast identity");}
            if(r.boolean())e.item_origin=origin(r);
            if(r.boolean()){e.equipment_item_id=r.u64();require(local_ids.contains(*e.equipment_item_id)&&c.bound_equipment_effects_.contains(*e.equipment_item_id),"Dangling equipment effect ownership");}
            require(!(e.spell_id&&e.item_cast_id)&&!(e.equipment_item_id&&(e.spell_id||e.item_cast_id)),"Conflicting effect ownership domains");c.effects_.push_back(std::move(e));
        }
        for(const auto& item:c.inventory_.items_)if(c.bound_equipment_effects_.contains(item.id_))require(item.readied()&&item.byte(0x3e)>=128,"Binding refers to inactive equipment");
        p.members_.push_back(std::move(c));
    }
    require(p.next_item_id_>highest&&p.next_item_id_<std::numeric_limits<ItemId>::max(),"Invalid next item identity");
    for(auto& value:v->call_parameters)if(r.boolean())value=r.u16();
    {
        for(auto& amount:v->campaign.amounts)amount=r.u32();
        v->campaign.next_item_id=r.u64();v->campaign.combat_setup=r.boolean();
        const auto items=r.count(4096);std::set<CampaignItemId> identities;CampaignItemId greatest=0;
        for(std::size_t i=0;i<items;++i) {
            CampaignItem entry;entry.id=r.u64();const auto raw=r.array<63>();entry.item=Item::decode(raw);
            require(entry.item.bytes_==raw,"Campaign item contains process pointers or display scratch");
            require(entry.item.type()<128,"Invalid campaign item definition index");
            entry.item.origin_=origin(r);const auto& provenance=entry.item.origin_;
            const bool generated=provenance.file=="START.EXE:random-treasure";
            if(generated) {
                const auto type=entry.item.type();
                const bool supported=(type>=1&&type<=47&&type!=45)||(type>=50&&type<=59)||
                    type==61||type==62||type==67||type==69||type==70||type==71||type==73||type==77||type==78||type==79||type==84||type==92||type==93;
                require(supported&&!provenance.archive_record&&provenance.record_offset==type,"Invalid generated campaign item provenance");
            } else require(plain_filename(provenance.file)&&provenance.record_offset%63==0,"Invalid campaign item provenance");
            const bool archive=provenance.file.size()==9&&provenance.file.starts_with("ITEM")&&
                provenance.file[4]>='1'&&provenance.file[4]<='8'&&provenance.file.ends_with(".DAX");
            require(generated|| (archive?(provenance.archive_record.has_value()&&v->sources.contains(provenance.file)):
                (provenance.file.ends_with(".ITM")&&!provenance.archive_record)),"Unsupported campaign item source identity");
            require(entry.id!=0&&identities.insert(entry.id).second,"Duplicate or null campaign item identity");
            greatest=std::max(greatest,entry.id);v->campaign.items.push_back(std::move(entry));
        }
        require(v->campaign.next_item_id>greatest&&v->campaign.next_item_id<std::numeric_limits<CampaignItemId>::max(),"Invalid next campaign item identity");
    }
    v->rest_encounter_ticks=r.u16();
    {
        if(r.boolean()){v->sprite_selection=ExplorationSpriteSelection{r.u8(),r.u8()};v->sprite_active=r.boolean();}
        if(r.boolean()){
            PortSaveState::Sprite sprite;sprite.disk=r.u32();sprite.record=r.u8();sprite.distance=r.u8();
            require(sprite.disk>=1&&sprite.disk<=8&&sprite.distance<=2,"Invalid saved exploration sprite identity");v->sprite=sprite;
        }
        if(r.boolean())v->sprite_closeup_selector=r.u8();
    }
    require(!(v->sprite && v->picture),"Snapshot contains both picture and exploration sprite art");
    require(r.at==payload.size(),"Trailing snapshot payload");
    const bool city=v->city&&v->record>=25&&v->record<=27&&v->banks.raw_value(0x49e6)==0;
    require(v->banks.raw_value(0x49e6)==1 || city,"Snapshot has an unsupported exploration mode");
    if(city)require(v->disk==v->record-19u,"Snapshot overland region disagrees with resource disk");
    require(city||v->map.has_value(),"Snapshot has no active exploration map");
    require(v->x>=0&&v->x<16&&v->y>=0&&v->y<16,"Invalid snapshot interior coordinates");
    if(city)require(v->banks.raw_value(0x49c3)<16&&v->banks.raw_value(0x49c4)<36,"Invalid snapshot city coordinates");
    require(v->banks.raw_value(0x6e12)==v->disk,"Snapshot resource disk disagrees with VM state");
    require(std::any_of(p.members_.begin(),p.members_.end(),[](const Character& c){return c.active();}),"Cannot resume a defeated campaign");
    v->background=ExplorationBackground::rebuild(v->disk,v->record,v->banks,v->script);
    return PortSave(v);
}
void PortSave::validate_sources(const IFileSystem& files) const {
    std::set<std::string> actual;for(const auto& name:files.list_assets())if(source_name(name))actual.insert(name);
    require(actual.size()==state_->sources.size(),"Saved source asset set differs from selected game data");
    for(const auto& [name,digest]:state_->sources)require(actual.contains(name)&&sha256(files.read_asset(name))==digest,"Saved source fingerprint differs from selected game data");
}
void PortSave::restore_into(ExplorationSession& s) const {
    validate_sources(s.files_);const auto& v=*state_;
    require(v.text_origin==s.vm.text_origin_,"Saved script source identity differs from selected archive");
    // Native imports own the full saved 0x1e00 image; later map loads use the
    // archive's actual script length. Neither permits an arbitrary shortened VM.
    require(v.loaded_size==s.vm.loaded_size_||v.loaded_size==0x1e00,"Saved script length differs from selected archive");
    require(v.modified||v.script==s.vm.script_,"Unmarked saved script modification");
    auto party=v.party;party.item_catalog_=std::make_shared<ItemCatalog>(ItemCatalog::load(s.files_));
    for(const auto& entry:v.campaign.items) {
        (void)party.item_catalog_->definition(entry.item.type());
        const auto& provenance=entry.item.origin();
        if(provenance.archive_record) {
            const auto disk=static_cast<unsigned>(provenance.file[4]-'0');
            const ItemTemplateId key{disk,*provenance.archive_record,provenance.record_offset/63};
            const auto found=party.item_catalog_->templates().find(key);
            require(found!=party.item_catalog_->templates().end()&&found->second.origin()==provenance,"Campaign item archive provenance does not resolve");
        }
    }
    for(const auto& c:party.members_)for(const auto& item:c.inventory_.items_) {
        (void)party.item_catalog_->definition(item.type());
        const auto& provenance=item.origin();
        if(provenance.archive_record) {
            const auto record=load_record(s.files_,provenance.file,*provenance.archive_record);
            require(provenance.record_offset<=record.size()&&record.size()-provenance.record_offset>=63,"Owned item archive provenance does not resolve");
        }
    }
    ExplorationAssets assets;
    for(std::size_t i=0;i<3;++i)if(v.walls[i]){
        const auto& wall=*v.walls[i];ExplorationAssets loaded;loaded.load_walls(s.files_,wall.disk,wall.slot,wall.record);
        require(loaded.slots_[i].has_value(),"Saved wall source does not contain its bank");assets.slots_[i]=std::move(loaded.slots_[i]);assets.sources_[i]=loaded.sources_[i];assets.universal_=std::move(loaded.universal_);
    }
    std::optional<CityAssets> city;if(v.city){city=CityAssets::load(s.files_);if(v.city_replacement)city->load_replacement_tiles(s.files_);city->cells=*v.city;}
    std::optional<PicturePresentation> picture;if(v.picture){
        const auto& p=*v.picture;picture=PicturePresentation::load(s.files_,p.disk,p.picture,p.head);require(p.frame<picture->frames_.size(),"Saved picture frame is invalid");
        const auto duration=picture->durations_.empty()?0:picture->durations_.at(p.frame);
        require(duration==0?p.elapsed==0:p.elapsed<duration,"Saved picture elapsed time is invalid");picture->frame_=p.frame;picture->elapsed_=p.elapsed;
    }
    std::optional<GameFont> font;if(!v.glyphs.empty())font=GameFont::load(s.files_);
    std::optional<ExplorationSpritePresentation> sprite;
    if(v.sprite)sprite=ExplorationSpritePresentation::load(s.files_,v.sprite->disk,v.sprite->record,v.sprite->distance);
    if(city && (v.banks.raw_value(0x4aa2)!=0 ||
       (v.record==26 && v.banks.raw_value(0x4ab3)==254)))city->apply_river_recovery();
    if(v.background)assets.load_background(s.files_);
    validate_scene(v,assets,city,picture,sprite);
    s.journal_={v.journal,{}};
    s.world.party=std::move(party);s.world.resource_disk=v.disk;s.world.x=v.x;s.world.y=v.y;s.world.facing=v.facing;s.world.map=v.map;
    s.world.tile_query_x=v.tile_query[0];s.world.tile_query_y=v.tile_query[1];s.world.tile_query_value=v.tile_query[2];s.world.tile_query_result=v.tile_query[3];s.world.tile_query_available=v.tile_available;
    s.world.call_parameters=v.call_parameters;
    s.world.background=v.background;
    s.campaign_=v.campaign;
    s.rest_camp_.encounter_ticks=v.rest_encounter_ticks;
    s.assets=std::move(assets);s.city_=std::move(city);s.picture_=std::move(picture);s.font_=std::move(font);
    s.overland_phase_ms_=0;s.overland_presented_=false;s.link_backdrop_.reset();s.link_scene_pending_=false;
    s.link_destination_loaded_=false;s.link_backdrop_overland_=false;
    s.command_notice_.reset();s.command_notice_presented_=false;s.command_notice_elapsed_=0;
    s.overland_colour_.reset();s.overland_entry_colour_done_=true;
    s.sprite_selection_=v.sprite_selection;s.sprite_active_=v.sprite_active;s.sprite_=std::move(sprite);s.sprite_closeup_selector_=v.sprite_closeup_selector;
    s.sprite_cache_.reset();s.sprite_closeup_shown_=false;s.pending_sprite_distance_.reset();
    s.vm.area1_=v.banks.area1;s.vm.area2_=v.banks.area2;s.vm.area3_=v.banks.area3;s.vm.script_=v.script;s.vm.loaded_size_=v.loaded_size;
    Bytes script{0,0};script.insert(script.end(),v.script.begin(),v.script.begin()+static_cast<std::ptrdiff_t>(v.loaded_size));s.vm.entries_=inspect_ecl_header(script);
    s.vm.text_origin_=v.text_origin;s.vm.script_modified_=v.modified;s.vm.area1_dirty_=v.dirty;s.vm.pc_=v.pc;s.vm.flags_=v.flags;s.vm.random_=random_from(v.random);
    s.vm.steps_=v.steps;s.vm.request_sequence_=v.sequence;s.vm.status_=EclStatus::halted;s.vm.stack_.clear();s.vm.pending_.reset();s.vm.pending_followups_.clear();s.vm.trace_.clear();s.vm.error_.clear();
    s.phase_=ExplorationPhase::idle;s.campaign_time_bound_=true;s.arriving_=false;s.party_defeated_=false;s.area_view_=v.area;s.saved_search_=v.saved_search;s.display_text=v.display;s.glyph_text_=v.glyphs;s.messages=v.messages;
}
ExplorationSession::ExplorationSession(const IFileSystem& files,const PortSave& save,IEffectExpiryResolver* expiry)
    :ExplorationSession(files,save.disk(),save.record(),expiry) {save.restore_into(*this);}
}
