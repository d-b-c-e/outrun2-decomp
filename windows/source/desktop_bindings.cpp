#include "desktop_bindings.hpp"
#include "controls_store.hpp"
#include "../vendor/controls/dbce_controls_store_win.hpp"
#include <mutex>
#include <cstdio>
#include <cstring>

namespace outrun::desktop::bindings {
namespace ctl=dbce::controls;
namespace sh=dbce::shim;
std::vector<edit::AcceptedObservation> accepted_observations(const sh::Frame& frame,
    const std::vector<sh::DeviceReport>& reports,double now){
    std::vector<edit::AcceptedObservation> out;
    if(!std::isfinite(now)||!frame.seq||frame.latched||frame.samples.size()>64||reports.size()>64||
       frame.run==sh::Run::Failed||frame.run==sh::Run::Expired||frame.run==sh::Run::Completed||frame.run==sh::Run::Inert)return out;
    for(const auto& sample:frame.samples){
        if(sample.device>=reports.size()||!sample.epoch||!sample.seq||!sample.valid||sample.stale||sample.poll!=sh::Call::Ok||sample.state!=sh::Call::Ok||
           sample.injected.any()||sample.read_end_us<=0||sample.read_end_us>frame.boundary_us||
           frame.boundary_us-sample.read_end_us>100'000)continue;
        const auto& report=reports[sample.device];
        if(!report.opened||report.epoch!=sample.epoch||report.identity.interface_path.empty())continue;
        edit::AcceptedObservation row;
        row.instance=report.identity.dev;row.product=report.identity.prod;row.name=report.identity.name;
        row.identity_key=report.identity.interface_path;row.epoch=sample.epoch;row.sequence=sample.seq;row.seen=now;
        for(std::size_t i=0;i<8;++i){
            if(report.caps.axis[i])row.axis_mask|=1u<<i;
            if(report.caps.range_ok[i])row.range_mask|=1u<<i;
            row.minimum[i]=report.caps.min[i];row.maximum[i]=report.caps.max[i];row.sample.axes[i]=sample.physical.axes[i];
        }
        row.button_mask=report.caps.button;
        for(std::size_t i=0;i<128;++i)row.sample.buttons[i]=(sample.physical.buttons[i]&0x80u)!=0;
        for(std::size_t i=0;i<4;++i){
            if(report.caps.pov[i])row.hat_mask|=1u<<i;
            const auto angle=sample.physical.povs[i];
            row.sample.hats[i]=angle==0xffffffffu?-1:angle<36000u?std::int32_t(angle):36000;
        }
        // Core supplied the accepted sample and adopted-epoch caps; malformed metadata still cannot enter capture.
        if(edit::nonzero_guid(row.instance)&&edit::nonzero_guid(row.product)&&edit::ranges_valid(row)&&
           edit::valid_utf8(row.name)&&edit::valid_utf8(row.identity_key))out.push_back(std::move(row));
    }
    return out;
}
std::vector<edit::AcceptedObservation> native_observations(const std::vector<NativeSample>& samples,double now){
    std::vector<edit::AcceptedObservation> out;
    if(!std::isfinite(now)||samples.size()>64)return out;
    for(const auto& s:samples){
        if(!s.epoch||!s.sequence)continue;
        edit::AcceptedObservation row;
        row.instance=s.dev;row.product=s.prod;row.name=s.name;
        // The Native reader has no interface path: its key is the instance it opened, renewed by the epoch at each reopen.
        row.identity_key="native:"+s.dev;row.epoch=s.epoch;row.sequence=s.sequence;row.seen=now;
        // shortcut: DIDEVCAPS gives axis counts, not which axes exist, so all eight are offered at the reader's 0..65535; an
        // absent axis reads a constant 0 and never captures. Upgrade to GetObjectInfo if a device reports a phantom axis.
        row.axis_mask=row.range_mask=0xffu;
        for(std::size_t i=0;i<8;++i){row.minimum[i]=0;row.maximum[i]=65535;row.sample.axes[i]=s.axes[i];}
        for(std::size_t i=0;i<std::min<std::size_t>(s.buttons,128);++i){row.button_mask.set(i);row.sample.buttons[i]=(s.button_state[i]&0x80u)!=0;}
        for(std::size_t i=0;i<std::min<std::size_t>(s.povs,4);++i){
            row.hat_mask|=1u<<i;
            const auto angle=s.pov_state[i];
            row.sample.hats[i]=(angle&0xffffu)==0xffffu?-1:angle<36000u?std::int32_t(angle):36000;
        }
        // A read outside the reader's range (its range set failed) or a malformed identity never enters capture.
        if(edit::nonzero_guid(row.instance)&&edit::nonzero_guid(row.product)&&edit::ranges_valid(row)&&edit::valid_utf8(row.name))
            out.push_back(std::move(row));
    }
    return out;
}
bool action_kind(const std::string& action,ctl::Kind kind){
    if(unavailable_reason(action))return false;
    switch(ctl::shapeOf(action)){
    case ctl::Shape::Centred:return kind==ctl::Kind::Axis;
    case ctl::Shape::Pedal:return kind==ctl::Kind::Axis||kind==ctl::Kind::Button;
    case ctl::Shape::Digital:return kind==ctl::Kind::Button||kind==ctl::Kind::Hat;
    default:return false;
    }
}
struct Editor::Impl {
    std::mutex mutex;
    std::filesystem::path path;
    bool ordinary{},opened{},pending_restart{},restart_required{},file_changed{};
    std::string action{"steer"},message;
    std::unique_ptr<edit::ProfileDraft> draft;
    edit::CaptureFlow capture{action_kind};
    edit::DeviceSelection selection;
    explicit Impl(const std::string& fixed,bool allowed):path(std::filesystem::absolute(std::filesystem::u8path(fixed)).lexically_normal()),ordinary(allowed){}
    bool editable(bool can_edit){
        if(file_changed){message="Stop FFB saved Off. Binding edits are kept but cannot be saved; cancel editing and reopen to use the updated file.";return false;}
        if(!ordinary||!opened||!draft||restart_required||!can_edit){
            message=restart_required?"Restart before editing again; the backup and draft are kept":
                "Editing needs ordinary input, the Controls page and game focus";
            return false;
        }
        return true;
    }
    std::uintptr_t host()const{return reinterpret_cast<std::uintptr_t>(this);}
    void refresh(double now,bool can_edit){
        const bool listening=!capture.action().empty();
        capture.refresh(now,can_edit&&ordinary&&opened&&!restart_required&&!file_changed);
        if(listening&&capture.action().empty())message=capture.message();
        selection.reconcile(host(),capture.devices(now),now);
    }
};
// Whether a [Controls] section holds a Schema key, read as the shared draft reads it (leading BOM, CR/LF, trimmed
// lines, case-insensitive header, exact key). Without one there are no saved controls for the editor to open.
static bool has_controls_schema(std::string text){
    if(text.compare(0,3,"\xEF\xBB\xBF")==0)text.erase(0,3);
    bool inside=false;
    for(std::size_t at=0;;){
        const std::size_t n=text.find('\n',at);
        const auto line=ctl::trim(text.substr(at,n==std::string::npos?std::string::npos:n-at));
        if(!line.empty()&&line.front()=='[')inside=_stricmp(line.c_str(),"[controls]")==0;
        else if(inside&&line.find('=')!=std::string::npos&&ctl::trim(line.substr(0,line.find('=')))=="Schema")return true;
        if(n==std::string::npos)return false;
        at=n+1;
    }
}
const char* const FirstTimeSetup=
    "No saved controls to edit yet. Set up the wheel once in Wheelkit; after that, Edit bindings changes them here. "
    "With Native input, a Tweaks [DirectInput] setup is imported at start while its wheel is connected.";

Editor::Editor(const std::string& path,bool ordinary):impl_(std::make_unique<Impl>(path,ordinary)){}
Editor::~Editor()=default;
bool Editor::opened()const{auto& s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);return s.opened;}
bool Editor::open(){
    auto& s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);
    if(!s.ordinary||s.restart_required){s.message="Binding editing is unavailable in tests/playback or until restart";return false;}
    if(s.opened)return true;
    try{
        auto text=edit::store_detail::read_path(s.path);
        if(!has_controls_schema(text)){s.message=FirstTimeSetup;return false;}   // the draft would refuse it too
        s.draft=std::make_unique<edit::ProfileDraft>(std::move(text),action_kind,false,ControlsReads);
        s.capture.cancel();s.selection.select_saved();s.opened=true;s.file_changed=false;s.message="Editing saved bindings; active controls are unchanged";
        return true;
    }catch(const std::exception& e){s.message=std::string("Cannot edit saved bindings: ")+e.what();return false;}
}
void Editor::cancel(){
    auto& s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);
    if(s.restart_required){s.capture.cancel();s.message="Restart before editing again; the backup and uncertain draft are kept";return;}
    s.capture.cancel();if(s.draft)s.draft->discard();s.opened=false;s.selection.select_saved();
    s.message="Local binding edits canceled; saved and active controls are unchanged";
}
void Editor::hide(){
    auto& s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);
    if(!s.capture.action().empty()){s.capture.cancel();s.message="Listening canceled; the binding draft is kept";}
}
void Editor::saved_file_changed(){
    auto& s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);
    if(!s.opened)return;
    s.capture.cancel();s.file_changed=true;
    s.message="Stop FFB saved Off. Binding edits are kept but cannot be saved; cancel editing and reopen to use the updated file.";
}
View Editor::view(double now,bool can_edit){
    auto& s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);s.refresh(now,can_edit);
    View out;out.opened=s.opened;out.pending_restart=s.pending_restart;out.restart_required=s.restart_required;
    out.file_changed=s.file_changed;
    out.action=s.action;out.message=s.capture.action().empty()?s.message:s.capture.message();
    out.listening=!s.capture.action().empty();out.selection=s.selection.mode();out.devices=s.capture.devices(now);
    if(s.selection.selected())out.selected_instance=s.selection.selected()->instance;
    if(s.draft){out.dirty=s.draft->has_draft();out.transmission=s.draft->transmission();out.binding=s.draft->binding(s.action);}
    return out;
}
void Editor::observe(const std::vector<edit::AcceptedObservation>& rows,double now,bool can_edit){
    auto& s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);
    const bool eligible=can_edit&&s.ordinary&&s.opened&&!s.restart_required&&!s.file_changed;
    const bool listening=!s.capture.action().empty();bool captured=false;
    for(const auto& old:s.capture.devices(now))if(std::none_of(rows.begin(),rows.end(),[&](const auto& row){return row.instance==old.instance;}))
        s.capture.remove(old.instance,"Capture device is unavailable; the draft is kept");
    for(const auto& row:rows){
        s.capture.observe(row,now,eligible);
        if(auto result=s.capture.take_result(now,eligible);result&&s.draft){
            captured=true;
            std::string why;
            auto binding=result->binding;
            // A centred axis takes its direction only from the explicit inversion (normalize ignores rest and travel),
            // which capture cannot measure: re-capturing the same device axis keeps it, so steering never silently reverses.
            if(ctl::shapeOf(result->action)==ctl::Shape::Centred)
                if(const auto old=s.draft->binding(result->action);old&&old->kind==ctl::Kind::Axis&&old->dev==binding.dev&&
                   old->prod==binding.prod&&old->index==binding.index)binding.inverted=old->inverted;
            if(s.draft->set(result->action,binding,why))s.message="Binding captured in the draft; Save to keep it";
            else s.message=why;
        }
    }
    s.refresh(now,eligible);
    if(listening&&!captured&&s.capture.action().empty())s.message=s.capture.message();
}
bool Editor::select_action(const std::string& action){
    auto& s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);
    if(!action_kind(action,ctl::Kind::Axis)&&!action_kind(action,ctl::Kind::Button)&&!action_kind(action,ctl::Kind::Hat)){
        s.message="This action is unavailable in OutRun";return false;
    }
    s.capture.cancel();s.selection.select_saved();s.action=action;return true;
}
bool Editor::select_device(const std::optional<edit::AcceptedObservation>& row,double now,bool can_edit){
    auto& s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);if(!s.editable(can_edit))return false;
    s.capture.cancel();
    if(!row){s.selection.select_saved();return true;}
    return s.selection.select_device(s.host(),*row,now);
}
bool Editor::begin_capture(double now,bool can_edit){
    auto& s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);if(!s.editable(can_edit))return false;
    s.refresh(now,can_edit);
    std::string instance;
    if(s.selection.mode()==edit::SelectionMode::Explicit&&s.selection.selected())instance=s.selection.selected()->instance;
    else if(s.selection.mode()==edit::SelectionMode::Saved){
        const auto bound=s.draft->binding(s.action);const auto devices=s.capture.devices(now);
        if(bound&&std::any_of(devices.begin(),devices.end(),[&](const auto& row){return row.instance==bound->dev&&row.product==bound->prod;}))instance=bound->dev;
    }
    if(instance.empty()){s.message="Choose a connected capture device; an unavailable selection is never replaced automatically";return false;}
    const bool ok=s.capture.begin(s.action,instance,now,true);s.message=s.capture.message();return ok;
}
void Editor::stop_capture(){
    auto& s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);s.capture.cancel();s.message="Listening canceled; the draft is kept";
}
bool Editor::clear(bool can_edit){
    auto& s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);if(!s.editable(can_edit))return false;
    s.capture.cancel();std::string why;const bool ok=s.draft->set(s.action,std::nullopt,why);
    s.message=ok?"Binding cleared in the draft; Save to keep it":why;return ok;
}
bool Editor::invert(bool value,bool can_edit){
    auto& s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);if(!s.editable(can_edit))return false;
    auto binding=s.draft->binding(s.action);
    if(!binding||binding->kind!=ctl::Kind::Axis){s.message="Choose an axis binding to invert";return false;}
    s.capture.cancel();binding->inverted=value;std::string why;const bool ok=s.draft->set(s.action,*binding,why);
    s.message=ok?"Input direction changed in the draft; Save to keep it":why;return ok;
}
bool Editor::transmission(const std::string& value,bool can_edit){
    auto& s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);if(!s.editable(can_edit))return false;
    s.capture.cancel();std::string why;const bool ok=s.draft->set_transmission(value,why);
    s.message=ok?"Shifter mode changed in the draft; game Manual/Automatic selection is separate":why;return ok;
}
bool Editor::save(bool can_edit){
    auto& s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);if(!s.editable(can_edit))return false;
    if(!s.capture.action().empty()){s.message="Finish or stop listening before saving";return false;}
    const auto result=edit::save(*s.draft,s.path,"f6-"+std::to_string(GetTickCount64()));
    s.message=result.message;
    std::printf("bindings: saved=%u replaced=%u backup=%s; %s\n",unsigned(result.saved),unsigned(result.replaced),
        result.backup.u8string().c_str(),result.message.c_str());std::fflush(stdout);
    if(result.saved){s.pending_restart=true;s.opened=false;s.selection.select_saved();}
    else if(result.replaced)s.restart_required=true;
    return result.saved;
}
}
