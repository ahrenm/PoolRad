#include "gbengine/debug/runtime.hpp"
#include "gbengine/world/exploration_session.hpp"
#include "gbengine/resources/string_store.hpp"
#include <iomanip>
#include <sstream>

namespace GBEngine::Debug {
namespace {
std::string quote(std::string_view s) {
    std::ostringstream out;out<<'"';
    for(unsigned char c:s) {
        if(c=='"' || c=='\\')out<<'\\'<<c;
        else if(c<32 || c>=127)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c)<<std::dec;
        else out<<c;
    }
    out<<'"';return out.str();
}
template<class T> void numbers(std::ostream& out,const T& values) {
    out<<'[';bool first=true;for(auto n:values){if(!first)out<<',';
        first=false;out<<+n;}out<<']';
}
void character(std::ostream& out,const Character& c) {
    const auto stats=c.stats();
    out<<"{\"name\":"<<quote(c.name())<<",\"status\":"<<unsigned(c.status())<<",\"hp\":"<<unsigned(stats.current_hp)
       <<",\"maximum_hp\":"<<unsigned(stats.maximum_hp)<<",\"ac\":"<<stats.armor_class<<",\"xp\":"<<stats.experience<<",\"levels\":";
    numbers(out,stats.levels);out<<",\"money\":";numbers(out,stats.money);out<<",\"abilities\":";numbers(out,stats.abilities);
    out<<",\"record_bytes\":[";for(unsigned n=0;n<285;++n){if(n)out<<',';out<<unsigned(c.byte(n));}out<<"],\"inventory\":[";
    for(unsigned n=0;n<c.inventory().size();++n){if(n)out<<',';const auto& i=c.inventory().item(n);
        out<<"{\"id\":"<<i.id()<<",\"name\":"<<quote(i.name())<<",\"readied\":"<<(i.readied()?"true":"false")<<",\"record_bytes\":[";
        for(unsigned at=0;at<63;++at){if(at)out<<',';out<<unsigned(i.byte(at));}out<<"]}";}
    out<<"],\"effects\":[";bool first=true;for(const auto& e:c.effects()) {
        if(!first)out<<',';
        first=false;out<<"{\"payload\":";numbers(out,e.payload);out<<",\"source\":"<<quote(e.source_file)<<",\"offset\":"<<e.source_offset<<'}';
    }out<<"]}";
}
}
std::string inspect(const ExplorationSession& s) {
    std::ostringstream out,random;random<<s.vm.random_state();const auto rng=random.str();
    out<<"{\"inspection_schema\":1,\"disk\":"<<s.disk()<<",\"ecl\":"<<unsigned(s.record())<<",\"x\":"<<s.position_x()<<",\"y\":"<<s.position_y()
       <<",\"facing\":"<<unsigned(s.world.facing)<<",\"can_act\":"<<(s.can_act()?"true":"false")<<",\"boundary\":"<<quote(s.boundary)
       <<",\"text\":"<<quote(s.display_text)<<",\"pc\":"<<s.vm.pc()<<",\"vm_status\":"<<quote(ecl_status_name(s.vm.status()))
       <<",\"steps\":"<<s.vm.steps()<<",\"rng_sha256\":"<<quote(sha256({reinterpret_cast<const std::uint8_t*>(rng.data()),rng.size()}))<<",\"rng_state\":"<<quote(rng);
    out<<",\"journal\":{\"stored\":"<<(s.journal().state.stored?"true":"false")<<",\"occupied\":"<<(s.journal().state.occupied?"true":"false")<<",\"known_bytes\":";
    numbers(out,s.journal().state.known);out<<"}";
    out<<",\"request\":";
    if(const auto& r=s.vm.pending_request()) {
        out<<"{\"token\":"<<r->token<<",\"kind\":"<<quote(ecl_request_name(r->kind))<<",\"address\":"<<r->address<<",\"opcode\":"<<unsigned(r->opcode)<<",\"values\":";
        numbers(out,r->values);out<<'}';
    }else out<<"null";
    out<<",\"menu\":[";if(const auto* menu=s.menu())for(unsigned n=0;n<menu->labels.size();++n){if(n)out<<',';out<<quote(menu->labels[n]);}out<<']';
    out<<",\"campaign\":";
    if(s.campaign_token()) {
        out<<"{\"token\":"<<*s.campaign_token()<<",\"kind\":"<<unsigned(*s.campaign_kind())<<",\"amounts\":";numbers(out,s.campaign_resources().amounts);out<<",\"items\":[";
        bool first=true;for(const auto& i:s.campaign_resources().items){if(!first)out<<',';first=false;out<<"{\"id\":"<<i.id<<",\"name\":"<<quote(i.item.name())<<'}';}out<<"]}";
    }else out<<"null";
    out<<",\"selected_member\":";if(const auto selected=s.world.party.selected_index())out<<*selected;else out<<"null";
    out<<",\"background\":";
    if(s.world.background) {
        const auto& b=*s.world.background;
        out<<"{\"sky\":"<<b.sky<<",\"alternate_sky\":"<<b.alternate_sky<<",\"horizon\":"<<unsigned(b.horizon)
           <<",\"lower_horizon\":"<<unsigned(b.lower_horizon)<<",\"floor\":"<<unsigned(b.floor)<<'}';
    }else out<<"null";
    out<<",\"prepared_actor_count\":"<<unsigned(s.world.encounter.count)<<",\"selected_encounter_actor_id\":";
    if(s.world.encounter.selected_actor)out<<s.world.encounter.selected_actor->value;else out<<"null";
    out<<",\"party_revision\":"<<s.world.party.revision()<<",\"party\":[";
    for(unsigned n=0;n<s.world.party.size();++n){if(n)out<<',';character(out,s.world.party.member(n));}out<<']';
    const auto banks=s.vm.state_banks();out<<",\"banks\":{\"4900\":";numbers(out,banks.area1);out<<",\"6B00\":";numbers(out,banks.area2);out<<",\"9700\":";numbers(out,banks.area3);out<<'}';
    out<<",\"clock\":";if(const auto clock=s.clock())numbers(out,clock->words());else out<<"null";
    out<<",\"combat\":";
    if(const auto* b=s.combat()) {
        out<<"{\"token\":"<<b->entry().token.value<<",\"revision\":"<<b->revision()<<",\"phase\":"<<unsigned(b->round().phase)
           <<",\"runtime_started\":"<<(b->runtime_attempted()?"true":"false")<<",\"error\":"<<quote(b->runtime_error())<<",\"action\":"<<quote(b->action_message())<<",\"actors\":[";
        bool first=true;if(b->deployment())for(const auto& a:b->deployment()->actors){if(!first)out<<',';first=false;
            out<<"{\"origin\":"<<unsigned(a.id.origin)<<",\"id\":"<<a.id.value<<",\"removed\":"<<(a.removed?"true":"false")<<",\"side\":"<<unsigned(a.character.byte(0x10e))<<",\"extension\":";numbers(out,a.extension);
            out<<",\"position\":";if(a.position)out<<'['<<a.position->x<<','<<a.position->y<<']';else out<<"null";
            out<<",\"character\":";character(out,a.character);out<<'}';}
        out<<"]}";
    }else out<<"null";
    out<<'}';return out.str();
}
}

