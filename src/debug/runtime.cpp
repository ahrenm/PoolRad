#include "gbengine/debug/runtime.hpp"
#include "gbengine/qol/journal.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace GBEngine::Debug {
namespace {
thread_local Runtime* active=nullptr;
std::atomic<std::uint64_t> runs{0};
std::string quote(std::string_view s) {
    std::ostringstream out;out<<'"';for(unsigned char c:s) {
        if(c=='"' || c=='\\')out<<'\\'<<c;
        else if(c<32 || c>=127)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c)<<std::dec;
        else out<<c;
    }out<<'"';return out.str();
}
std::string hash(std::string_view s) {return sha256({reinterpret_cast<const std::uint8_t*>(s.data()),s.size()});}
void write(IArtifactWriter& writer,const std::filesystem::path& path,std::string_view s) {
    writer.write(path,{reinterpret_cast<const std::uint8_t*>(s.data()),s.size()});
}
}
struct Runtime::Impl {
    IArtifactWriter& writer;std::ostream& errors;std::filesystem::path root;std::ofstream events;
    std::vector<ExplorationSession*> sessions;
    std::uint64_t sequence=0,frame=0,time=0,generation=0;bool failed=false,snapshot=false,assisted=false;
    std::string last_hash,last_mode;
    std::uint64_t confirmation_ms=0;
    Impl(const std::filesystem::path& dir,IArtifactWriter& w,std::ostream& e):writer(w),errors(e) {
        const auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
        root=dir/("run-"+std::to_string(stamp)+"-"+std::to_string(runs.fetch_add(1)));
    }
    std::string publish(std::string_view mode,std::string_view state,std::string_view boundary) {
        const auto filename="state-"+std::to_string(++generation)+".json";
        const auto envelope="{\"trace_schema\":1,\"generation\":"+std::to_string(generation)+",\"frame\":"+std::to_string(frame)+",\"quick_win_used_this_run\":"+(assisted?"true":"false")+",\"capture_boundary\":"+quote(boundary)+",\"mode\":"+quote(mode)+",\"state\":"+std::string(state)+"}";
        write(writer,root/filename,envelope);
        const auto pointer="{\"file\":"+quote(filename)+",\"sha256\":"+quote(hash(envelope))+"}";
        write(writer,root/"latest.json",pointer);return pointer;
    }
};
Runtime::Runtime(const std::filesystem::path& dir,const IFileSystem& files,IArtifactWriter& writer,std::ostream& errors)
    :impl_(std::make_unique<Impl>(dir,writer,errors)),previous_(active) {
    auto& d=*impl_;std::ostringstream manifest;
    manifest<<"{\"trace_schema\":1,\"inspection_schema\":1,\"quick_win_key\":\"F8\",\"snapshot_key\":\"F9\",\"sources\":[";
    bool first=true;for(const auto& name:files.list_assets()) {
        const auto bytes=files.read_asset(name);if(!first)manifest<<',';first=false;
        manifest<<"{\"name\":"<<quote(name)<<",\"size\":"<<bytes.size()<<",\"sha256\":"<<quote(sha256(bytes))<<'}';
    }manifest<<"]}";
    // The injected writer enforces source-output separation before any native
    // stream is opened (NativeArtifactWriter is App's default).
    // Validate the destination using the source-protecting writer, then reserve
    // a directory atomically. Simultaneous launches cannot share a run stream.
    write(writer,dir/".trace-destination","");
    const auto candidate=d.root;
    bool reserved=false;
    for(unsigned attempt=0;attempt<128;++attempt) {
        d.root=candidate;d.root+="-"+std::to_string(attempt);
        if(std::filesystem::create_directory(d.root)){reserved=true;break;}
    }
    if(!reserved)throw std::runtime_error("Cannot reserve a private trace run directory");
    write(writer,d.root/"manifest.json",manifest.str());
    d.events.open(d.root/"events.jsonl",std::ios::binary|std::ios::trunc);
    if(!d.events)throw std::runtime_error("Cannot open debug trace events");
    active=this;event("run_start","{\"assistance_policy\":\"F8 is explicit, logged, and never a combat parity test\"}");
    errors<<"Debug trace: "<<d.root.string()<<" (F8 win, F9 snapshot)\n";
}
Runtime::~Runtime(){if(active==this)active=previous_;}
Runtime* Runtime::current(){return active;}
bool Runtime::healthy()const{return !impl_->failed;}
bool enabled(){return active && active->healthy();}
void Runtime::attach(ExplorationSession* s){impl_->sessions.push_back(s);}
void Runtime::detach(ExplorationSession* s){std::erase(impl_->sessions,s);}
SessionAttachment::SessionAttachment(ExplorationSession& s):runtime_(Runtime::current()),session_(&s){if(runtime_)runtime_->attach(&s);}
SessionAttachment::~SessionAttachment(){if(runtime_)runtime_->detach(session_);}
bool Runtime::event(std::string_view kind,std::string_view payload) {
    auto& d=*impl_;if(d.failed)return false;
    if(kind=="mode_exit") {
        // A quit input can commit terminal state without another render. Capture
        // before mode destruction, without inventing an actual-present event.
        try {
            const auto state=d.sessions.empty()?"null":inspect(*d.sessions.back());
            if(!event("state",d.publish(d.last_mode,state,"mode_exit")))return false;
        }catch(const std::exception& error){d.failed=true;d.errors<<"Debug final capture disabled: "<<error.what()<<'\n';return false;}
    }
    d.events<<"{\"sequence\":"<<++d.sequence<<",\"frame\":"<<d.frame<<",\"elapsed_ms\":"<<d.time<<",\"kind\":"<<quote(kind)<<",\"data\":"<<payload<<"}\n";
    d.events.flush();
    if(!d.events){d.failed=true;d.errors<<"Debug trace failed; tracing and quick-win disabled\n";return false;}
    if(kind=="debug_quick_win")d.assisted=true;
    return true;
}
void Runtime::request_snapshot(){impl_->snapshot=true;}
bool Runtime::snapshot_confirmed()const{return healthy() && impl_->confirmation_ms!=0;}
void Runtime::draw_feedback(Framebuffer& frame)const {
    if(!snapshot_confirmed())return;
    // Debug-only confirmation, outside game state and captured source pixels.
    for(int y=0;y<200;++y)for(int x=0;x<320;++x)
        if(x<2 || x>=318 || y<2 || y>=198)frame[static_cast<std::size_t>(y*320+x)]=0xff55ff55u;
}
void Runtime::input(InputEvent e,std::string_view mode) {
    event("input","{\"mode\":"+quote(mode)+",\"key\":"+std::to_string(unsigned(e.key))+",\"pressed\":"+(e.pressed?"true":"false")+",\"character\":"+std::to_string(e.character)+"}");
    if(e.pressed && e.key==Key::debug_snapshot)request_snapshot();
}
void Runtime::presented(const Framebuffer& frame,std::string_view mode,std::uint64_t elapsed) {
    auto& d=*impl_;if(d.failed)return;
    d.confirmation_ms-=std::min(d.confirmation_ms,elapsed);
    ++d.frame;d.time+=elapsed;d.last_mode=mode;
    try {
        const auto state=d.sessions.empty()?"null":inspect(*d.sessions.back());const auto state_hash=hash(state);
        const bool changed=state_hash!=d.last_hash,manual=d.snapshot;
        if(manual || changed) {
            // The immutable generation is authoritative. Readers retry a torn
            // latest pointer and verify the generation's digest before use.
            event("state",d.publish(mode,state,"actual_present"));d.last_hash=state_hash;
        }
        if(d.snapshot) {
            const std::string header="P6\n320 200\n255\n";Bytes pixels(header.begin(),header.end());
            for(auto c:frame){pixels.push_back(static_cast<std::uint8_t>(c>>16));pixels.push_back(static_cast<std::uint8_t>(c>>8));pixels.push_back(static_cast<std::uint8_t>(c));}
            d.writer.write(d.root/("frame-"+std::to_string(d.frame)+".ppm"),pixels);d.snapshot=false;
            if(event("snapshot_saved","{\"frame\":"+std::to_string(d.frame)+"}"))d.confirmation_ms=1500;
        }
        const auto pixels=(manual || changed || d.frame%30==0)?quote(sha256({reinterpret_cast<const std::uint8_t*>(frame.data()),frame.size()*sizeof(frame[0])})):"null";
        event("present","{\"mode\":"+quote(mode)+",\"pixels_sha256\":"+pixels+",\"delta_ms\":"+std::to_string(elapsed)+"}");
    }catch(const std::exception& error){d.failed=true;d.errors<<"Debug trace disabled: "<<error.what()<<'\n';}
}
void input(InputEvent e,std::string_view mode){if(enabled())active->input(e,mode);}
void presented(const Framebuffer& f,std::string_view mode,std::uint64_t elapsed){if(enabled())active->presented(f,mode,elapsed);}
void journal_receipt(const JournalReceipt& receipt) noexcept {
    if(!enabled())return;
    try {
        // Broad text detection is diagnostic only; it never awards an ID.
        std::string upper=receipt.text;for(char& c:upper)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');
        const bool candidate=upper.find("JOURNAL")!=std::string::npos || upper.find("ENTRY")!=std::string::npos ||
            upper.find("TALE")!=std::string::npos || upper.find("PROCLAMATION")!=std::string::npos;
        if(receipt.ids.empty() && receipt.status=="unmapped" && !candidate)return;
        if(receipt.ids.empty() && !candidate && receipt.status!="invalid-value")return;
        std::string ids="[";for(auto id:receipt.ids){if(ids.size()>1)ids+=',';ids+=quote(journal_id(id));}ids+=']';
        active->event("journal_receipt","{\"token\":"+std::to_string(receipt.token)+",\"disk\":"+std::to_string(receipt.disk)+
            ",\"record\":"+std::to_string(receipt.record)+",\"address\":"+std::to_string(receipt.address)+
            ",\"status\":"+quote(receipt.status)+",\"text_id\":"+quote(receipt.text_id)+",\"text\":"+quote(receipt.text)+",\"ids\":"+ids+"}");
    }catch(...) { /* Optional audit failure must not change source completion. */ }
}

}
