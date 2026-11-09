#include "gbengine/party/training_service.hpp"
#include "gbengine/party/character_recovery_service.hpp"
#include "gbengine/campaign/economy_service.hpp"
#include <algorithm>
namespace GBEngine {
namespace {
std::uint8_t die(std::mt19937& random,unsigned sides) {
    if(!sides || sides>256)throw DecodeError("Invalid training hit die");
    const auto threshold=(std::uint32_t{0}-sides)%sides;std::uint32_t value;
    do{value=static_cast<std::uint32_t>(random());}while(value<threshold);
    return static_cast<std::uint8_t>(value%sides+1);
}
void pay(Character& c,const ItemCatalog& catalog) {
    //393E:42ED visits low denominations first, taking floor(remaining/rate)+1,
    //then refunds excess greedily from platinum down. Integer coin rates make
    //this1000gp path exact; never normalize the entire purse or use a pool.
    const auto& rates=catalog.coin_rates();std::int64_t remaining=1000ll*rates[3];
    for(unsigned i=0;i<5 && remaining>0;++i) {
        if(!rates[i])throw DecodeError("Invalid training currency rate");
        const auto count=std::min<std::int64_t>(c.word(0x88+i*2u),remaining/rates[i]+1);
        c.set_word(0x88+i*2u,static_cast<std::uint16_t>(c.word(0x88+i*2u)-count));remaining-=count*rates[i];
    }
    if(remaining>0)throw DecodeError("Training purse no longer covers its fee");
    remaining=-remaining;
    for(int i=4;i>=0 && remaining>0;--i) {
        const auto n=remaining/rates[static_cast<unsigned>(i)];
        c.set_word(0x88+static_cast<unsigned>(i)*2u,static_cast<std::uint16_t>(c.word(0x88+static_cast<unsigned>(i)*2u)+n));remaining-=n*rates[static_cast<unsigned>(i)];
    }
}
}
std::uint8_t TrainingService::byte(unsigned at)const {
    if(0xc7c0u+at>=module_.size())throw DecodeError("Training source table is truncated");
    return module_[0xc7c0u+at];
}
std::uint32_t TrainingService::dword(unsigned at)const {
    std::uint32_t n=0;for(unsigned i=0;i<4;++i)n|=static_cast<std::uint32_t>(byte(at+i))<<(i*8u);return n;
}
TrainingService TrainingService::load(const IFileSystem& files){TrainingService s;s.module_=decode_start_data(files.read_asset("START.EXE"));return s;}
TrainingService::Advancement TrainingService::advancement(const Character& c)const {
    Advancement p;p.experience=c.word(0xac)|(static_cast<std::uint32_t>(c.word(0xae))<<16);
    std::uint32_t cap=0;
    for(unsigned cls=0;cls<8;++cls)if(c.byte(0x96+cls) && c.byte(0x96+cls)<128) {
        const auto bit=byte(0x5de + cls);p.existing=static_cast<std::uint8_t>(p.existing+bit);
        if(CharacterRecoveryService::advancement_capped(c,static_cast<std::uint8_t>(cls)))continue;
        const auto level=c.byte(0x96+cls);const auto needed=dword(0x409b+cls*56u+(level+1u)*4u);
        if(static_cast<std::int32_t>(needed)>0 && static_cast<std::int32_t>(needed)<=static_cast<std::int32_t>(p.experience)) {
            p.eligible=static_cast<std::uint8_t>(p.eligible+bit);
            const auto subsequent=dword(0x409b+cls*56u+(level+2u)*4u);
            if(static_cast<std::int32_t>(subsequent)>0 && static_cast<std::int32_t>(subsequent)<=static_cast<std::int32_t>(p.experience) && subsequent>cap)cap=subsequent-1;
        }
    }
    if(cap)p.experience=cap;
    return p;
}
std::uint8_t TrainingService::eligible_classes(const Character& c)const {return advancement(c).eligible;}
TrainingPlan TrainingService::review(const Party& party,std::size_t member,std::uint16_t available)const {
    if(member>=party.size())throw DecodeError("Training member is unavailable");
    const auto& c=party.member(member);
    TrainingPlan p;p.member=member;p.revision=party.revision();p.identity=c.byte(0xbf);p.available=static_cast<std::uint8_t>(available);
    p.experience=c.word(0xac)|(static_cast<std::uint32_t>(c.word(0xae))<<16);
    if(c.status()!=0){p.status=TrainingStatus::inactive;return p;}
    if(WealthRules::gold_value(c,party.item_catalog())<1000){p.status=TrainingStatus::insufficient_money;return p;}
    const auto progress=advancement(c);p.eligible=progress.eligible;p.experience=progress.experience;
    p.status=(progress.existing&p.available)==0?TrainingStatus::wrong_school:(p.eligible&p.available)==0?TrainingStatus::insufficient_experience:TrainingStatus::ready;
    p.eligible=static_cast<std::uint8_t>(p.eligible&p.available);return p;
}
TrainingPlan TrainingService::prepare(Party& party,std::size_t member,std::uint16_t available)const {
    auto p=review(party,member,available);auto& c=party.members_.at(member);
    const auto current=c.word(0xac)|(static_cast<std::uint32_t>(c.word(0xae))<<16);
    if(p.experience!=current){c.set_word(0xac,static_cast<std::uint16_t>(p.experience));c.set_word(0xae,static_cast<std::uint16_t>(p.experience>>16));++party.revision_;p.revision=party.revision_;}
    return p;
}
std::vector<std::uint8_t> TrainingService::learning_options(const Party& party,const TrainingPlan& p)const {
    if(p.status!=TrainingStatus::ready || party.revision()!=p.revision || p.member>=party.size() || party.member(p.member).byte(0xbf)!=p.identity)throw DecodeError("Stale training confirmation");
    if(!(p.eligible&byte(0x5de + 5)))return {};
    auto c=party.member(p.member);
    for(unsigned cls=0;cls<8;++cls)if(c.byte(0x96+cls) && (p.eligible&byte(0x5de + cls)))c.set_byte(0x96+cls,static_cast<std::uint8_t>(c.byte(0x96+cls)+1));
    CharacterRecoveryService::rebuild_class_cache(c,module_);
    std::vector<std::uint8_t> result;
    for(unsigned id=1;id<=56;++id) {
        const auto school=byte(0x31fa+id*16u),level=byte(0x31fb+id*16u);
        const unsigned index=0xb1+school*3u+level;
        if(index>=285)throw DecodeError("Training learning capacity index is undefined");
        if(c.byte(index) && !c.byte(0x32+id))result.push_back(static_cast<std::uint8_t>(id));
    }
    return result;
}
void TrainingService::train(Party& party,const TrainingPlan& p,std::optional<std::uint8_t> learned,std::mt19937& random)const {
    const auto options=learning_options(party,p);
    if((p.eligible&byte(0x5de + 5)) && (!learned || std::find(options.begin(),options.end(),*learned)==options.end()))throw DecodeError("Training requires an eligible new spell choice");
    if(!(p.eligible&byte(0x5de + 5)) && learned)throw DecodeError("This training has no spell-learning step");
    const auto current=review(party,p.member,p.available);
    //393E:29AE retains its eligible-class mask across the pre-question XP cap.
    // In a multiclass character that cap can put XP below another qualifying
    // class's threshold. Recomputing eligibility here would lose a native gain.
    const auto& offered=party.member(p.member);
    const auto experience=offered.word(0xac)|(static_cast<std::uint32_t>(offered.word(0xae))<<16);
    if((current.status!=TrainingStatus::ready && current.status!=TrainingStatus::insufficient_experience) ||
       (current.eligible&static_cast<std::uint8_t>(~p.eligible)) || experience!=p.experience)
        throw DecodeError("Training state changed since its offer");
    auto next=party;auto rng=random;auto& c=next.members_.at(p.member);pay(c,next.item_catalog());
    unsigned count=0;std::uint8_t hp=0;
    for(unsigned cls=0;cls<8;++cls)if(c.byte(0x96+cls) && c.byte(0x96+cls)<128) {
        ++count;if(!(p.eligible&byte(0x5de + cls)))continue;
        c.set_byte(0x96+cls,static_cast<std::uint8_t>(c.byte(0x96+cls)+1));
        if(c.byte(0x74)){c.set_byte(0x75,static_cast<std::uint8_t>(c.byte(0x75)-c.byte(0x75)/c.byte(0x74)));c.set_byte(0x74,static_cast<std::uint8_t>(c.byte(0x74)-1));}
    }
    CharacterRecoveryService::rebuild_class_cache(c,module_);if(learned)c.set_byte(0x32+*learned,1);
    for(unsigned cls=0;cls<8;++cls)if(c.byte(0x96+cls) && (p.eligible&byte(0x5de + cls))) {
        std::uint8_t value=0;for(unsigned i=0;i<byte(0x8b6+cls);++i)value=static_cast<std::uint8_t>(value+die(rng,byte(0x8be + cls)));
        hp=static_cast<std::uint8_t>(hp+value);
    }
    int con=static_cast<std::int8_t>(byte(0x408e + c.byte(0x14)));
    if(c.byte(0x2f)==2){if(c.byte(0x14)>16)++con;if(c.byte(0x14)>17)++con;}
    con*=static_cast<int>(count);
    const auto gain=std::max(1,(static_cast<int>(hp)+con)/static_cast<int>(count));
    c.set_byte(0xb1,static_cast<std::uint8_t>(c.byte(0xb1)+std::max(1,static_cast<int>(hp/count))));
    const auto old=c.byte(0x32),current_hp=c.hit_points();c.set_byte(0x32,static_cast<std::uint8_t>(old+gain));c.set_byte(0x11b,static_cast<std::uint8_t>(c.byte(0x32)-(old-current_hp)));
    ++next.revision_;party=std::move(next);random=std::move(rng);
}
}

