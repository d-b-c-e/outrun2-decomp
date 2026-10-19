#include "desktop_bindings.hpp"
#include "controls_store.hpp"
#include "../vendor/controls/dbce_controls_store_win.hpp"
#include <mutex>
#include <cstdio>

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
    bool ordinary{},opened{},pending_restart{},restart_required{};
    std::string action{"steer"},message;
    std::unique_ptr<edit::ProfileDraft> draft;
    edit::CaptureFlow capture{action_kind};
    edit::DeviceSelection selection;
    explicit Impl(const std::string& fixed,bool allowed):path(std::filesystem::absolute(std::filesystem::u8path(fixed)).lexically_normal()),ordinary(allowed){}
    bool editable(bool can_edit){
        if(!ordinary||!opened||!draft||restart_required||!can_edit){
            message=restart_required?"Restart before editing again; the backup and draft are kept":
                "Editing needs ordinary shared input, the Controls page and game focus";
            return false;
        }
        return true;
    }
    std::uintptr_t host()const{return reinterpret_cast<std::uintptr_t>(this);}
    void refresh(double now,bool can_edit){
        const bool listening=!capture.action().empty();
        capture.refresh(now,can_edit&&ordinary&&opened&&!restart_required);
        if(listening&&capture.action().empty())message=capture.message();
        selection.reconcile(host(),capture.devices(now),now);
    }
};
Editor::Editor(const std::string& path,bool ordinary):impl_(std::make_unique<Impl>(path,ordinary)){}
Editor::~Editor()=default;
bool Editor::opened()const{auto& s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);return s.opened;}
bool Editor::open(){
    auto& s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);
    if(!s.ordinary||s.restart_required){s.message="Binding editing is unavailable in tests/playback or until restart";return false;}
    if(s.opened)return true;
    try{
        s.draft=std::make_unique<edit::ProfileDraft>(edit::store_detail::read_path(s.path),action_kind,false,ControlsReads);
        s.capture.cancel();s.selection.select_saved();s.opened=true;s.message="Editing saved bindings; active controls are unchanged";
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
View Editor::view(double now,bool can_edit){
    auto& s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);s.refresh(now,can_edit);
    View out;out.opened=s.opened;out.pending_restart=s.pending_restart;out.restart_required=s.restart_required;
    out.action=s.action;out.message=s.capture.action().empty()?s.message:s.capture.message();
    out.listening=!s.capture.action().empty();out.selection=s.selection.mode();out.devices=s.capture.devices(now);
    if(s.selection.selected())out.selected_instance=s.selection.selected()->instance;
    if(s.draft){out.dirty=s.draft->has_draft();out.transmission=s.draft->transmission();out.binding=s.draft->binding(s.action);}
    return out;
}
void Editor::observe(const std::vector<edit::AcceptedObservation>& rows,double now,bool can_edit){
    auto& s=*impl_;std::lock_guard<std::mutex> lock(s.mutex);
    const bool eligible=can_edit&&s.ordinary&&s.opened&&!s.restart_required;
    const bool listening=!s.capture.action().empty();bool captured=false;
    for(const auto& old:s.capture.devices(now))if(std::none_of(rows.begin(),rows.end(),[&](const auto& row){return row.instance==old.instance;}))
        s.capture.remove(old.instance,"Capture device is unavailable; the draft is kept");
    for(const auto& row:rows){
        s.capture.observe(row,now,eligible);
        if(auto result=s.capture.take_result(now,eligible);result&&s.draft){
            captured=true;
            std::string why;
            if(s.draft->set(result->action,result->binding,why))s.message="Binding captured in the draft; Save to keep it";
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
