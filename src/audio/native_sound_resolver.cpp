#include "gbengine/audio/native_sound_resolver.hpp"
#include "gbengine/assets/decoders.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <string>

namespace GBEngine {
namespace {
constexpr std::size_t driver_at=0x8ae0,driver_size=0x1831;
constexpr std::uint64_t pit_clock=1193182,timer_divisor=5041,sample_rate=48000;
struct Machine {
    Bytes memory;
    std::uint16_t bank,notes,return_pc=0;
    explicit Machine(const Bytes& image,NativeSoundDevice device,unsigned index):memory(image),
        bank(device==NativeSoundDevice::pc_speaker?0x22d:0x3c7),notes(device==NativeSoundDevice::pc_speaker?0x3ad:0x547) {
        const unsigned table=device==NativeSoundDevice::pc_speaker?0x55f:0x607;
        for(unsigned channel=0;channel<4;++channel) {
            const auto at=static_cast<std::uint16_t>(bank+channel*48);
            for(unsigned field=0;field<48;field+=2)put(at+field,0);
            put(at,1);put(at+2,word(table+index*8+channel*2));
        }
    }
    std::uint8_t byte(unsigned at)const {if(at>=memory.size())throw DecodeError("Native sound byte address outside source driver");return memory[at];}
    std::uint16_t word(unsigned at)const {return static_cast<std::uint16_t>(byte(at)|(static_cast<unsigned>(byte(at+1))<<8));}
    void put(unsigned at,std::uint16_t value) {
        if(at<bank || at+1>=static_cast<unsigned>(bank)+4*48 || ((at-bank)&1))throw DecodeError("Native sound write outside owned channel registers");
        memory[at]=static_cast<std::uint8_t>(value);memory[at+1]=static_cast<std::uint8_t>(value>>8);
    }
    static int signed_word(std::uint16_t value){return value<32768?value:static_cast<int>(value)-65536;}
    static int signed_byte(std::uint8_t value){return value<128?value:static_cast<int>(value)-256;}
    void execute(std::uint16_t current) {
        auto si=current;auto pc=word(si+2);if(!pc)return;
        unsigned operations=0;
        const auto read=[&]() {const auto result=byte(pc);++pc;return result;};
        const auto read_word=[&]() {const auto lo=read(),hi=read();return static_cast<std::uint16_t>(lo|(static_cast<unsigned>(hi)<<8));};
        const auto finish=[&]() {if(!word(current))pc=0;put(current+2,pc);};
        while(++operations<16384) {
            const auto opcode=read();
            if(opcode==0xfa) {
                for(const auto field:{4u,6u,8u,10u,12u,16u,18u,22u,24u,26u,28u,30u,32u,34u,36u})put(si+field,0);
            }else if(opcode==0xfb) {if(!return_pc)throw DecodeError("Native sound return without source call");pc=return_pc;
            }else if(opcode==0xfc) {const auto destination=read_word();return_pc=pc;pc=destination;
            }else if(opcode==0xfd) {
                const auto offset=read_word();if(offset>144 || offset%48)throw DecodeError("Native sound channel selection outside four-channel bank");
                si=static_cast<std::uint16_t>(bank+offset);
                for(const auto field:{4u,6u,8u,10u,12u,16u,18u,22u,24u,26u,28u,30u,32u,34u,36u})put(si+field,0);
            }else if(opcode==0xfe) {
                const auto field=read();const auto destination=read_word();
                if(field>=48 || (field&1))throw DecodeError("Native sound loop register outside channel");
                auto count=word(si+field);if(count){--count;put(si+field,count);if(!count)continue;}
                pc=destination;
            }else if(opcode==0xff) {
                const auto field=read();const auto value=read_word();
                if(field>=48 || (field&1))throw DecodeError("Native sound register outside channel");
                put(si+field,value);
                if(!field){finish();return;}
            }else {
                //16C4: header selects one channel. Following note/duration
                // pairs use bit7 on the NOTE, not the header, as the return.
                const unsigned channel=opcode>>5;
                if(channel>3)throw DecodeError("Native sound note channel outside bank");
                const auto target=static_cast<std::uint16_t>(bank+channel*48);
                const auto note=read(),length=read();const auto tempo=static_cast<std::uint8_t>(word(si+14));
                const auto duration=static_cast<std::uint16_t>(tempo<=1?length:tempo*length);
                put(si,duration);
                if((note&127)!=127 && channel==0) {
                    put(target,duration);put(target+20,duration);put(target+24,0);put(target+26,1);
                    const auto pitch=static_cast<std::uint16_t>((note&127)+word(target+18));
                    const unsigned octave=pitch/12;
                    if(octave>31)throw DecodeError("Native sound note shift outside verified source range");
                    //Native CL startsFF and increments once before subtraction:
                    //pitch0..11 selects shift0, then twelve-tone table remainder.
                    const auto divisor=static_cast<std::uint16_t>(word(notes+(pitch%12)*2)>>(octave&31));
                    put(target+4,divisor);put(target+8,divisor);
                }
                if(note&128){finish();return;}
                //16C1 fetches the next header before processing another note.
            }
        }
        throw DecodeError("Native sound source control flow does not yield");
    }
    void channel(std::uint16_t at) {
        if(!word(at))return;
        put(at+10,static_cast<std::uint16_t>(word(at+10)+word(at+12)));
        put(at+4,static_cast<std::uint16_t>(word(at+4)+word(at+6)));
        std::uint16_t modulation=0;
        auto phase=static_cast<std::uint16_t>(word(at+30)+word(at+32));
        if(phase) {
            if(phase>=word(at+36))phase=static_cast<std::uint16_t>(phase-word(at+36));
            put(at+30,phase);
            const int shifted=signed_word(phase)>=0?signed_word(phase)/16:-((-signed_word(phase)+15)/16);
            const auto address=static_cast<std::uint16_t>(word(at+28)+shifted);
            const int product=signed_byte(byte(address))*256*signed_word(word(at+34));
            //IMUL's signed high word, including negative-floor semantics.
            const int high=product>=0?product/65536:-((-product+65535)/65536);
            modulation=static_cast<std::uint16_t>(high);
        }
        put(at+8,static_cast<std::uint16_t>(word(at+4)+modulation));
        if(word(at+20)) {
            put(at+20,static_cast<std::uint16_t>(word(at+20)-1));
            if(!word(at+20)){put(at+24,16);put(at+26,1);}
        }
        put(at,static_cast<std::uint16_t>(word(at)-1));
        if(!word(at))execute(at);
        if(word(at+26)) {
            put(at+26,static_cast<std::uint16_t>(word(at+26)-1));
            if(!word(at+26)) {
                for(unsigned n=0;;++n) {
                    if(n>1024)throw DecodeError("Native sound envelope does not yield");
                    const auto envelope=static_cast<std::uint16_t>(word(at+22)+word(at+24));
                    const auto delta=word(envelope),duration=word(envelope+2);
                    put(at+24,static_cast<std::uint16_t>(word(at+24)+4));
                    if(duration==65535) {put(at+10,delta);if(!delta)put(at+12,0);continue;}
                    put(at+12,delta);put(at+26,duration);break;
                }
            }
        }
    }
    NativeSoundFrame tick(NativeSoundDevice device) {
        for(unsigned i=0;i<4;++i)channel(static_cast<std::uint16_t>(bank+i*48));
        NativeSoundFrame frame;
        if(device==NativeSoundDevice::pc_speaker) {
            for(unsigned i=0;i<4;++i)if(word(bank+i*48) && word(bank+i*48+10)) {
                frame.frequency[0]=word(bank+i*48+8);frame.volume[0]=word(bank+i*48+10)&3;break;
            }
        }else for(unsigned i=0;i<4;++i) {
            frame.frequency[i]=static_cast<std::uint16_t>(word(bank+i*48+8)>>6);
            frame.volume[i]=static_cast<std::uint16_t>((65535u-word(bank+i*48+10))>>12);
        }
        return frame;
    }
    bool active()const {for(unsigned i=0;i<4;++i)if(word(bank+i*48))return true;return false;}
    Bytes key()const {Bytes result(memory.begin()+bank,memory.begin()+bank+192);result.push_back(static_cast<std::uint8_t>(return_pc));result.push_back(static_cast<std::uint8_t>(return_pc>>8));return result;}
};
struct Oscillator {
    std::array<double,4> phase{};
    unsigned noise=0x8000;bool noise_output=false;unsigned previous_noise_control=0;
    float sample(const NativeSoundFrame& frame,NativeSoundDevice device) {
        if(device==NativeSoundDevice::pc_speaker) {
            const auto divisor=frame.frequency[0]?frame.frequency[0]:65536u;
            phase[0]+=static_cast<double>(pit_clock)/(divisor*sample_rate);phase[0]-=std::floor(phase[0]);
            return frame.volume[0]==3?(phase[0]<0.5?0.2f:-0.2f):0.0f;
        }
        constexpr double psg_clock=3579545.0;
        double mixed=0;
        for(unsigned i=0;i<3;++i) {
            const unsigned divisor=(frame.frequency[i]&1023)?frame.frequency[i]&1023:1024;
            phase[i]+=psg_clock/(32*divisor*sample_rate);phase[i]-=std::floor(phase[i]);
            if(frame.volume[i]<15)mixed+=(phase[i]<0.5?1:-1)*std::pow(10.0,-static_cast<double>(frame.volume[i])/10);
        }
        const unsigned control=frame.frequency[3]&7;
        if((control&4)!=(previous_noise_control&4))noise=0x8000;
        previous_noise_control=control;
        const unsigned divisor=(control&3)==3?2*((frame.frequency[2]&1023)?frame.frequency[2]&1023:1024):(1u<<(5+(control&3)));
        phase[3]+=psg_clock/(16*divisor*sample_rate);
        while(phase[3]>=1) {
            phase[3]-=1;
            const bool feedback=((noise&2)!=0)!=(((noise&32)!=32) && (control&4));
            noise=(noise>>1)|(feedback?0x8000u:0u);noise_output=(noise&1)!=0;
        }
        if(frame.volume[3]<15)mixed+=(noise_output?1:-1)*std::pow(10.0,-static_cast<double>(frame.volume[3])/10);
        return static_cast<float>(mixed*0.05);
    }
};
}
NativeSoundDevice NativeSoundResolver::configuration(const IFileSystem& files) {
    const auto bytes=files.read_asset("POOL.CFG");std::string text(bytes.begin(),bytes.end());
    const auto first=text.find('\n');if(first==std::string::npos || first+1==text.size())throw DecodeError("POOL.CFG has no native sound configuration line");
    const auto mode=static_cast<unsigned char>(text[first+1]);
    return mode=='T'?NativeSoundDevice::tandy:mode=='P'?NativeSoundDevice::pc_speaker:NativeSoundDevice::disabled;
}
NativeSoundResolver NativeSoundResolver::load(const IFileSystem& files,std::optional<NativeSoundDevice> override_device) {
    const auto module=decode_start_data(files.read_asset("START.EXE"));
    if(module.size()<driver_at+driver_size)throw DecodeError("Native sound driver is truncated");
    NativeSoundResolver result;result.driver_.assign(module.begin()+driver_at,module.begin()+driver_at+driver_size);
    constexpr std::array<std::uint16_t,6> dispatch{0x164e,0x16ac,0x16b3,0x1645,0x162e,0x167f};
    for(unsigned i=0;i<dispatch.size();++i)if(read_u16(result.driver_,0x221+i*2)!=dispatch[i])throw DecodeError("Unsupported native sound opcode dispatch");
    if(result.driver_[0x1da]!=48 || read_u16(result.driver_,0x55f)!=0xd2b || read_u16(result.driver_,0x607)!=0x11bf)
        throw DecodeError("Unsupported native sound channel/program layout");
    result.device_=override_device?*override_device:configuration(files);return result;
}
std::optional<NativeSoundProgram> NativeSoundResolver::program(std::uint16_t command)const {
    if(device_==NativeSoundDevice::disabled || command<2 || command>12)return {};
    try {
        Machine machine(driver_,device_,command-1);NativeSoundProgram result;
        std::map<Bytes,std::size_t> seen;
        for(unsigned tick=0;tick<28000;++tick) {
            const auto key=machine.key();const auto old=seen.find(key);
            if(old!=seen.end()){result.loop_tick=old->second;return result;}
            seen.emplace(key,result.frames.size());result.frames.push_back(machine.tick(device_));
            if(!machine.active())return result;
        }
    }catch(const DecodeError&){return {};}
    return {};
}
std::optional<ResolvedAudioCue> NativeSoundResolver::resolve_program(std::uint16_t command) {
    if(const auto old=cache_.find(command);old!=cache_.end())return old->second;
    const auto source=program(command);if(!source || source->frames.empty())return {};
    ResolvedAudioCue result;Oscillator oscillator;
    for(std::size_t tick=0;tick<source->frames.size();++tick) {
        const auto end=((tick+1)*timer_divisor*sample_rate+pit_clock-1)/pit_clock;
        if(source->loop_tick && tick==*source->loop_tick)result.loop_start=result.samples.size();
        while(result.samples.size()<end)result.samples.push_back(oscillator.sample(source->frames[tick],device_));
    }
    cache_.emplace(command,result);return result;
}
std::optional<std::vector<float>> NativeSoundResolver::resolve(std::uint16_t command) {
    auto result=resolve_program(command);if(!result)return {};return std::move(result->samples);
}
}
