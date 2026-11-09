#include "gbengine/effects/effect_lifecycle.hpp"
#include <algorithm>
namespace GBEngine {
namespace {
bool same_effects(const std::vector<CharacterEffect>& a,const std::vector<CharacterEffect>& b,bool contexts) {
    if(a.size()!=b.size()) return false;
    for(std::size_t i=0;i<a.size();++i) {
        auto expected=a[i];
        if(contexts) expected.payload[3]=b[i].payload[3];
        if(expected!=b[i]) return false;
    }
    return true;
}
}
EffectAdvanceResult EffectLifecycle::advance(Party& party,std::uint16_t ticks,IEffectExpiryResolver* resolver) {
    EffectActivityMask mask;mask.fill(1);
    return advance_retained(party,ticks,mask,resolver);
}
EffectAdvanceResult EffectLifecycle::advance_retained(Party& party,std::uint16_t ticks,EffectActivityMask& mask,IEffectExpiryResolver* resolver) {
    if(!party.size()) throw DecodeError("Effect lifecycle requires a bound roster");
    if(party.size()>mask.size()) throw DecodeError("Effect activity mask cannot hold roster");
    for(const auto& member:party.members_) if(!member.effects_restored())
        throw DecodeError("Effect lifecycle requires restored effect lists");
    auto staged=party;
    EffectAdvanceResult result;
    // Stable IDs replace native pointers, preserving duplicate codes and the
    // verified predecessor/head restart quirk without using saved addresses.
    struct Node {std::size_t id;CharacterEffect effect;};
    std::vector<std::vector<Node>> lists;
    std::vector<std::size_t> next_ids;
    for(const auto& member:staged.members_) {
        auto& list=lists.emplace_back();
        for(const auto& effect:member.effects_) list.push_back({list.size(),effect});
        next_ids.push_back(list.size());
    }
    auto marked=mask;
    std::size_t work=0;
    for(unsigned remaining=ticks;remaining;) {
        const auto chunk=static_cast<std::uint16_t>(std::min(remaining,10u));
        for(std::size_t member=0;member<lists.size();++member) {
            auto& nodes=lists[member];
            if(!marked[member]) continue;
            marked[member]=false;
            if(nodes.empty()) continue;
            const auto tail=nodes.back().id;
            std::optional<std::size_t> cursor=nodes.front().id,predecessor=cursor;
            bool finished=false;
            while(cursor && !finished) {
                work+=2*nodes.size()+1; // Bound cursor/predecessor searches as well as visits.
                if(work>1000000) throw DecodeError("Effect lifecycle work budget exhausted");
                auto it=std::find_if(nodes.begin(),nodes.end(),[&](const Node& n){return n.id==*cursor;});
                if(it==nodes.end()) throw DecodeError("Invalid effect lifecycle cursor");
                const auto position=static_cast<std::size_t>(it-nodes.begin());
                finished=it->id==tail;
                const auto next=position+1<nodes.size()?std::optional<std::size_t>{nodes[position+1].id}:std::nullopt;
                const auto duration=it->effect.word_1();
                if(!duration) cursor=next;
                else if(duration>chunk) {
                    it->effect.set_duration(static_cast<std::uint16_t>(duration-chunk));
                    ++result.decrements;marked[member]=true;cursor=next;
                } else {
                    const auto expired_id=it->id;
                    if(it->effect.payload[4]) {
                        // The callback observes countdown changes made earlier
                        // in this chunk, with the expiring node still linked.
                        auto& current=staged.members_[member].effects_;current.clear();
                        for(const auto& node:nodes) current.push_back(node.effect);
                        const auto before=staged.members_[member].effects_;
                        EffectExpiryEdits edits;
                        if(!resolver || !resolver->reverse_with_edits({member,it->effect,position},staged.members_[member],edits))
                            throw DecodeError("Effect expiry reversal unresolved for code "+std::to_string(it->effect.code()));
                        const auto& after=staged.members_[member].effects_;
                        if(!same_effects(before,after,resolver->allows_context_updates()))
                            throw DecodeError("Effect expiry resolver cannot change effect identity, order or duration");
                        for(std::size_t i=0;i<nodes.size();++i) nodes[i].effect.payload[3]=after[i].payload[3];
                        std::vector<std::size_t> removed_ids;
                        for(const auto index:edits.remove_indices) {
                            if(index>=nodes.size() || nodes[index].id==expired_id ||
                               std::find(removed_ids.begin(),removed_ids.end(),nodes[index].id)!=removed_ids.end())
                                throw DecodeError("Invalid effect expiry removal journal");
                            removed_ids.push_back(nodes[index].id);
                        }
                        if(nodes.size()-removed_ids.size()+edits.append.size()>65535)
                            throw DecodeError("Effect callback exceeds native node count bound");
                        for(const auto& consumption:edits.spell_slot_consumptions) {
                            if(consumption.member>=staged.members_.size() || consumption.slot<0x17 || consumption.slot>0x2b ||
                               !consumption.expected_full_byte || staged.members_[consumption.member].byte(consumption.slot)!=consumption.expected_full_byte)
                                throw DecodeError("Effect callback spell consumption lost its exact member/slot/full-byte ownership");
                            staged.members_[consumption.member].set_byte(consumption.slot,0);
                        }
                        nodes.erase(std::remove_if(nodes.begin(),nodes.end(),[&](const Node& node){
                            return std::find(removed_ids.begin(),removed_ids.end(),node.id)!=removed_ids.end();
                        }),nodes.end());
                        for(auto& effect:edits.append) nodes.push_back({next_ids[member]++,std::move(effect)});
                        for(const auto& notice:edits.notices) {
                            if(notice.member!=member) throw DecodeError("Effect expiry notice has the wrong owner");
                            result.notices.push_back(notice);
                        }
                    }
                    it=std::find_if(nodes.begin(),nodes.end(),[&](const Node& n){return n.id==expired_id;});
                    if(it==nodes.end()) throw DecodeError("Expiring effect identity was lost");
                    nodes.erase(it);++result.expired;
                    cursor=!nodes.empty() && predecessor==nodes.front().id?
                        std::optional<std::size_t>{nodes.front().id}:next;
                    if(cursor && std::none_of(nodes.begin(),nodes.end(),[&](const Node& n){return n.id==*cursor;})) {
                        // Native code would retain the removed successor's
                        // pointer on this path. Do not invent its contents.
                        throw DecodeError("Effect callback invalidated the native saved successor");
                    }
                    if(!cursor) for(const auto& node:nodes) if(node.effect.word_1()) marked[member]=true;
                }
                predecessor.reset();
                if(cursor) for(std::size_t i=0;i<nodes.size();++i) {
                    predecessor=nodes[i].id;
                    if(nodes[i].id==*cursor || (i+1<nodes.size() && nodes[i+1].id==*cursor)) break;
                }
            }
            if(cursor) {
                const auto start=std::find_if(nodes.begin(),nodes.end(),[&](const Node& n){return n.id==*cursor;});
                marked[member]=marked[member] || std::any_of(start,nodes.end(),[](const Node& n){return n.effect.word_1()!=0;});
            }
        }
        remaining-=chunk;
    }
    for(std::size_t i=0;i<lists.size();++i) {
        auto& effects=staged.members_[i].effects_;effects.clear();
        for(auto& node:lists[i]) effects.push_back(std::move(node.effect));
    }
    if(result.decrements || result.expired) {++staged.revision_;party=std::move(staged);}
    mask=marked;
    return result;
}
}
