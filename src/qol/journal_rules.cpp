#include "gbengine/qol/journal.hpp"
#include "gbengine/resources/string_store.hpp"
#include <algorithm>
#include <charconv>
#include <sstream>
#include <set>

namespace GBEngine {
namespace {
#include "journal_reference_registry.inc"
struct BuiltinReferences final:IFileSystem {
    Bytes read_asset(std::string_view name)const override {
        if(name!="references.tsv")throw DecodeError("Unknown built-in Journal metadata");
        return {builtin_references.begin(),builtin_references.end()};
    }
    std::vector<std::string> list_assets()const override {return {"references.tsv"};}
};
unsigned number(std::string_view s,int base=10) {
    unsigned n{};const auto parsed=std::from_chars(s.data(),s.data()+s.size(),n,base);
    if(parsed.ec!=std::errc{} || parsed.ptr!=s.data()+s.size())throw DecodeError("Invalid Journal registry number");
    return n;
}
std::vector<std::string> split(std::string_view s,char separator) {
    std::vector<std::string> values;std::size_t start=0;
    for(;;){const auto end=s.find(separator,start);values.emplace_back(s.substr(start,end==s.npos?s.size()-start:end-start));if(end==s.npos)return values;start=end+1;}
}
JournalId parse_id(std::string_view s) {
    JournalKind kind;std::size_t offset;
    if(s.starts_with("entry-")){kind=JournalKind::entry;offset=6;}
    else if(s.starts_with("tale-")){kind=JournalKind::tale;offset=5;}
    else if(s.starts_with("proclamation-")){kind=JournalKind::proclamation;offset=13;}
    else throw DecodeError("Invalid Journal registry ID");
    JournalId id{kind,number(s.substr(offset))};if(journal_id(id)!=s)throw DecodeError("Noncanonical Journal registry ID");return id;
}
}
JournalRules::JournalRules(const IFileSystem& source):JournalRules(source,BuiltinReferences{}) {}
JournalRules::JournalRules(const IFileSystem& source,const IFileSystem& content) {
    const auto bytes=content.read_asset("references.tsv");
    if(bytes.size()>1024*1024)throw DecodeError("Journal registry exceeds size limit");
    std::istringstream input(std::string(bytes.begin(),bytes.end()));std::string line;
    std::array<std::string,8> profiles;std::set<std::tuple<unsigned,unsigned,unsigned>> sites;
    while(std::getline(input,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty()||line[0]=='#')continue;
        const auto cells=split(line,'\t');
        if(cells[0]=="profile" && cells.size()==3) {
            const auto disk=number(cells[1]);
            if(disk<1||disk>8||!profiles[disk-1].empty()||cells[2].size()!=64 ||
               cells[2].find_first_not_of("0123456789abcdef")!=std::string::npos)throw DecodeError("Invalid Journal source profile");
            profiles[disk-1]=cells[2];continue;
        }
        if(cells[0]!="site"||cells.size()!=7)throw DecodeError("Invalid Journal registry row");
        Rule rule{number(cells[1]),number(cells[2]),number(cells[3],16),cells[4],{}};
        if(rule.disk<1||rule.disk>8||rule.record>255||rule.address>65535||cells[6].empty()||
           !sites.emplace(rule.disk,rule.record,rule.address).second)throw DecodeError("Invalid or duplicate Journal source site");
        if(rule.resolver=="literal") {
            for(const auto& id:split(cells[5],','))rule.ids.push_back(parse_id(id));
        }else if((rule.resolver!="tale-number"&&rule.resolver!="roman-list")||cells[5]!="-")throw DecodeError("Invalid Journal reference resolver");
        rules_.push_back(std::move(rule));
    }
    if(rules_.empty())throw DecodeError("Empty Journal reference registry");
    for(unsigned d=1;d<=8;++d)if(profiles[d-1].empty()||sha256(source.read_asset("ECL"+std::to_string(d)+".DAX"))!=profiles[d-1])
        throw DecodeError("Journal source profile mismatch for ECL"+std::to_string(d));
}
JournalReceipt JournalRules::resolve(std::uint64_t token,unsigned disk,unsigned record,std::uint16_t address,std::string_view text_id,std::string_view text)const {
    JournalReceipt out{token,disk,record,address,std::string(text_id),std::string(text),"unmapped",{}};
    const auto rule=std::find_if(rules_.begin(),rules_.end(),[&](const Rule& r){return r.disk==disk&&r.record==record&&r.address==address;});
    if(rule==rules_.end())return out;
    out.status="matched";
    if(rule->resolver=="literal")out.ids=rule->ids;
    else if(rule->resolver=="tale-number") {
        unsigned n{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),n);
        if(parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&n>=1&&n<=23)out.ids.push_back({JournalKind::tale,n});
        else out.status="invalid-value";
    }else {
        static constexpr std::array<std::string_view,18> roman{"LIX","LXIV","LXXVIII","CI","CIX","CX","CXIV","CXX","CXXVI","CXXIX","CXXXIV","CLIV","CLVI","CLIX","CXC","CCI","CCIV","CCXIV"};
        std::string word;bool invalid=false;
        const auto flush=[&] {
            if(!word.empty()&&word!="AND") {
                const auto it=std::find(roman.begin(),roman.end(),word);
                if(it==roman.end())invalid=true;
                else {JournalId id{JournalKind::proclamation,proclamation_numbers[static_cast<std::size_t>(it-roman.begin())]};if(std::find(out.ids.begin(),out.ids.end(),id)==out.ids.end())out.ids.push_back(id);}
            }
            word.clear();
        };
        for(char c:text){if(c>='A'&&c<='Z')word+=c;else if(c==' '||c==','||c=='.'||c=='\r'||c=='\n')flush();else invalid=true;}flush();
        if(invalid||out.ids.empty()){out.ids.clear();out.status="invalid-value";}
    }
    return out;
}
}
