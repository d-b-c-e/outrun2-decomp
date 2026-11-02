// Input shim: names, strict UTF-8, the profile file and the session file (INPUT-SHIM.md sections 5 and 9), and a game's
// declared extras with the profile's [Extras] (hub artifacts/extra-actions-design-20261014).
#include "shim.hpp"
#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
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
std::vector<std::string> tokens(const std::string& line){
    std::vector<std::string> out;std::string t;
    for(const char c:line){if(c==' '||c=='\t'){if(!t.empty()){out.push_back(t);t.clear();}}else t+=c;}
    if(!t.empty())out.push_back(t);
    return out;
}
bool lower_or_digit(char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9');}
bool alnum(char c){return lower_or_digit(c)||(c>='A'&&c<='Z');}
bool game_id_ok(const std::string& s){
    if(s.empty()||s.size()>32)return false;
    for(const char c:s)if(!lower_or_digit(c)&&c!='-')return false;
    return true;
}
// [a-z][A-Za-z0-9]{0,max-1}
bool name_ok(const std::string& s,std::size_t max){
    if(s.empty()||s.size()>max||!(s[0]>='a'&&s[0]<='z'))return false;
    for(const char c:s)if(!alnum(c))return false;
    return true;
}
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
    case Status::ContextInactive:return "context-inactive";
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

ProfilePlan plan_profile(const Profile& profile,const Capability& capability){
    ProfilePlan r;r.bound_of.fill(-1);
    for(int i=0;i<ActionCount;++i){
        const char* why=capability.unsupported[std::size_t(i)];
        r.base[std::size_t(i)]=why?Status::Unsupported:Status::Unbound;
        r.reasons[std::size_t(i)]=why?why:"";
    }
    r.usable=profile.ok&&profile.section.error.empty();
    if(!r.usable)return r;
    for(const auto& [key,why]:profile.section.invalid){
        r.invalid_keys.push_back(key);
        const int i=action_index(key);
        if(i>=0&&!capability.unsupported[std::size_t(i)]){r.base[std::size_t(i)]=Status::Invalid;r.reasons[std::size_t(i)]=why;}
    }
    for(const auto& e:profile.section.bound){
        const int i=action_index(e.action);
        if(i<0)continue;
        r.in_profile[std::size_t(i)]=true;
        if(capability.unsupported[std::size_t(i)])continue;
        int d=-1;
        for(std::size_t k=0;k<r.devices.size();++k)if(r.devices[k].dev==e.binding.dev)d=int(k);
        if(d>=0&&r.devices[std::size_t(d)].prod!=e.binding.prod){
            r.base[std::size_t(i)]=Status::Invalid;
            r.reasons[std::size_t(i)]="prod "+e.binding.prod+" differs from the other bindings of "+e.binding.dev;
            r.invalid_keys.push_back(e.action);
            continue;
        }
        if(d<0){r.devices.push_back({e.binding.dev,e.binding.prod,e.binding.name});d=int(r.devices.size())-1;}
        r.bound_of[std::size_t(i)]=int(r.bound.size());
        r.bound.push_back({i,e.binding,d});r.base[std::size_t(i)]=Status::Opening;
    }
    return r;
}

// ---- game-scoped extras

bool parse_declaration(const char* bytes,std::size_t len,Declaration& out,std::string& why){
    out=Declaration{};
    if(!bytes||len==0){why="the declaration is empty";return false;}
    if(len>MaxDeclarationBytes){why="the declaration is larger than 4096 bytes";return false;}
    for(std::size_t i=0;i<len;++i){
        const unsigned char c=static_cast<unsigned char>(bytes[i]);
        if(c==0||c>=0x80){why="the declaration is not ASCII text (byte "+std::to_string(i)+")";return false;}
    }
    const std::string text(bytes,len);
    Declaration d;d.hash=sha256(text);
    bool have_game=false,have_contexts=false;
    int number=0;
    for(const auto& raw:lines_of(text)){
        ++number;
        const auto t=tokens(raw);
        if(t.empty())continue;
        const std::string at="line "+std::to_string(number)+": ";
        if(!have_game){
            if(t[0]!="game"||t.size()!=2||!game_id_ok(t[1])){why=at+"the first line must be \"game <id>\" ([a-z0-9-], 1-32 characters)";return false;}
            d.game=t[1];have_game=true;continue;
        }
        if(t[0]=="game"){why=at+"\"game\" comes once, first";return false;}
        if(t[0]=="contexts"){
            if(have_contexts||!d.extras.empty()){why=at+"\"contexts\" comes once, right after \"game\"";return false;}
            if(t.size()<3||(t[1]!="observed"&&t[1]!="advisory")){why=at+"the form is \"contexts observed|advisory <name>...\"";return false;}
            d.mode=t[1]=="observed"?ContextMode::Observed:ContextMode::Advisory;
            for(std::size_t i=2;i<t.size();++i){
                if(!name_ok(t[i],16)){why=at+"context \""+t[i]+"\" is not [a-z][A-Za-z0-9]{0,15}";return false;}
                if(std::find(d.contexts.begin(),d.contexts.end(),t[i])!=d.contexts.end()){why=at+"context \""+t[i]+"\" is listed twice";return false;}
                d.contexts.push_back(t[i]);
            }
            if(d.contexts.size()>std::size_t(MaxExtraContexts)){why=at+"more than 8 contexts";return false;}
            have_contexts=true;continue;
        }
        // <id> <shape> [circle|square] [contexts=a,b]
        ExtraDecl e;e.id=t[0];
        const std::string prefix=d.game+".";
        if(e.id.compare(0,prefix.size(),prefix)!=0||!name_ok(e.id.substr(prefix.size()),32)){
            why=at+"the id must be \""+prefix+"<name>\" with <name> [a-z][A-Za-z0-9]{0,31}";return false;}
        for(const auto& x:d.extras)if(x.id==e.id){why=at+"\""+e.id+"\" is declared twice";return false;}
        if(t.size()<2){why=at+"\""+e.id+"\" has no shape";return false;}
        std::size_t next=2;
        if(t[1]=="digital")e.shape=ctl::Shape::Digital;
        else if(t[1]=="pedal")e.shape=ctl::Shape::Pedal;
        else if(t[1]=="centred")e.shape=ctl::Shape::Centred;
        else if(t[1]=="stick"){
            if(t.size()<3||(t[2]!="circle"&&t[2]!="square")){why=at+"a stick names its range: \"stick circle\" or \"stick square\"";return false;}
            e.shape=ctl::Shape::Centred;e.stick=true;e.circle=t[2]=="circle";e.components=2;next=3;
        }else{why=at+"shape \""+t[1]+"\" is not digital, pedal, centred or stick";return false;}
        for(;next<t.size();++next){
            const auto& opt=t[next];
            if(opt.compare(0,9,"contexts=")!=0||e.contexts){why=at+"unknown or repeated option \""+opt+"\"";return false;}
            if(d.mode==ContextMode::None){why=at+"contexts= needs a \"contexts\" line";return false;}
            std::size_t pos=9;
            for(;;){
                const auto comma=opt.find(',',pos);
                const auto name=opt.substr(pos,comma==std::string::npos?std::string::npos:comma-pos);
                const auto it=std::find(d.contexts.begin(),d.contexts.end(),name);
                if(it==d.contexts.end()){why=at+"context \""+name+"\" is not on the contexts line";return false;}
                const auto bit=1u<<unsigned(it-d.contexts.begin());
                if(e.contexts&bit){why=at+"context \""+name+"\" is named twice";return false;}
                e.contexts|=bit;
                if(comma==std::string::npos)break;
                pos=comma+1;
            }
        }
        e.first_slot=d.slots;d.slots+=e.components;
        if(d.slots>MaxExtraSlots){why=at+"more than 32 slots";return false;}
        d.extras.push_back(e);
    }
    if(!have_game){why="no \"game\" line";return false;}
    if(d.extras.empty()){why="no extra is declared";return false;}
    out=std::move(d);
    return true;
}

ExtrasPlan plan_extras(const std::string& bytes,const Declaration& decl,const std::vector<DeviceInfo>& common,int reads){
    using Form=ExtraComponent::Form;
    ExtrasPlan r;r.extras.resize(decl.extras.size());
    for(auto& x:r.extras){x.base=Status::Unbound;x.reason="not bound";}
    auto all=[&](const std::string& why)->ExtrasPlan&{
        r.error=why;r.devices.clear();r.revision.clear();r.ignored.clear();   // a section error echoes nothing
        for(auto& x:r.extras){x=ExtraPlan{};x.base=Status::Unbound;x.reason=why;}
        return r;
    };
    if(bytes.size()>MaxProfileBytes)return all("dbce-controls.ini is larger than 64 KiB");
    if(!strict_utf8(bytes))return all("dbce-controls.ini is not valid UTF-8");
    const std::string text=bytes.compare(0,3,"\xEF\xBB\xBF")==0?bytes.substr(3):bytes;
    const auto lines=lines_of(text);
    struct Line{std::string text;int number;};
    std::vector<Line> body;
    int section_line=0;bool inside=false;
    for(std::size_t i=0;i<lines.size();++i){
        const int number=int(i)+1;
        const auto t=ctl::trim(lines[i]);
        if(!t.empty()&&t[0]=='['){
            const auto close=t.find(']');
            if(close==std::string::npos)return all("line "+std::to_string(number)+": a section header without ]");
            inside=lower(ctl::trim(t.substr(1,close-1)))=="extras";
            if(inside){
                if(section_line)return all("a second [Extras] at line "+std::to_string(number)+" (the first is at line "+std::to_string(section_line)+")");
                section_line=number;
            }
            continue;
        }
        if(inside)body.push_back({lines[i],number});
    }
    if(!section_line)return all("the profile has no [Extras]");
    struct Key{std::string key,val;int line;};
    std::vector<Key> keys;
    long schema=-1;bool have_schema=false,have_game=false,have_declaration=false,have_revision=false;
    std::string game,declaration;
    for(const auto& l:body){
        const auto t=ctl::trim(l.text);
        if(t.empty()||t[0]==';'||t[0]=='#')continue;
        const auto eq=t.find('=');
        const std::string at="line "+std::to_string(l.number)+" in [Extras]";
        if(eq==std::string::npos)return all(at+" is not key = value");
        const auto key=ctl::trim(t.substr(0,eq)),val=ctl::trim(t.substr(eq+1));
        if(key=="Schema"){if(have_schema)return all(at+": a second Schema");have_schema=true;long v=0;schema=ctl::detail::integer(val,v)?v:-1;continue;}
        if(key=="Game"){if(have_game)return all(at+": a second Game");have_game=true;game=val;continue;}
        if(key=="Declaration"){if(have_declaration)return all(at+": a second Declaration");have_declaration=true;declaration=lower(val);continue;}
        if(key=="Revision"){if(have_revision)return all(at+": a second Revision");have_revision=true;r.revision=val;continue;}   // the echoed identity
        keys.push_back({key,val,l.number});
    }
    if(!have_schema)return all("[Extras] has no Schema");
    if(schema<1||schema>(reads>=2?2:1))
        return all("[Extras] Schema "+std::to_string(schema)+" is not supported (this mod reads "+(reads>=2?"1 or 2":"1")+")");
    if(!have_game||game!=decl.game)return all("[Extras] Game is \""+game+"\", not \""+decl.game+"\"");
    if(!have_declaration||declaration!=hex(decl.hash))return all("[Extras] was written for another declaration");
    const int binding_schema=schema==2&&reads>=2?2:1;

    // Each key into its extra, component and half. 0 none, 1 deliberately empty, 2 bound.
    struct Half{int state=0;ExtraInput in;};
    struct Comp{Half single,neg,pos;};
    struct Work{bool invalid{};std::string why;Comp c[2];std::set<std::string> keys;};
    std::vector<Work> w(decl.extras.size());
    auto bad=[&](std::size_t e,const std::string& why){if(!w[e].invalid){w[e].invalid=true;w[e].why=why;}};
    for(const auto& k:keys){
        std::size_t e=decl.extras.size();std::string rest;
        for(std::size_t i=0;i<decl.extras.size();++i){
            const auto& id=decl.extras[i].id;
            if(k.key==id){e=i;break;}
            if(k.key.size()>id.size()&&k.key.compare(0,id.size(),id)==0&&k.key[id.size()]=='.'){e=i;rest=k.key.substr(id.size()+1);break;}
        }
        if(e==decl.extras.size()){r.ignored.push_back(k.key+": not a declared extra (line "+std::to_string(k.line)+")");continue;}
        const auto& x=decl.extras[e];
        if(!w[e].keys.insert(k.key).second){bad(e,"duplicate key \""+k.key+"\"");continue;}
        int comp=0,half=-1;   // half: -1 the component itself, 0 neg, 1 pos
        if(x.stick){
            if(rest=="x"||rest=="y")comp=rest=="y";
            else if(rest=="x.neg"||rest=="x.pos"||rest=="y.neg"||rest=="y.pos"){comp=rest[0]=='y';half=rest.substr(2)=="pos";}
            else{bad(e,"\""+k.key+"\" is not a component of the stick "+x.id+" (x, y, x.neg, x.pos, y.neg, y.pos)");continue;}
        }else if(x.shape==ctl::Shape::Centred){
            if(rest.empty()){}
            else if(rest=="neg"||rest=="pos")half=rest=="pos";
            else{bad(e,"\""+k.key+"\" is not a component of "+x.id+" (neg, pos)");continue;}
        }else if(!rest.empty()){bad(e,"\""+k.key+"\" is not a component of "+x.id);continue;}
        Half h;h.in.shape=half>=0?ctl::Shape::Digital:x.shape;
        h.state=1;
        if(!k.val.empty()){
            std::string why;
            if(!ctl::parse(k.val,h.in.binding,why,binding_schema)){bad(e,k.key+": "+why);continue;}
            why=ctl::fits(h.in.shape,h.in.binding);
            if(!why.empty()){bad(e,k.key+": "+why);continue;}
            h.in.configured=true;h.state=2;
        }
        auto& c=w[e].c[comp];
        (half<0?c.single:half==0?c.neg:c.pos)=h;
    }
    for(std::size_t e=0;e<decl.extras.size();++e){
        const auto& x=decl.extras[e];
        auto& p=r.extras[e];
        for(int c=0;c<x.components&&!w[e].invalid;++c){
            const auto& wc=w[e].c[c];
            if(wc.single.state&&(wc.neg.state||wc.pos.state))
                bad(e,std::string("two representations of ")+x.id+(x.stick?(c?".y":".x"):"")+": an axis and a button pair");
        }
        if(w[e].invalid){p.base=Status::Invalid;p.reason=w[e].why;continue;}
        bool any=false;
        for(int c=0;c<x.components;++c){
            const auto& wc=w[e].c[c];
            auto& pc=p.component[c];
            if(wc.single.state==2){pc.form=Form::Single;pc.a=wc.single.in;any=true;}
            else if(wc.neg.state==2||wc.pos.state==2){pc.form=Form::Pair;pc.a=wc.neg.in;pc.b=wc.pos.in;any=true;}
            else pc.form=Form::Unbound;   // deliberately empty or absent
        }
        if(any){p.base=Status::Opening;p.reason.clear();}
    }

    // Devices. The common table identifies a device first: an extra that disagrees with it is the invalid one, and the
    // common plan never changes. Between extras-only bindings, an instance named with two products invalidates every
    // extra on it (sana 101153): no first binding wins, so reordering the file or the declaration changes nothing.
    auto each_input=[](ExtraPlan& p,const auto& f){
        for(auto& c:p.component){
            if(c.form==Form::Single){if(c.a.configured)f(c.a);}
            else if(c.form==Form::Pair){if(c.a.configured)f(c.a);if(c.b.configured)f(c.b);}
        }
    };
    auto common_index=[&](const std::string& dev)->int{for(std::size_t k=0;k<common.size();++k)if(common[k].dev==dev)return int(k);return -1;};
    for(auto& p:r.extras){
        if(p.base!=Status::Opening)continue;
        std::string why;
        each_input(p,[&](ExtraInput& in){
            const int k=common_index(in.binding.dev);
            if(k>=0&&common[std::size_t(k)].prod!=in.binding.prod&&why.empty())
                why="prod "+in.binding.prod+" differs from the common binding of "+in.binding.dev;
        });
        if(!why.empty()){p.base=Status::Invalid;p.reason=why;}
    }
    std::map<std::string,std::set<std::string>> products;
    for(auto& p:r.extras)if(p.base==Status::Opening)each_input(p,[&](ExtraInput& in){if(common_index(in.binding.dev)<0)products[in.binding.dev].insert(in.binding.prod);});
    for(auto& p:r.extras){
        if(p.base!=Status::Opening)continue;
        std::string why;
        each_input(p,[&](ExtraInput& in){
            const auto it=products.find(in.binding.dev);
            if(it!=products.end()&&it->second.size()>1&&why.empty())why="the extras name "+in.binding.dev+" with conflicting products";
        });
        if(!why.empty()){p.base=Status::Invalid;p.reason=why;}
    }
    for(auto& p:r.extras){
        if(p.base!=Status::Opening)continue;
        each_input(p,[&](ExtraInput& in){
            int k=common_index(in.binding.dev);
            if(k<0){
                for(std::size_t j=0;j<r.devices.size();++j)if(r.devices[j].dev==in.binding.dev)k=int(common.size()+j);
                if(k<0){r.devices.push_back({in.binding.dev,in.binding.prod,in.binding.name});k=int(common.size()+r.devices.size()-1);}
            }
            in.device=k;
        });
    }
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
