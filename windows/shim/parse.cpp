// Input shim: names, strict UTF-8, the profile file and the session file (INPUT-SHIM.md sections 5 and 9).
#include "shim.hpp"
#include <cstdlib>
namespace dbce::shim {
namespace {
std::vector<std::string> lines_of(const std::string& text){
    std::vector<std::string> out;
    std::size_t pos=0;
    while(pos<=text.size()){
        const auto nl=text.find('\n',pos);
        auto line=text.substr(pos,nl==std::string::npos?std::string::npos:nl-pos);
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        out.push_back(line);
        if(nl==std::string::npos)break;
        pos=nl+1;
    }
    if(!out.empty()&&out.back().empty())out.pop_back();   // the final line end
    return out;
}
std::string lower(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c=char(c-'A'+'a');return s;}
}

int action_index(const std::string& id){
    for(int i=0;i<ActionCount;++i)if(id==ctl::kActions[i])return i;
    return -1;
}
const char* action_name(int i){return i>=0&&i<ActionCount?ctl::kActions[i]:"?";}
bool is_digital(int i){return ctl::shapeOf(action_name(i))==ctl::Shape::Digital;}

const char* status_name(Status s){
    switch(s){
    case Status::Unbound:return "unbound";case Status::Invalid:return "invalid";case Status::Unsupported:return "unsupported";
    case Status::DeviceMissing:return "device-missing";case Status::ProductMismatch:return "product-mismatch";
    case Status::ObjectMissing:return "object-missing";case Status::RangeUnqualified:return "range-unqualified";
    case Status::Opening:return "opening";case Status::ReadPending:return "read-pending";case Status::ReadFailed:return "read-failed";case Status::Stale:return "stale";
    case Status::Rearming:return "rearming";case Status::Live:return "live";case Status::TestNeutral:return "test-neutral";
    }
    return "?";
}
bool is_unavailable(Status s){return s>=Status::DeviceMissing&&s<=Status::Stale;}
bool is_applied(Status s){return s==Status::Rearming||s==Status::Live||s==Status::TestNeutral;}
const char* run_name(Run r){
    switch(r){
    case Run::None:return "none";case Run::Latched:return "latched";case Run::Waiting:return "waiting";case Run::Running:return "running";
    case Run::Completed:return "completed";case Run::Expired:return "expired";case Run::Failed:return "failed";case Run::Inert:return "inert";
    }
    return "?";
}
bool run_terminal(Run r){return r==Run::Completed||r==Run::Expired||r==Run::Failed||r==Run::Inert;}
const char* event_name(EventKind k){
    switch(k){
    case EventKind::EpochBegin:return "epoch-begin";case EventKind::EpochEnd:return "epoch-end";
    case EventKind::BatchAccepted:return "batch-accepted";case EventKind::BatchDuplicate:return "batch-duplicate";
    case EventKind::BatchRefused:return "batch-refused";case EventKind::BatchActivated:return "batch-activated";
    case EventKind::BatchSuperseded:return "batch-superseded";case EventKind::ObjectExpired:return "object-expired";
    case EventKind::RunChanged:return "run-changed";
    case EventKind::ReadPending:return "read-pending";
    }
    return "?";
}

// RFC 3629: no overlong forms, no surrogates, nothing above U+10FFFF; NUL is refused too (these are text files).
bool strict_utf8(const std::string& s){
    const auto* p=reinterpret_cast<const unsigned char*>(s.data());
    const std::size_t n=s.size();
    for(std::size_t i=0;i<n;){
        const unsigned c=p[i];
        if(c==0)return false;
        if(c<0x80){++i;continue;}
        std::size_t len;unsigned lo=0x80,hi=0xBF;
        if(c>=0xC2&&c<=0xDF)len=2;
        else if(c==0xE0){len=3;lo=0xA0;}
        else if(c>=0xE1&&c<=0xEC)len=3;
        else if(c==0xED){len=3;hi=0x9F;}
        else if(c>=0xEE&&c<=0xEF)len=3;
        else if(c==0xF0){len=4;lo=0x90;}
        else if(c>=0xF1&&c<=0xF3)len=4;
        else if(c==0xF4){len=4;hi=0x8F;}
        else return false;
        if(i+len>n)return false;
        if(p[i+1]<lo||p[i+1]>hi)return false;
        for(std::size_t k=2;k<len;++k)if(p[i+k]<0x80||p[i+k]>0xBF)return false;
        i+=len;
    }
    return true;
}

Profile read_profile(const std::string& bytes,int reads){
    Profile r;
    r.file_hash=sha256(bytes);
    if(bytes.size()>MaxProfileBytes){r.error="dbce-controls.ini is larger than 64 KiB";return r;}
    if(!strict_utf8(bytes)){r.error="dbce-controls.ini is not valid UTF-8";return r;}
    const std::string text=bytes.compare(0,3,"\xEF\xBB\xBF")==0?bytes.substr(3):bytes;
    const auto lines=lines_of(text);
    int controls_line=0;
    bool inside=false;
    std::vector<std::string> body;
    std::vector<std::pair<std::string,int>> keys;
    for(std::size_t i=0;i<lines.size();++i){
        const int number=int(i)+1;
        const auto t=ctl::trim(lines[i]);
        if(!t.empty()&&t[0]=='['){
            const auto close=t.find(']');
            if(close==std::string::npos){r.error="line "+std::to_string(number)+": a section header without ]";return r;}
            inside=lower(ctl::trim(t.substr(1,close-1)))=="controls";
            if(inside){
                if(controls_line){r.error="a second [Controls] at line "+std::to_string(number)+" (the first is at line "+std::to_string(controls_line)+")";return r;}
                controls_line=number;
            }
            continue;
        }
        if(!inside)continue;
        body.push_back(lines[i]);
        if(t.empty()||t[0]==';'||t[0]=='#')continue;
        const auto eq=t.find('=');
        if(eq==std::string::npos){r.error="line "+std::to_string(number)+" in [Controls] is not key = value";return r;}
        const auto key=ctl::trim(t.substr(0,eq));
        for(const auto& k:keys)if(k.first==key){
            r.error="duplicate key \""+key+"\" in [Controls] at lines "+std::to_string(k.second)+" and "+std::to_string(number);return r;}
        keys.push_back({key,number});
    }
    if(!controls_line){r.error="dbce-controls.ini has no [Controls]";return r;}
    r.section=ctl::parseSection(body,reads);   // reads 1 (the default): a Schema 2 section applies nothing
    r.ok=true;
    return r;
}

bool parse_session(const std::string& bytes,std::uint64_t now_unix,Session& out,std::string& why){
    out={};
    if(bytes.size()>512){why="inject.on is larger than 512 bytes";return false;}
    if(!strict_utf8(bytes)){why="inject.on is not valid UTF-8";return false;}
    bool have_nonce=false,have_expiry=false;
    for(const auto& raw:lines_of(bytes)){
        const auto line=ctl::trim(raw);
        if(line.empty())continue;
        const auto eq=line.find('=');
        if(eq==std::string::npos){why="inject.on line \""+line+"\" is not key=value";return false;}
        const auto key=line.substr(0,eq),value=line.substr(eq+1);
        if(key=="nonce"){
            if(have_nonce){why="inject.on names nonce= twice";return false;}
            have_nonce=true;out.nonce=value;
        }else if(key=="expires"){
            if(have_expiry){why="inject.on names expires= twice";return false;}
            have_expiry=true;
            if(value.empty()||value.size()>12||value.find_first_not_of("0123456789")!=std::string::npos){why="expires= is not unix seconds";return false;}
            out.expires_unix=std::strtoull(value.c_str(),nullptr,10);
        }else{why="inject.on has an unknown key \""+key+"\" (cleanup ownership belongs in inject.receipt)";return false;}
    }
    if(!have_nonce||out.nonce.size()<8||out.nonce.size()>64){why="inject.on names no nonce= of 8-64 letters/digits";return false;}
    for(char c:out.nonce)if(!((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z'))){why="the nonce is not letters/digits";return false;}
    if(!have_expiry){why="inject.on names no expires=";return false;}
    if(out.expires_unix<=now_unix){why="the test session in inject.on has expired";return false;}
    if(out.expires_unix-now_unix>3600){why="the test session in inject.on ends more than an hour from now";return false;}
    return true;
}

BatchResult check_batch(const std::string& bytes,const BatchContext& ctx){
    BatchResult r;
    r.verdict=BatchVerdict::Refused;
    if(bytes.size()>MaxBatchBytes){r.why="inject.txt is larger than 4096 bytes";return r;}
    if(!strict_utf8(bytes)){r.why="inject.txt is not valid UTF-8";return r;}
    std::vector<std::string> lines;
    for(const auto& l:lines_of(bytes)){const auto t=ctl::trim(l);if(!t.empty())lines.push_back(t);}
    if(lines.empty()){r.why="inject.txt is empty";return r;}
    // 1. the session
    if(lines[0]!="nonce="+ctx.nonce){r.why="inject.txt does not name this session's nonce";return r;}
    // 2. identity: the full hash of the bytes
    auto& g=r.generation;
    g.hash=sha256(bytes);
    if(lines.size()<2||lines[1].compare(0,6,"batch=")!=0){r.why="the second line must be batch=<n>";return r;}
    const auto digits=lines[1].substr(6);
    if(digits.empty()||digits.size()>15||digits.find_first_not_of("0123456789")!=std::string::npos||digits[0]=='0'){
        r.why="batch= is not a positive whole number";return r;}
    g.n=std::strtoull(digits.c_str(),nullptr,10);
    // 3. / 4. duplicates and order
    if(ctx.have_last){
        if(g.n==ctx.last_n){
            if(g.hash==ctx.last_hash){r.verdict=BatchVerdict::Duplicate;r.why="an exact duplicate of batch "+digits;return r;}
            r.why="batch "+digits+" repeated with different bytes";return r;
        }
        if(g.n<ctx.last_n){r.why="batch "+digits+" is lower than the last accepted batch "+std::to_string(ctx.last_n);return r;}
    }
    // 5. the whole batch
    const std::size_t count=lines.size()-2;
    if(!count){r.why="batch "+digits+" has no commands";return r;}
    if(count>std::size_t(MaxBatchCommands)){r.why="batch "+digits+" has more than 32 commands";return r;}
    if(count==1&&lines[2]=="end"){g.end=true;r.verdict=BatchVerdict::Accepted;return r;}
    for(std::size_t k=2;k<lines.size();++k){
        const auto where="command "+std::to_string(k-1)+": ";
        if(lines[k]=="end"){r.why=where+"end must be the batch's only command";return r;}
        ctl::InjectCommand ic;std::string why;
        if(!ctl::parseInject(lines[k],ctx.profile,ic,why)){r.why=where+why;return r;}
        const OwnedDevice* dev=nullptr;
        for(const auto& o:ctx.owned)if(o.dev==ic.binding.dev)dev=&o;
        if(!dev){r.why=where+ic.binding.dev+" is not a device of the admitted workload";return r;}
        const int i=ic.binding.index;const auto& c=dev->caps;
        switch(ic.binding.kind){
        case ctl::Kind::Axis:
            if(!c.axis[std::size_t(i)]){r.why=where+"the device has no axis "+std::to_string(i);return r;}
            if(!c.range_ok[std::size_t(i)]||c.max[std::size_t(i)]<=c.min[std::size_t(i)]){r.why=where+"axis "+std::to_string(i)+" has no readable range";return r;}
            break;
        case ctl::Kind::Button:if(!c.button.test(std::size_t(i))){r.why=where+"the device has no button "+std::to_string(i);return r;}break;
        case ctl::Kind::Hat:if(!c.pov[std::size_t(i)]){r.why=where+"the device has no hat "+std::to_string(i);return r;}break;
        default:r.why=where+"bad kind";return r;
        }
        for(const auto& prior:g.commands)if(prior.dev==ic.binding.dev&&prior.kind==ic.binding.kind&&prior.index==i){
            r.why=where+"the same object is named twice";return r;}
        Command cmd;
        cmd.dev=ic.binding.dev;cmd.kind=ic.binding.kind;cmd.index=i;cmd.angle=ic.binding.angle;cmd.raw=ic.raw;
        cmd.bind_min=ic.binding.min;cmd.bind_max=ic.binding.max;cmd.ms=ic.ms;cmd.action=ic.action;
        (cmd.action.empty()?g.used_raw:g.used_action)=true;
        g.commands.push_back(cmd);
    }
    if(g.commands.size()>std::size_t(MaxRunningObjects)){r.why="batch "+digits+" names more than 16 objects";return r;}
    r.verdict=BatchVerdict::Accepted;
    return r;
}
}
