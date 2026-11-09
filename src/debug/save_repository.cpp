#include "gbengine/debug/save_repository.hpp"
#include "gbengine/debug/runtime.hpp"
#include "gbengine/resources/string_store.hpp"
namespace GBEngine::Debug {
namespace {
class Repository final:public ISaveRepository {
    std::unique_ptr<ISaveRepository> inner_;bool characters_;
    void record(std::string_view operation,char slot,std::span<const std::uint8_t> bytes)const {
        if(auto* trace=Runtime::current())trace->event(operation,
            "{\"repository\":\""+std::string(characters_?"characters":"campaign")+"\",\"slot_code\":"+std::to_string(static_cast<unsigned char>(slot))+
            ",\"size\":"+std::to_string(bytes.size())+",\"sha256\":\""+sha256(bytes)+"\",\"loaded_save_assistance_provenance\":\"not_asserted\"}");
    }
public:
    Repository(std::unique_ptr<ISaveRepository> inner,bool characters):inner_(std::move(inner)),characters_(characters){}
    bool occupied(char slot)const override{return inner_->occupied(slot);}
    Bytes load(char slot)const override {auto bytes=inner_->load(slot);record("save_load",slot,bytes);return bytes;}
    void save(char slot,std::span<const std::uint8_t> bytes)override{inner_->save(slot,bytes);record("save_write",slot,bytes);}
};
}
std::unique_ptr<ISaveRepository> trace_repository(std::unique_ptr<ISaveRepository> inner,bool characters){
    if(!inner)return {};
    return std::make_unique<Repository>(std::move(inner),characters);
}
}
