#include "gbengine/scripting/ecl_damage_mode.hpp"
#include "gbengine/scripting/ecl_vm.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
#include <limits>
#include <sstream>

namespace GBEngine {
namespace {
constexpr std::uint16_t base=0x9900;
constexpr std::size_t capacity=0x1e00;
void need(bool condition,const char* message) { if (!condition) throw DecodeError(message); }
std::string hex(std::uint16_t value) { std::ostringstream s; s<<"0x"<<std::hex<<value; return s.str(); }
bool character_read(std::uint16_t address) {
    // Original overlay 7 + 0x07bc: all cases that override ordinary area2 words.
    const auto offset=address-0x6b00;
    if(offset>=0xa5 && offset<=0xac) return true;
    switch(offset) {
    case 0x18:case 0x9b:case 0xa0:case 0xb8:case 0xbb:case 0xbd:case 0xbf:
    case 0xc1:case 0xc3:case 0xd8:case 0xf7:case 0xf9:case 0x100:case 0x10c:
    case 0x10d:case 0x11b:case 0x2b1:case 0x2b4:case 0x2cf:case 0x312:return true;
    default:return false;
    }
}
bool character_write(std::uint16_t address) {
    // Original overlay 7 + 0x0a74: do not drop selected-character or host effects.
    const auto offset=address-0x6b00;
    if(offset>=0x20 && offset<=0x70) return true;
    switch(offset) {
    case 0:case 0xb8:case 0xbb:case 0xbd:case 0xbf:case 0xc1:case 0xc3:
    case 0xf7:case 0xf9:case 0x100:case 0x10c:case 0x312:case 0x322:case 0x324:case 0x326:return true;
    default:return false;
    }
}
// GAME.OVR payload 0x74d3 + 0x1ea6: this is the native skip table, including
// its fixed counts for 0x0c/0x34/0x36 and one-byte skips for variable menus.
std::size_t skip_count(std::uint8_t op) {
    switch(op) {
    case 1: case 2: case 10: case 14: case 17: case 18: case 29: case 32:
    case 45: case 50: case 52: case 54: case 56: case 57: case 60: return 1;
    case 3: case 8: case 9: case 12: case 15: case 16: case 31: case 34: return 2;
    case 4: case 5: case 6: case 7: case 11: case 33: case 40: case 42:
    case 47: case 48: case 53: case 55: case 59: return 3;
    case 20: case 35: return 4;
    case 46: return 5;
    case 30: case 44: return 6;
    case 39: return 8;
    case 41: return 14;
    default: return 0;
    }
}
}
std::string_view ecl_status_name(EclStatus s) {
    switch(s) {
    case EclStatus::ready:return "ready"; case EclStatus::waiting:return "waiting";
    case EclStatus::halted:return "halted"; case EclStatus::budget_exhausted:return "budget_exhausted";
    case EclStatus::faulted:return "faulted";
    }
    return "invalid";
}
std::string_view ecl_request_name(EclRequestKind k) {
    switch(k) {
    case EclRequestKind::add_monsters:return "add_monsters";
    case EclRequestKind::combat:return "combat";
    case EclRequestKind::encounter_dialogue:return "encounter_dialogue";
    case EclRequestKind::setup_sprite:return "setup_sprite";
    case EclRequestKind::approach_sprite:return "approach_sprite";
    case EclRequestKind::select_current:return "select_current";
    case EclRequestKind::restore_current:return "restore_current";
    case EclRequestKind::add_npc:return "add_npc";
    case EclRequestKind::engine_service:return "engine_service";
    case EclRequestKind::clear_treasure:return "clear_treasure";
    case EclRequestKind::treasure_setup:return "treasure_setup";
    case EclRequestKind::campaign_service:return "campaign_service";
    case EclRequestKind::remove_wealth:return "remove_wealth";
    case EclRequestKind::print:return "print"; case EclRequestKind::picture:return "picture";
    case EclRequestKind::load_files:return "load_files"; case EclRequestKind::load_pieces:return "load_pieces";
    case EclRequestKind::delay:return "delay"; case EclRequestKind::clear_box:return "clear_box";
    case EclRequestKind::sprite_off:return "sprite_off"; case EclRequestKind::write_value:return "write_value";
    case EclRequestKind::write_string:return "write_string";
    case EclRequestKind::native_call:return "native_call";
    case EclRequestKind::menu:return "menu";
    case EclRequestKind::select_character:return "select_character";
    case EclRequestKind::load_ecl:return "load_ecl";
    case EclRequestKind::damage:return "damage";
    case EclRequestKind::input_number:return "input_number";
    case EclRequestKind::input_string:return "input_string";
    case EclRequestKind::glyph_table:return "glyph_table";
    case EclRequestKind::advance_time:return "advance_time";
    }
    return "invalid";
}
std::string_view ecl_opcode_name(std::uint8_t op) {
    switch(op) {
    case 0x00:return "EXIT"; case 0x01:return "GOTO"; case 0x02:return "GOSUB";
    case 0x0a:return "SELECT_CURRENT";case 0x38:return "ENGINE_SERVICE";
    case 0x0c:return "SETUP_EXPLORATION_SPRITE";case 0x0d:return "APPROACH_EXPLORATION_SPRITE";
    case 0x03:return "COMPARE"; case 0x04:return "ADD"; case 0x05:return "SUBTRACT";
    case 0x06:return "DIVIDE"; case 0x07:return "MULTIPLY"; case 0x08:return "RANDOM";
    case 0x09:return "SAVE"; case 0x0e:return "PICTURE"; case 0x11:return "PRINT";
    case 0x12:return "PRINTCLEAR"; case 0x13:return "RETURN"; case 0x14:return "COMPARE_AND";
    case 0x16:return "IF_EQ"; case 0x17:return "IF_NE"; case 0x18:return "IF_LT";
    case 0x19:return "IF_GT"; case 0x1a:return "IF_LE"; case 0x1b:return "IF_GE";
    case 0x20:return "LOAD_ECL";
    case 0x21:return "LOAD_FILES"; case 0x25:return "ON_GOTO"; case 0x26:return "ON_GOSUB";
    case 0x2a:return "GET_TABLE"; case 0x2f:return "AND"; case 0x30:return "OR";
    case 0x31:return "SPRITE_OFF"; case 0x35:return "SAVE_TABLE"; case 0x37:return "LOAD_PIECES";
    case 0x3a:return "DELAY"; case 0x3d:return "CLEAR_BOX";
    case 0x2d:return "CALL";
    case 0x2b:return "MENU";
    case 0x39:return "SELECT_CHARACTER";
    case 0x2e:return "DAMAGE";
    case 0x15:return "LIST_MENU";
    case 0x2c:return "PARLAY_MENU";
    case 0x0f:return "INPUT_NUMBER";case 0x10:return "INPUT_STRING";
    case 0x33:return "NEWLINE";case 0x3c:return "PRINT_CHAR_TABLE";
    case 0x1d:return "PARTY_STRENGTH";case 0x1e:return "PARTY_QUERY";case 0x22:return "PARTY_CLASS_CHECK";
    case 0x32:return "HAS_ITEM_TYPE";case 0x34:return "ADVANCE_TIME";case 0x36:return "ADD_NPC";
    case 0x0b:return "ADD_MONSTERS";
    case 0x23:return "SURPRISE";
    case 0x29:return "ENCOUNTER";
    case 0x1c:return "CLEAR_TREASURE_SETUP";case 0x24:return "CAMPAIGN_SERVICE";
    case 0x27:return "TREASURE";case 0x28:return "REMOVE_WEALTH";
    default:return "UNSUPPORTED";
    }
}
EclVm::EclVm(std::span<const std::uint8_t> record,std::uint32_t seed,const IEclBindings* bindings)
    : entries_(inspect_ecl_header(record)),bindings_(bindings),random_(seed) {
    loaded_size_=record.size()-2;
    text_origin_=StringStore::active().record_origin(record);
    script_.assign(capacity,0);
    std::ranges::copy(record.subspan(2),script_.begin());
    start_entry(4);
}
void EclVm::valid_target(std::uint16_t address) const {
    need(address>=base && static_cast<std::size_t>(address-base)<loaded_size_,"ECL target outside loaded script");
}
void EclVm::start_entry(std::size_t i) {
    start_at(entries_.addresses.at(i)); }
void EclVm::start_at(std::uint16_t address) {
    valid_target(address);
    area1_numeric_writes_.reset();
    pc_=address; flags_.fill(false); stack_.clear(); pending_.reset();pending_followups_.clear(); trace_.clear();
    steps_=0; error_.clear(); status_=EclStatus::ready;
    invocation_selection_=bindings_?bindings_->party_selected_identity():std::nullopt;restore_selection_=false;
}
std::uint8_t EclVm::fetch(std::size_t& cursor) const {
    need(cursor<loaded_size_,"Truncated ECL instruction");
    return script_[cursor++];
}
std::uint16_t EclVm::cursor_address(std::size_t cursor) const {
    need(cursor<=capacity,"ECL instruction exceeds script region");
    return static_cast<std::uint16_t>(base+cursor);
}
EclOperand EclVm::operand(std::size_t& cursor) const {
    EclOperand out{};
    const auto tag_address=cursor_address(cursor);
    out.tag=fetch(cursor);
    const auto low=fetch(cursor);
    out.raw=low;
    switch(out.tag) {
    case 0: break;
    case 1: case 2: case 3: case 0x81:
        out.raw=static_cast<std::uint16_t>(low|(fetch(cursor)<<8));
        if (out.tag==0x81) {
            out.text=read_string(out.raw);
            out.text_id=!script_modified_ && !text_origin_.starts_with("runtime:") && locate_script_value(out.raw).region==ScriptRegion::script?
                StringStore::active().reference_id(text_origin_,out.raw):StringStore::active().runtime_id(out.text);
        }
        break;
    case 0x80: {
        need(low<=loaded_size_-cursor,"Truncated packed ECL string");
        out.text_id=StringStore::active().ecl_id(text_origin_,tag_address,std::span<const std::uint8_t>(script_).subspan(cursor,low),!script_modified_);
        out.text=StringStore::active().get(out.text_id);
        need(out.text.size()<=255,"ECL string exceeds native capacity");
        cursor+=low;
        break;
    }
    default: throw DecodeError("Unsupported ECL operand tag "+hex(out.tag));
    }
    return out;
}
std::vector<EclOperand> EclVm::operands(std::size_t& cursor,std::size_t count) const {
    need(count<=63,"ECL operand list exceeds native table capacity");
    std::vector<EclOperand> out;
    for (std::size_t i=0;i<count;++i) out.push_back(operand(cursor));
    return out;
}
std::uint16_t EclVm::value(const EclOperand& op) const {
    need(!op.is_string(),"String used where a numeric operand is required");
    if (op.tag==0 || op.tag==2) return op.raw;
    return read_value(op.raw);
}
std::uint16_t EclVm::read_value(std::uint16_t address) const {
    const auto loc=locate_script_value(address);
    switch(loc.region) {
    case ScriptRegion::area1:return area1_[loc.byte_offset/2];
    case ScriptRegion::area3:return area3_[loc.byte_offset/2];
    case ScriptRegion::script:return script_[loc.byte_offset];
    case ScriptRegion::area2:
        if(!character_read(address)) return area2_[loc.byte_offset/2];
        [[fallthrough]];
    case ScriptRegion::special:
        if (bindings_) if (const auto result=bindings_->read_value(address)) return *result;
        throw DecodeError("Unresolved game-state read at "+hex(address));
    }
    throw DecodeError("Invalid script region");
}
void EclVm::restore_state(const EclStateBanks& banks,std::span<const std::uint8_t> image) {
    need(status_!=EclStatus::waiting,"Cannot restore while a host request is pending");
    need(image.size()==capacity,"Saved ECL image must be exactly 7680 bytes");
    Bytes record{0,0};record.insert(record.end(),image.begin(),image.end());
    const auto entries=inspect_ecl_header(record);
    Bytes script(image.begin(),image.end());
    // Keep original source IDs when the saved image equals the archive image.
    const bool modified=script_modified_ || script!=script_;
    area1_=banks.area1;area2_=banks.area2;area3_=banks.area3;
    script_=std::move(script);entries_=entries;loaded_size_=capacity;script_modified_=modified;
    area1_dirty_=false;start_entry(4);
}
void EclVm::set_portraits_enabled(bool enabled) {
    if(status_!=EclStatus::halted || pending_)
        throw DecodeError("Picture preferences require a quiescent interpreter");
    auto& flags=area1_[0xff];
    flags=static_cast<std::uint16_t>((flags&0xfffeu)|(enabled?1u:0u));
}
void EclVm::set_monsters_enabled(bool enabled) {
    if(status_!=EclStatus::halted || pending_)
        throw DecodeError("Picture preferences require a quiescent interpreter");
    // Native import is resident49F6=lowbyte(VM49FF>>1), not just bit1.
    // Alter canonicalizes that byte to0/1. Retain Portraits and opaque bits9..15.
    auto& flags=area1_[0xff];
    flags=static_cast<std::uint16_t>((flags&0xfe01u)|(enabled?2u:0u));
}
void EclVm::seed_value(std::uint16_t address,std::uint16_t v) {
    need(status_!=EclStatus::waiting,"Cannot seed state while a host request is pending");
    const auto loc=locate_script_value(address);
    if (loc.region==ScriptRegion::area1) area1_[loc.byte_offset/2]=v;
    else if (loc.region==ScriptRegion::area2) area2_[loc.byte_offset/2]=v;
    else if (loc.region==ScriptRegion::area3) area3_[loc.byte_offset/2]=v;
    else throw DecodeError("Fixture seeding requires a state-bank address");
}
std::string EclVm::read_string(std::uint16_t address) const {
    if (address==0x6b00) {
        if (bindings_) if (const auto name=bindings_->read_character_name()) {
            need(name->size()<=255,"Character name exceeds native string capacity");
            return *name;
        }
        throw DecodeError("Selected character name is unresolved");
    }
    const auto region=locate_script_value(address).region;
    need(region!=ScriptRegion::special,"Unsupported string source region");
    if(region==ScriptRegion::script && !script_modified_ && !text_origin_.starts_with("runtime:"))
        return StringStore::active().get(StringStore::active().reference_id(text_origin_,address));
    std::string out;
    for (std::uint32_t i=0;i<=255;++i) {
        const auto at=static_cast<std::uint32_t>(address)+i;
        need(at<=65535 && locate_script_value(static_cast<std::uint16_t>(at)).region==region,
             "ECL string crosses storage region");
        const auto location=locate_script_value(static_cast<std::uint16_t>(at));
        // Native string copying reads raw bank words; only 6b00 is a character name.
        const auto v=region==ScriptRegion::area2 ? area2_[location.byte_offset/2] : read_value(static_cast<std::uint16_t>(at));
        if (!v) return StringStore::active().runtime_text(out);
        need(i<255,"Unterminated ECL string");
        out.push_back(static_cast<char>(v&255));
    }
    throw DecodeError("Unterminated ECL string");
}
void EclVm::compare(std::uint16_t left,std::uint16_t right) {
    flags_={left==right,left!=right,left<right,left>right,left<=right,left>=right};
}
void EclVm::request(EclRequestKind kind,std::uint8_t op,std::size_t next,
                    std::vector<std::uint16_t> values,std::string text) {
    pending_=EclRequest{++request_sequence_,kind,pc_,op,std::move(values),std::move(text),std::nullopt};
    pending_next_=cursor_address(next);
    status_=EclStatus::waiting;
}
void EclVm::write_value(std::uint16_t address,std::uint16_t v,std::uint8_t op,std::size_t next) {
    const auto loc=locate_script_value(address);
    switch(loc.region) {
    case ScriptRegion::area1:
        area1_[loc.byte_offset/2]=v;area1_numeric_writes_.set(loc.byte_offset/2);
        if (address==0x49fd || address==0x49fe) area1_dirty_=true;
        break;
    case ScriptRegion::area3:area3_[loc.byte_offset/2]=v;break;
    case ScriptRegion::script:script_[loc.byte_offset]=static_cast<std::uint8_t>(v);script_modified_=true;break;
    case ScriptRegion::area2:
        if(!character_write(address)) { area2_[loc.byte_offset/2]=v;break; }
        [[fallthrough]];
    case ScriptRegion::special:
        request(EclRequestKind::write_value,op,next,{address,v});break;
    }
}
void EclVm::write_values(std::span<const EclStateUpdate> updates,std::uint8_t op,std::size_t next) {
    for(std::size_t i=0;i<updates.size();++i) {
        write_value(updates[i].address,updates[i].value,op,next);
        if(status_==EclStatus::waiting) {
            pending_followups_.assign(updates.begin()+static_cast<std::ptrdiff_t>(i+1),updates.end());
            return;
        }
    }
}
void EclVm::write_string(std::uint16_t address,std::string_view text,std::uint8_t op,std::size_t next) {
    need(text.size()<=255,"ECL string exceeds native capacity");
    const auto region=locate_script_value(address).region;
    if (address==0x6b00) {
        request(EclRequestKind::write_string,op,next,{address},std::string(text));return;
    }
    need(region!=ScriptRegion::special,"Unsupported string destination region");
    for (std::size_t i=0;i<=text.size();++i) {
        const auto at=static_cast<std::size_t>(address)+i;
        need(at<=65535 && locate_script_value(static_cast<std::uint16_t>(at)).region==region,
             "ECL string write crosses storage region");
    }
    for (std::size_t i=0;i<=text.size();++i) {
        const auto location=locate_script_value(static_cast<std::uint16_t>(address+i));
        const auto v=i==text.size()?std::uint16_t{0}:static_cast<std::uint16_t>(static_cast<unsigned char>(text[i]));
        // Native string writes bypass numeric setters and their dirty/character effects.
        if(region==ScriptRegion::area1) area1_[location.byte_offset/2]=v;
        else if(region==ScriptRegion::area2) area2_[location.byte_offset/2]=v;
        else if(region==ScriptRegion::area3) area3_[location.byte_offset/2]=v;
        else {script_[location.byte_offset]=static_cast<std::uint8_t>(v);script_modified_=true;}
    }
}
void EclVm::execute() {
    valid_target(pc_);
    std::size_t cursor=pc_-base;
    const auto op=fetch(cursor);
    auto args=[&](std::size_t count) { return operands(cursor,count); };
    auto set_target=[&](std::uint16_t target) { valid_target(target); cursor=target-base; };
    switch(op) {
    case 0x0c: {
        const auto a=args(3);
        const auto sprite=static_cast<std::uint8_t>(value(a[0]));
        const auto maximum=value(a[1]);
        // Native03B4 publishes operand2 at VM6DC0 before evaluating operand3.
        // Preview that dependency without committing partial setup on a load failure.
        auto staged=*this;staged.area2_[0x2c0]=maximum;
        const auto body=static_cast<std::uint8_t>(staged.value(a[2]));
        request(EclRequestKind::setup_sprite,op,cursor,{sprite,maximum,body});break;
    }
    case 0x0d: {
        const auto distance=read_value(0x6dc1);
        if(distance)request(EclRequestKind::approach_sprite,op,cursor,{distance,static_cast<std::uint16_t>(distance-1)});
        break;
    }
    case 0x0b: {
        const auto a=args(3);std::vector<std::uint16_t> encoded;
        for(const auto& parameter:a) {encoded.push_back(parameter.tag);encoded.push_back(parameter.raw);}
        request(EclRequestKind::add_monsters,op,cursor,std::move(encoded));break;
    }
    case 0x1c:request(EclRequestKind::clear_treasure,op,cursor);break;
    case 0x27: {
        const auto a=args(8);std::vector<std::uint16_t> values;
        for(const auto& operand:a) values.push_back(value(operand));
        request(EclRequestKind::treasure_setup,op,cursor,std::move(values));break;
    }
    case 0x24:request(EclRequestKind::campaign_service,op,cursor);break;
    case 0x29: {
        const auto a=args(14);
        EncounterDialogueConfig config;
        config.sprite=static_cast<std::uint8_t>(value(a[0]));
        config.maximum_distance=value(a[1]);
        // Native direct store precedes operand3 and all response getters.
        // A later reference to6DC0 observes the new full-word maximum.
        area2_[0x2c0]=config.maximum_distance;
        config.body=static_cast<std::uint8_t>(value(a[2]));
        need(!a[3].is_string(),"Invalid ENCOUNTER destination");config.destination=a[3].raw;
        for(std::size_t i=0;i<5;++i)config.policies[i]=static_cast<std::uint8_t>(value(a[4+i]));
        // The native decoder retains its three string operands separately.
        // Reject other layouts rather than borrowing stale global text buffers.
        for(std::size_t i=0;i<3;++i) {
            need(a[9+i].is_string(),"ENCOUNTER requires three retained text operands");
            config.texts[i]=a[9+i].text;
        }
        config.flee_threshold=static_cast<std::uint8_t>(value(a[12]));
        config.monster_flee_threshold=static_cast<std::uint8_t>(value(a[13]));
        request(EclRequestKind::encounter_dialogue,op,cursor);pending_->encounter=std::move(config);break;
    }
    case 0x28: {
        const auto a=args(3);
        // The native handler decodes three operands, but evaluates operand 2
        // twice and never reads operand 3. Do not trigger a third binding read.
        const auto target=static_cast<std::uint8_t>(value(a[0]));
        const auto percent=static_cast<std::uint8_t>(value(a[1]));
        need(percent==static_cast<std::uint8_t>(value(a[1])),"Unstable wealth percentage binding");
        request(EclRequestKind::remove_wealth,op,cursor,{target,percent});break;
    }
    case 0x00:
        if(restore_selection_) request(EclRequestKind::restore_current,op,cursor);
        else {stack_.clear();status_=EclStatus::halted;}
        break;
    case 0x0a: {
        const auto a=args(1);
        request(EclRequestKind::select_current,op,cursor,{static_cast<std::uint8_t>(value(a[0]))});break;
    }
    case 0x36: {
        const auto a=args(2);need(!a[1].is_string(),"Invalid NPC disposition parameter");
        request(EclRequestKind::add_npc,op,cursor,{static_cast<std::uint8_t>(value(a[0])),a[1].tag,a[1].raw});break;
    }
    case 0x38: {
        const auto a=args(1);need(!a[0].is_string(),"Invalid engine service parameter");
        request(EclRequestKind::engine_service,op,cursor,{a[0].tag,a[0].raw});break;
    }
    case 0x01:case 0x02: {
        const auto a=args(1);
        need(a[0].tag!=0x80,"Invalid branch operand");
        valid_target(a[0].raw);
        if (op==2) {
            need(stack_.size()<256,"ECL call stack limit reached");
            stack_.push_back(cursor_address(cursor));
        }
        set_target(a[0].raw);break;
    }
    case 0x03: {
        const auto a=args(2);
        if (a[0].is_string() || a[1].is_string()) {
            need(a[0].is_string() && a[1].is_string(),"Mixed numeric/string comparison is unresolved");
            const auto less=[](const std::string& x,const std::string& y) {
                return std::lexicographical_compare(x.begin(),x.end(),y.begin(),y.end(),
                    [](unsigned char l,unsigned char r) { return l<r; });
            };
            const auto& l=a[0].text; const auto& r=a[1].text;
            flags_={l==r,l!=r,less(l,r),less(r,l),!less(r,l),!less(l,r)};
        } else compare(value(a[0]),value(a[1]));
        break;
    }
    case 0x04:case 0x05:case 0x06:case 0x07:case 0x2f:case 0x30: {
        const auto a=args(3);
        const std::uint32_t l=value(a[0]),r=value(a[1]);
        need(a[2].tag!=0x80,"Invalid numeric destination");
        std::uint32_t result{};
        switch(op) {
        case 4:result=l+r;break; case 5:result=r-l;break;
        case 6:need(r!=0,"ECL division by zero");result=l/r;break;
        case 7:result=l*r;break;
        case 0x2f:result=(l&r)&255;compare(0,static_cast<std::uint16_t>(result));break;
        default:result=(l|r)&255;compare(0,static_cast<std::uint16_t>(result));break;
        }
        write_value(a[2].raw,static_cast<std::uint16_t>(result),op,cursor);break;
    }
    case 0x08: {
        const auto a=args(2);
        const auto maximum=value(a[0])&255u;
        const std::uint32_t bound=maximum==255 ? 255 : maximum+1;
        const std::uint32_t threshold=(std::uint32_t{0}-bound)%bound;
        std::uint32_t sample;
        do { sample=static_cast<std::uint32_t>(random_()); } while(sample<threshold);
        need(a[1].tag!=0x80,"Invalid RANDOM destination");
        write_value(a[1].raw,static_cast<std::uint16_t>(sample%bound),op,cursor);break;
    }
    case 0x23: {
        const auto a=args(4);
        std::array<std::uint8_t,4> parameters{};
        for(std::size_t i=0;i<parameters.size();++i)parameters[i]=static_cast<std::uint8_t>(value(a[i]));
        const auto signed_byte=[](unsigned v) {v&=255u;return v<128u?static_cast<int>(v):static_cast<int>(v)-256;};
        const auto first=signed_byte(parameters[3]+258u-parameters[0]);
        const auto second=signed_byte(parameters[1]+258u-parameters[2]);
        const auto roll=[&] {
            constexpr std::uint32_t bound=6,threshold=(std::uint32_t{0}-bound)%bound;
            std::uint32_t sample;do {sample=static_cast<std::uint32_t>(random_());}while(sample<threshold);
            return static_cast<int>(sample%bound+1);
        };
        const auto first_roll=roll();const auto second_roll=roll();
        // 2182:16C9 performs two independent tests. The second overwrites
        // tentative result3, so the native routine can return only0/1/2.
        const auto result=second_roll<=second?2u:first_roll<=first?1u:0u;
        write_value(0x6dcb,static_cast<std::uint16_t>(result),op,cursor);break;
    }
    case 0x09: {
        const auto a=args(2);
        need(a[1].tag!=0x80,"Invalid SAVE destination");
        if (a[0].is_string()) write_string(a[1].raw,a[0].text,op,cursor);
        else write_value(a[1].raw,value(a[0]),op,cursor);
        break;
    }
    case 0x1d: {
        const auto a=args(1);need(!a[0].is_string(),"Invalid PARTY_STRENGTH destination");
        const auto result=bindings_?bindings_->party_strength():std::nullopt;
        need(result.has_value(),"PARTY_STRENGTH requires an imported party");
        write_value(a[0].raw,*result,op,cursor);break;
    }
    case 0x22: {
        const auto a=args(2);
        need(!a[0].is_string() && !a[1].is_string(),"Invalid PARTY_CLASS_CHECK destination");
        const auto result=bindings_?bindings_->party_field_2f_check():std::nullopt;
        need(result.has_value(),"PARTY_CLASS_CHECK requires an imported party");
        const std::array updates{EclStateUpdate{a[0].raw,*result},EclStateUpdate{a[1].raw,0}};
        write_values(updates,op,cursor);
        break;
    }
    case 0x1e: {
        const auto a=args(6);
        need(!a[0].is_string(),"Invalid PARTY_QUERY mode");
        for(std::size_t i=2;i<6;++i) need(!a[i].is_string(),"Invalid PARTY_QUERY destination");
        // A tag-one selector identifies a field by its encoded address. Other
        // selectors are evaluated. The native field modes are deliberately
        // restricted to the two recovered aggregate paths.
        const auto mode=a[0].tag==1?a[0].raw:value(a[0]);
        const auto code=static_cast<std::uint8_t>(value(a[1]));
        std::array<std::uint16_t,4> outputs{};
        if(mode==0) {
            const auto found=bindings_?bindings_->party_has_effect(code):std::nullopt;
            need(found.has_value(),"PARTY_QUERY requires restored party effects");
            outputs[3]=static_cast<std::uint16_t>(*found);
        } else {
            need(mode==0x6ba7 || mode==0x6c1b,"Unresolved PARTY_QUERY aggregate mode");
            const auto summary=bindings_?bindings_->party_field_summary(mode):std::nullopt;
            need(summary.has_value(),"PARTY_QUERY requires an imported party summary");
            std::copy(summary->begin(),summary->end(),outputs.begin());
        }
        // Raw 2182:1516 clears the first three outputs. Helper13D7 writes
        // them before the fourth Boolean, retaining aliases and host effects.
        const std::array updates{EclStateUpdate{a[2].raw,outputs[0]},EclStateUpdate{a[3].raw,outputs[1]},
            EclStateUpdate{a[4].raw,outputs[2]},EclStateUpdate{a[5].raw,outputs[3]}};
        write_values(updates,op,cursor);
        break;
    }
    case 0x32: {
        const auto a=args(1);
        const auto type=static_cast<std::uint8_t>(value(a[0]));
        const auto result=bindings_?bindings_->party_has_item_type(type):std::nullopt;
        need(result.has_value(),"HAS_ITEM_TYPE requires an imported party");
        // Native 2182:266A clears all six comparison flags, then sets EQ/NE.
        flags_={*result,!*result,false,false,false,false};break;
    }
    case 0x0f:case 0x10: {
        const auto a=args(2);
        // INPUT_STRING accepts a string-reference destination (81), as used
        // by Slums AF9F. It is an address, not a packed literal (80).
        need(!a[1].is_string() || (op==0x10 && a[1].tag==0x81),"Invalid input destination");
        // Native handlers consume but do not use operand 1; operand 2 is encoded.
        request(op==0x0f?EclRequestKind::input_number:EclRequestKind::input_string,op,cursor,{a[1].raw});break;
    }
    case 0x0e: {
        const auto a=args(1);
        request(EclRequestKind::picture,op,cursor,{static_cast<std::uint16_t>(value(a[0])&255)});break;
    }
    case 0x11:case 0x12: {
        const auto a=args(1);
        const auto text=a[0].is_string() ? a[0].text : std::to_string(value(a[0]));
        request(EclRequestKind::print,op,cursor,{},text);pending_->text_id=a[0].text_id;break;
    }
    case 0x33:request(EclRequestKind::print,op,cursor,{},"\n");break;
    case 0x34: {
        const auto a=operand(cursor);
        request(EclRequestKind::advance_time,op,cursor,{static_cast<std::uint16_t>(value(a)&255)});break;
    }
    case 0x3c: {
        const auto a=args(1);need(a[0].tag!=0x80,"Invalid character-table source");
        std::vector<std::uint16_t> glyphs;bool terminated=false;
        for(unsigned i=0;i<256;++i) {
            const auto glyph=static_cast<std::uint16_t>((read_value(static_cast<std::uint16_t>(a[0].raw+i))+1)&255);
            if(glyph<2) {terminated=true;break;}
            glyphs.push_back(glyph);
        }
        need(terminated,"Unterminated character table (native byte index would wrap)");
        request(EclRequestKind::glyph_table,op,cursor,std::move(glyphs));break;
    }
    case 0x13:
        if (!stack_.empty()) { set_target(stack_.back());stack_.pop_back(); }
        break;
    case 0x14: {
        const auto a=args(4);
        const auto v0=value(a[0]),v1=value(a[1]),v2=value(a[2]),v3=value(a[3]);
        const bool equal=v0==v1 && v2==v3;
        flags_={equal,!equal,false,false,false,false};break;
    }
    case 0x16:case 0x17:case 0x18:case 0x19:case 0x1a:case 0x1b:
        if (!flags_[op-0x16]) {
            // Native skip does not recursively execute chained IFs or variable menus.
            const auto skipped=fetch(cursor);
            operands(cursor,skip_count(skipped));
        }
        break;
    case 0x20: {
        const auto a=operand(cursor);
        request(EclRequestKind::load_ecl,op,cursor,{static_cast<std::uint16_t>(value(a)&255)});break;
    }
    case 0x21:case 0x37: {
        const auto a=args(3);
        std::vector<std::uint16_t> values;
        for (const auto& v:a) values.push_back(static_cast<std::uint16_t>(value(v)&255));
        request(op==0x21?EclRequestKind::load_files:EclRequestKind::load_pieces,op,cursor,std::move(values));break;
    }
    case 0x25:case 0x26: {
        const auto head=args(2);
        const auto selector=static_cast<std::size_t>(value(head[0])&255);
        const auto count=value(head[1])&255;
        const auto targets=args(count);
        if (selector<targets.size()) {
            const auto& target=targets[selector];
            need(target.tag!=0x80,"Invalid computed branch target");
            valid_target(target.raw);
            if (op==0x26) {
                need(stack_.size()<256,"ECL call stack limit reached");
                stack_.push_back(cursor_address(cursor));
            }
            set_target(target.raw);
        }
        break;
    }
    case 0x2a: {
        const auto a=args(3);
        need(a[0].tag!=0x80 && a[2].tag!=0x80,"Invalid GET_TABLE address");
        const auto source=static_cast<std::uint16_t>(a[0].raw+(value(a[1])&255));
        write_value(a[2].raw,read_value(source),op,cursor);break;
    }
    case 0x15: {
        const auto head=args(3);
        need(!head[0].is_string() && head[1].is_string(),"Invalid list-menu destination or prompt");
        const auto count=value(head[2])&255;
        need(count>=1 && count<=63,"List-menu option count must be 1..63");
        const auto strings=args(count);
        EclMenu menu;menu.destination=head[0].raw;menu.vertical=true;menu.prompt=head[1].text;
        for(const auto& label:strings) {
            need(label.is_string(),"List-menu labels must be strings");
            menu.labels.push_back(StringStore::active().runtime_text(label.text.substr(0,40))); // Native linked-list string[40].
        }
        request(EclRequestKind::menu,op,cursor,{},menu.prompt);
        pending_->text_id=head[1].text_id;pending_->menu=std::move(menu);break;
    }
    case 0x2b: {
        const auto head=args(2);
        need(!head[0].is_string(),"Invalid MENU destination");
        const auto count=value(head[1])&255;
        need(count>=1 && count<=63,"MENU option count must be 1..63");
        const auto strings=args(count);
        std::string joined;
        for(std::size_t i=0;i<strings.size();++i) {
            need(strings[i].is_string(),"MENU labels must be strings");
            auto label=strings[i].text;
            if(count==1 && label==overlay_string(0x1dcb)) label=overlay_string(0x1df0);
            joined+='~';joined+=label;
            if(i+1<strings.size()) joined+=' ';
            if(joined.size()>50) joined.resize(50); // Native Pascal string[50].
        }
        EclMenu menu;menu.destination=head[0].raw;menu.allow_enter=count==1;
        std::vector<std::size_t> starts;
        for(std::size_t i=0;i<joined.size();++i) {
            auto c=static_cast<unsigned char>(joined[i]);
            if((c>='A' && c<='Z') || (c>='0' && c<='9')) joined[i]=static_cast<char>(c+32);
            if(c=='~') {
                joined.erase(i,1);need(i<joined.size(),"MENU hotkey truncated");
                c=static_cast<unsigned char>(joined[i]);
                if(c>='a' && c<='z') c=static_cast<unsigned char>(c-32);
                need((c>='A' && c<='Z') || (c>='0' && c<='9'),"Unsupported MENU hotkey");
                need(std::find(menu.hotkeys.begin(),menu.hotkeys.end(),c)==menu.hotkeys.end(),"Duplicate MENU hotkey");
                joined[i]=static_cast<char>(c);menu.hotkeys.push_back(c);starts.push_back(i);
            }
        }
        need(!starts.empty(),"MENU has no selectable labels");
        for(std::size_t i=0;i<starts.size();++i) {
            auto label=joined.substr(starts[i],(i+1<starts.size()?starts[i+1]:joined.size())-starts[i]);
            while(!label.empty() && label.back()==' ') label.pop_back();
            menu.labels.push_back(StringStore::active().runtime_text(label));
        }
        request(EclRequestKind::menu,op,cursor);
        pending_->menu=std::move(menu);break;
    }
    case 0x2c: {
        const auto a=args(6);
        need(!a[5].is_string(),"Invalid PARLAY destination");
        EclMenu menu;menu.destination=a[5].raw;
        const auto& joined=overlay_string(0x32ee);
        for(std::size_t at=0;at<joined.size();++at) if(joined[at]=='~') {
            const auto end=joined.find('~',at+1);auto label=joined.substr(at+1,end==std::string::npos?std::string::npos:end-at-1);
            while(!label.empty() && label.back()==' ') label.pop_back();
            need(!label.empty(),"Invalid extracted parlay label");menu.hotkeys.push_back(static_cast<std::uint8_t>(label.front()));
            for(std::size_t i=1;i<label.size();++i) if(label[i]>='A' && label[i]<='Z') label[i]=static_cast<char>(label[i]+32);
            menu.labels.push_back(StringStore::active().runtime_text(label));
        }
        need(menu.labels.size()==5,"Invalid extracted parlay menu");
        for(std::size_t i=0;i<5;++i) menu.responses.push_back(static_cast<std::uint16_t>(value(a[i])&255));
        request(EclRequestKind::menu,op,cursor);pending_->menu=std::move(menu);break;
    }
    case 0x35: {
        const auto a=args(3);
        need(a[1].tag!=0x80,"Invalid SAVE_TABLE address");
        const auto destination=static_cast<std::uint16_t>(a[1].raw+value(a[2]));
        write_value(destination,value(a[0]),op,cursor);break;
    }
    case 0x2e: {
        const auto a=args(5);std::vector<std::uint16_t> values;
        for(const auto& argument:a) values.push_back(static_cast<std::uint16_t>(value(argument)&255));
        request(EclRequestKind::damage,op,cursor,std::move(values));break;
    }
    case 0x31:request(EclRequestKind::sprite_off,op,cursor);break;
    case 0x2d: {
        const auto a=args(1);
        need(!a[0].is_string(),"Invalid native CALL operand");
        // Native 27a9:06af joins the encoded bytes, without a virtual read.
        request(EclRequestKind::native_call,op,cursor,{a[0].raw});break;
    }
    case 0x3a:request(EclRequestKind::delay,op,cursor);break;
    case 0x3b: {
        const auto a=args(3);
        // 2182:2C03 evaluates the needle first, but joins the encoded
        // destination bytes directly and then discards each high byte.
        (void)value(a[0]);
        need(!a[1].is_string() && !a[2].is_string(),"Invalid FIND_CHARACTER_FIELD destinations");
        need((a[1].raw&255)==0x7d && (a[2].raw&255)==0x7e,
             "Unsupported stack-dependent FIND_CHARACTER_FIELD destinations");
        // Actual ECL3/14: 007D/007E reach no case in 27A9:0C77.
        // Both setters are inert regardless of the inherited scan flag.
        // Do not invent that flag or publish intended party-search results.
        break;
    }
    case 0x39: {
        const auto a=args(1);need(a[0].is_string(),"Character selection requires a string prompt");
        request(EclRequestKind::select_character,op,cursor,{},a[0].text);break;
    }
    case 0x3d:request(EclRequestKind::clear_box,op,cursor);break;
    default:throw DecodeError("Unsupported ECL opcode "+hex(op));
    }
    if (status_!=EclStatus::waiting) pc_=cursor_address(cursor);
}
EclStatus EclVm::run(std::size_t budget) {
    need(budget>0 && budget<=1000000,"Instruction budget must be 1..1000000");
    if (status_==EclStatus::waiting || status_==EclStatus::halted || status_==EclStatus::faulted) return status_;
    status_=EclStatus::ready;
    for (std::size_t i=0;i<budget;++i) {
        const auto address=pc_;
        const auto opcode=(address>=base && static_cast<std::size_t>(address-base)<loaded_size_) ? script_[address-base] : std::uint8_t{255};
        // Bounded copy-on-step: malformed instructions cannot partially change banks,
        // the stack, random state or PC. Bindings are read-only and host writes yield.
        EclVm trial=*this;
        try {
            trial.execute();
            ++trial.steps_;
            *this=std::move(trial);
        } catch (const StringError&) {throw;}
        catch (const std::exception& e) {
            status_=EclStatus::faulted;
            error_="ECL "+hex(address)+" opcode "+hex(opcode)+": "+e.what();
        }
        if (trace_.size()<trace_limit_) trace_.push_back({address,opcode,status_==EclStatus::waiting?pending_next_:pc_,status_});
        if (status_!=EclStatus::ready) return status_;
    }
    status_=EclStatus::budget_exhausted;
    return status_;
}
void EclVm::complete_request(std::uint64_t token,
    std::span<const EclStateUpdate> updates) {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token,"Stale or unsolicited ECL host acknowledgement");
    need(pending_->kind!=EclRequestKind::menu,"MENU requires an explicit answer");
    need(pending_->kind!=EclRequestKind::input_number && pending_->kind!=EclRequestKind::input_string,"Input requires an explicit answer");
    need(pending_->kind!=EclRequestKind::load_ecl,"LOAD_ECL requires script replacement");
    need(pending_->kind!=EclRequestKind::damage,"DAMAGE requires a typed resolution");
    need(pending_->kind!=EclRequestKind::advance_time,"ADVANCE_TIME requires a typed resolution");
    need(pending_->kind!=EclRequestKind::add_monsters,"ADD_MONSTERS requires a staged encounter resolution");
    need(pending_->kind!=EclRequestKind::combat,"Combat requires a real typed outcome; development abort is rollback");
    need(pending_->kind!=EclRequestKind::native_call || pending_->values.empty() ||
        (pending_->values[0]!=0x8000 && pending_->values[0]!=0x8001),"Special combat setup requires its staged resolution");
    need(pending_->kind!=EclRequestKind::encounter_dialogue,"ENCOUNTER requires typed choices and presentation");
    need(pending_->kind!=EclRequestKind::setup_sprite && pending_->kind!=EclRequestKind::approach_sprite,
         "Sprite presentation requires a typed resolution");
    need(pending_->kind!=EclRequestKind::clear_treasure && pending_->kind!=EclRequestKind::treasure_setup &&
         pending_->kind!=EclRequestKind::remove_wealth,"Campaign mutation requires a staged resolution");
    need(pending_->kind!=EclRequestKind::select_current && pending_->kind!=EclRequestKind::restore_current &&
         pending_->kind!=EclRequestKind::add_npc && pending_->kind!=EclRequestKind::engine_service,"Party service requires a staged resolution");
    complete_host_request(token,updates);
}
void EclVm::complete_host_request(std::uint64_t token,std::span<const EclStateUpdate> updates) {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token,"Stale or unsolicited ECL host acknowledgement");
    for(const auto& [address,v]:updates) {
        (void)v;
        const auto region=locate_script_value(address).region;
        need(region==ScriptRegion::area1 || region==ScriptRegion::area2 || region==ScriptRegion::area3,
             "Host state updates require a state-bank address");
    }
    for(const auto& [address,v]:updates) {
        const auto location=locate_script_value(address);
        if(location.region==ScriptRegion::area1) area1_[location.byte_offset/2]=v;
        else if(location.region==ScriptRegion::area2) area2_[location.byte_offset/2]=v;
        else area3_[location.byte_offset/2]=v;
    }
    if(pending_->kind==EclRequestKind::write_value) {
        const auto location=locate_script_value(pending_->values[0]);
        if(location.region==ScriptRegion::area2) area2_[location.byte_offset/2]=pending_->values[1];
    }
    const auto next=pending_next_;const auto opcode=pending_->opcode;
    pending_.reset();status_=EclStatus::ready;
    if(!pending_followups_.empty()) {
        const auto followups=std::move(pending_followups_);pending_followups_.clear();
        write_values(followups,opcode,next-base);
    }
    if(status_!=EclStatus::waiting) pc_=next;
}
void EclVm::commit_random_state(const std::mt19937& expected,const std::mt19937& next) {
    need(status_==EclStatus::halted && !pending_,"Host randomness requires an idle halted VM");
    need(random_==expected,"Stale random-state checkpoint");random_=next;
}
void EclVm::commit_rest_tick(const std::mt19937& expected,const std::mt19937& next,
        const std::array<std::uint16_t,7>& clock) {
    need(status_==EclStatus::halted && !pending_,"Camp time requires an idle halted VM");
    need(random_==expected,"Stale camp random-state checkpoint");
    std::copy(clock.begin(),clock.end(),area1_.begin()+0xc6);random_=next;
}
void EclVm::answer_campaign_request(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next) {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token &&
         (pending_->kind==EclRequestKind::clear_treasure || pending_->kind==EclRequestKind::treasure_setup ||
          pending_->kind==EclRequestKind::remove_wealth),"Stale or unsolicited campaign mutation resolution");
    need(random_==expected,"Stale campaign random-state checkpoint");
    complete_host_request(token,{});random_=next;
}
std::uint8_t EclVm::next_opcode() const {
    valid_target(pc_);return script_[pc_-base];
}
std::uint16_t EclVm::preview_encounter_parameter(std::uint64_t token,std::size_t index) const {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token &&
         pending_->kind==EclRequestKind::add_monsters,"Stale encounter parameter preview");
    need(index<3 && pending_->values.size()==6,"Malformed encounter parameter request");
    EclOperand parameter;parameter.tag=static_cast<std::uint8_t>(pending_->values[2*index]);
    parameter.raw=pending_->values[2*index+1];
    need(!parameter.is_string(),"String used as an encounter parameter");
    // 27A9:0411 constructs into a separate pointer and does not change5D92.
    // Late selected-character operands therefore retain the original selection;
    // unresolved bindings still fail explicitly through the ordinary getter.
    return value(parameter);
}
void EclVm::answer_encounter_setup(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next) {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token &&
         pending_->kind==EclRequestKind::add_monsters,"Stale encounter setup resolution");
    need(random_==expected,"Stale encounter random-state checkpoint");
    auto staged=*this;staged.complete_host_request(token,{});staged.random_=next;*this=std::move(staged);
}
void EclVm::suspend_for_combat(std::uint64_t token) {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token &&
         pending_->kind==EclRequestKind::campaign_service,"Stale combat entry request");
    pending_->kind=EclRequestKind::combat;
    combat_preparation_committed_=false;
}
void EclVm::answer_special_combat_setup(std::uint64_t token,const std::mt19937& expected) {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token &&
         pending_->kind==EclRequestKind::native_call && pending_->values.size()==1 &&
         (pending_->values[0]==0x8000 || pending_->values[0]==0x8001),"Stale special combat setup resolution");
    need(random_==expected,"Stale special combat setup random-state checkpoint");
    const std::array<EclStateUpdate,1> updates{{{0x6de6,static_cast<std::uint16_t>(pending_->values[0]==0x8000)}}};
    complete_host_request(token,updates);
}
void EclVm::commit_combat_preparation(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next,
        std::uint8_t separation) {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token &&
         pending_->kind==EclRequestKind::combat,"Stale combat preparation request");
    need(!combat_preparation_committed_,"Combat preparation already committed");
    need(random_==expected,"Stale combat preparation random-state checkpoint");
    need(separation<=2,"Invalid combat separation");
    // Native direct bank stores, not numeric-setter host effects. ECL24 stays
    // suspended; this transaction cannot acknowledge a battle or grant rewards.
    area2_[0x2c1]=separation;area2_[0x2c6]=std::min<std::uint16_t>(area2_[0x2c6],100);
    area2_[0x333]=0;random_=next;combat_preparation_committed_=true;
}
void EclVm::commit_combat_runtime(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next,bool clear_surprise,bool hostility) {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token &&
         pending_->kind==EclRequestKind::combat && combat_preparation_committed_,"Stale or unprepared combat runtime request");
    need(random_==expected,"Stale combat runtime random-state checkpoint");
    // Native29CB:007A clears VM6DCB after every actor's round preparation.
    // No numeric setter, ECL acknowledgement or combat result is implied.
    if(clear_surprise)area2_[0x2cb]=0;
    //326D:2998's direct VM6E33 store joins the successful whole attack.
    if(hostility)area2_[0x333]=1;
    random_=next;
}
void EclVm::commit_combat_clock(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next,
        const std::array<std::uint16_t,7>& clock) {
    commit_combat_runtime(token,expected,next);
    std::copy(clock.begin(),clock.end(),area1_.begin()+0xc6);
}
void EclVm::begin_combat_rewards(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next,
        std::uint16_t defeated_count,std::uint16_t source_result,std::optional<std::uint16_t> party_count) {
    need(defeated_count<=71,"Invalid combat defeated-participant count");
    need(source_result==0 || source_result==0x80 || source_result==0x81,"Invalid native combat outcome word");
    need(!party_count || *party_count<=8,"Invalid combat persistent party count");
    commit_combat_runtime(token,expected,next);
    area2_[0x2c7]=source_result;area2_[0x2c8]=defeated_count;
    if(party_count)area2_[0x33e]=*party_count;
    pending_->kind=EclRequestKind::campaign_service;combat_preparation_committed_=false;
}
void EclVm::update_encounter_dialogue(std::uint64_t token,std::uint8_t distance) {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token &&
         pending_->kind==EclRequestKind::encounter_dialogue && pending_->encounter,"Stale encounter presentation");
    need(distance<=2 && distance<=pending_->encounter->maximum_distance,"Invalid encounter presentation distance");
    area2_[0x2c0]=pending_->encounter->maximum_distance;area2_[0x2c1]=distance;
}
void EclVm::complete_combat_defeat(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next,
        std::uint16_t defeated_count) {
    auto staged=*this;
    staged.begin_combat_rewards(token,expected,next,defeated_count,0x80,std::uint16_t{0});
    const std::array<EclStateUpdate,6> updates{{{0x6dca,static_cast<std::uint16_t>(staged.read_value(0x6dca)&1)},
        {0x6e70,0},{0x6e71,0},{0x6e72,0},{0x6de3,0},{0x6de6,0}}};
    staged.complete_host_request(token,updates);
    // Native49FF stops2182:3171 and unwinds2182:32F8 to host exit. The
    // ECL24 suffix must never execute after a confirmed true defeat.
    staged.status_=EclStatus::halted;*this=std::move(staged);
}
void EclVm::answer_encounter_dialogue(std::uint64_t token,std::optional<std::uint16_t> result) {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token &&
         pending_->kind==EclRequestKind::encounter_dialogue && pending_->encounter,"Stale encounter answer");
    need(!result || *result<=3,"Invalid encounter result");
    auto staged=*this;const auto destination=pending_->encounter->destination;
    staged.pending_.reset();staged.status_=EclStatus::ready;
    if(result)staged.write_value(destination,*result,pending_->opcode,pending_next_-base);
    if(staged.status_!=EclStatus::waiting)staged.pc_=pending_next_;
    *this=std::move(staged);
}
std::uint16_t EclVm::preview_party_parameter(std::uint64_t token,const IEclBindings& bindings) const {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token &&
        (pending_->kind==EclRequestKind::add_npc || pending_->kind==EclRequestKind::engine_service),"Stale party parameter preview");
    const auto offset=pending_->kind==EclRequestKind::add_npc?1u:0u;
    need(pending_->values.size()==offset+2,"Malformed party service request");
    auto staged=*this;staged.bindings_=&bindings;
    EclOperand operand;operand.tag=static_cast<std::uint8_t>(pending_->values[offset]);operand.raw=pending_->values[offset+1];
    return staged.value(operand);
}
void EclVm::answer_party_request(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next,
        std::span<const EclStateUpdate> updates,bool halt) {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token,"Stale party service answer");
    const auto kind=pending_->kind;
    need(kind==EclRequestKind::select_current || kind==EclRequestKind::restore_current ||
         kind==EclRequestKind::add_npc || kind==EclRequestKind::engine_service,"Invalid party service answer");
    need(random_==expected,"Stale party random-state checkpoint");
    need(kind!=EclRequestKind::select_current || invocation_selection_.has_value(),"Temporary selection requires an original party member");
    need(!halt || kind==EclRequestKind::restore_current || kind==EclRequestKind::engine_service,"Invalid terminating party service");
    auto staged=*this;staged.complete_host_request(token,updates);staged.random_=next;
    if(kind==EclRequestKind::select_current) staged.restore_selection_=true;
    if(kind==EclRequestKind::restore_current || kind==EclRequestKind::engine_service) staged.restore_selection_=false;
    if(kind==EclRequestKind::restore_current || halt) {staged.stack_.clear();staged.status_=EclStatus::halted;}
    *this=std::move(staged);
}
void EclVm::update_campaign_state(std::uint64_t token,std::span<const EclStateUpdate> updates) {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token &&
         pending_->kind==EclRequestKind::campaign_service,"Stale or unsolicited campaign state update");
    for(const auto& update:updates) {
        const auto region=locate_script_value(update.address).region;
        need(region==ScriptRegion::area1 || region==ScriptRegion::area2 || region==ScriptRegion::area3,
             "Campaign state updates require bank addresses");
    }
    for(const auto& update:updates) {
        const auto location=locate_script_value(update.address);
        if(location.region==ScriptRegion::area1) area1_[location.byte_offset/2]=update.value;
        else if(location.region==ScriptRegion::area2) area2_[location.byte_offset/2]=update.value;
        else area3_[location.byte_offset/2]=update.value;
    }
}
void EclVm::commit_campaign_random_state(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next) {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token &&
         pending_->kind==EclRequestKind::campaign_service,"Stale or unsolicited campaign randomness");
    need(random_==expected,"Stale campaign random-state checkpoint");random_=next;
}
void EclVm::answer_advance_time(std::uint64_t token,std::span<const EclStateUpdate> clock) {
    answer_advance_time(token,clock,random_,random_);
}
void EclVm::answer_advance_time(std::uint64_t token,std::span<const EclStateUpdate> clock,const std::mt19937& expected,const std::mt19937& next) {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token && pending_->kind==EclRequestKind::advance_time,
        "Stale or unsolicited time resolution");
    need(clock.size()==7,"Time resolution requires seven clock words");
    for(std::size_t i=0;i<clock.size();++i)
        need(clock[i].address==0x49c6+i,"Invalid time resolution clock address");
    need(random_==expected,"Stale time random-state checkpoint");
    auto staged=*this;staged.complete_host_request(token,clock);staged.random_=next;*this=std::move(staged);
}
void EclVm::answer_sprite_request(std::uint64_t token,std::uint8_t distance) {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token &&
        (pending_->kind==EclRequestKind::setup_sprite || pending_->kind==EclRequestKind::approach_sprite),"Stale or unsolicited sprite presentation resolution");
    need(distance<=2,"Unsupported sprite presentation distance");
    std::vector<EclStateUpdate> updates;
    if(pending_->kind==EclRequestKind::setup_sprite) {
        need(pending_->values.size()==3 && distance<=pending_->values[1],"Invalid sprite setup distance");
        updates.push_back({0x6dc0,pending_->values[1]});
    } else {
        need(pending_->values.size()==2 && pending_->values[0]==read_value(0x6dc1) &&
             pending_->values[0]>0 && distance==pending_->values[1],"Invalid sprite approach distance");
    }
    updates.push_back({0x6dc1,distance});complete_host_request(token,updates);
}
void EclVm::answer_menu(std::uint64_t token,std::size_t selection) {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token &&
         pending_->kind==EclRequestKind::menu && pending_->menu,"Stale or unsolicited MENU answer");
    need(selection<pending_->menu->labels.size(),"MENU selection out of range");
    const auto destination=pending_->menu->destination;
    const auto response=pending_->menu->responses.empty()?static_cast<std::uint16_t>(selection):pending_->menu->responses.at(selection);
    EclVm trial=*this;
    trial.pending_.reset();trial.status_=EclStatus::ready;
    trial.write_value(destination,response,pending_->opcode,pending_next_-base);
    if(trial.status_!=EclStatus::waiting) trial.pc_=pending_next_;
    *this=std::move(trial);
}
void EclVm::answer_input(std::uint64_t token,std::string_view text) {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token &&
         (pending_->kind==EclRequestKind::input_number || pending_->kind==EclRequestKind::input_string),"Stale or unsolicited input answer");
    const bool numeric=pending_->kind==EclRequestKind::input_number;
    need(text.size()<=(numeric?6u:40u),"Input exceeds native editor limit");
    std::string normalized;
    for(const unsigned char c:text) {
        need(c>=32 && c<=122,"Unsupported input character");
        normalized.push_back(static_cast<char>(c>='a' && c<='z'?c-32:c));
    }
    std::uint32_t number=0;
    if(numeric) {
        std::size_t i=normalized.find_first_not_of(' ');
        need(i!=std::string::npos,"Enter a number");
        bool negative=false;
        if(normalized[i]=='+' || normalized[i]=='-') {negative=normalized[i]=='-';++i;}
        unsigned radix=10;
        if(i<normalized.size() && normalized[i]=='$') {radix=16;++i;}
        need(i<normalized.size(),"Enter numeric digits");
        for(;i<normalized.size();++i) {
            const char c=normalized[i];
            const unsigned digit=c>='0' && c<='9'?static_cast<unsigned>(c-'0'):c>='A' && c<='F'?static_cast<unsigned>(c-'A'+10):16u;
            need(digit<radix,"Invalid numeric input");
            number=number*radix+digit;need(number<=65536,"Number must be 0..65536");
        }
        need(!negative || number==0,"Number must be nonnegative");
    }
    const auto destination=pending_->values.at(0);const auto opcode=pending_->opcode;
    EclVm trial=*this;trial.pending_.reset();trial.status_=EclStatus::ready;
    if(numeric) trial.write_value(destination,static_cast<std::uint16_t>(number),opcode,pending_next_-base);
    else trial.write_string(destination,normalized.empty()?overlay_string(0x16d8):normalized,opcode,pending_next_-base);
    if(trial.status_!=EclStatus::waiting) trial.pc_=pending_next_;
    *this=std::move(trial);
}
std::uint16_t EclVm::selected_damage_roll(std::uint64_t token,std::size_t party_size,std::mt19937& rng) const {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token &&
         pending_->kind==EclRequestKind::damage,"Stale or unsolicited damage resolution");
    const auto& v=pending_->values;
    need(v.size()==5 && v[0]==0xa0 && v[4]==0x80,"Unsupported damage target or saving-throw mode");
    need(party_size>=1 && party_size<=255,"Damage requires a nonempty party");
    need(v[1]==0 || v[2]!=0,"Damage dice require nonzero sides");
    auto sample=[&](std::uint32_t bound) {
        const auto threshold=(std::uint32_t{0}-bound)%bound;
        std::uint32_t value;do {value=static_cast<std::uint32_t>(rng());} while(value<threshold);
        return value%bound;
    };
    unsigned total=0;
    for(unsigned i=0;i<v[1];++i) total=(total+sample(v[2])+1)&255;
    // Native 2182:2775 rolls a random party ordinal even on the selected-target
    // path. Preserve its consumption of the modern, explicitly non-Borland RNG.
    (void)sample(static_cast<std::uint32_t>(party_size));
    return static_cast<std::uint16_t>(total+v[3]);
}
std::uint16_t EclVm::preview_selected_damage(std::uint64_t token,std::size_t party_size) const {
    auto rng=random_;return selected_damage_roll(token,party_size,rng);
}
void EclVm::answer_selected_damage(std::uint64_t token,std::size_t party_size,bool defeated) {
    auto trial=*this;(void)trial.selected_damage_roll(token,party_size,trial.random_);
    trial.pc_=trial.pending_next_;trial.pending_.reset();
    trial.status_=defeated?EclStatus::halted:EclStatus::ready;
    if(defeated) trial.stack_.clear();
    *this=std::move(trial);
}
void EclVm::answer_attack_damage(std::uint64_t token,const std::mt19937& expected,const std::mt19937& next,bool defeated) {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token && pending_->kind==EclRequestKind::damage &&
         decode_ecl_damage_mode(pending_->values).has_value(),"Stale or unsupported scripted damage resolution");
    need(random_==expected,"Stale scripted attack random-state checkpoint");
    auto trial=*this;trial.random_=next;trial.pc_=trial.pending_next_;trial.pending_.reset();
    trial.status_=defeated?EclStatus::halted:EclStatus::ready;
    if(defeated)trial.stack_.clear();
    *this=std::move(trial);
}
void EclVm::replace_script(std::uint64_t token,std::span<const std::uint8_t> record,std::uint8_t previous_record) {
    need(status_==EclStatus::waiting && pending_ && pending_->token==token &&
         pending_->kind==EclRequestKind::load_ecl,"Stale or unsolicited ECL replacement");
    EclVm trial=*this;
    trial.entries_=inspect_ecl_header(record);
    trial.text_origin_=StringStore::active().record_origin(record);trial.script_modified_=false;
    trial.loaded_size_=record.size()-2;
    need(trial.loaded_size_<=capacity,"ECL script exceeds capacity");
    trial.script_.assign(capacity,0);
    std::ranges::copy(record.subspan(2),trial.script_.begin());
    trial.pending_.reset();trial.status_=EclStatus::ready;
    trial.seed_value(0x49f2,previous_record);
    trial.seed_value(0x6de1,255);trial.seed_value(0x6dd2,0);trial.seed_value(0x6dd3,0);
    trial.seed_value(0x49e5,0);trial.seed_value(0x49e6,1);
    for(unsigned a=0x4a00;a<=0x4a1f;++a) trial.seed_value(static_cast<std::uint16_t>(a),0);
    for(unsigned a=0x6e79;a<=0x6e82;++a) trial.seed_value(static_cast<std::uint16_t>(a),0);
    const auto selection=trial.invocation_selection_;const auto restore=trial.restore_selection_;
    trial.start_entry(4);
    // Native20/27A9:01B6 retain4430/4436 across replacement. Destination4's
    // first EXIT restores that original member before arrival entries0/1.
    if(restore){trial.invocation_selection_=selection;trial.restore_selection_=true;}
    *this=std::move(trial);
}
}
