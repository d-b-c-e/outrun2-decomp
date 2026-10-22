#include "desktop_support_report.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace {
int checks{};
void check(bool ok,const char* message){
    ++checks;if(!ok){std::fprintf(stderr,"support adapter FAIL: %s\n",message);std::exit(1);}
}
}
int main(int argc,char** argv){
    using namespace outrun::desktop::support;
    namespace fs=std::filesystem;
    check(argc==2,"private fixture directory supplied");
    const auto root=fs::u8path(argv[1]);
    check(fs::create_directory(root),"fresh private fixture directory");
    const auto log=root/"runtime.log",exported=root/"support-report.txt";
    {std::ofstream f(log);f<<"private-fixture-marker\nauth_token=private-fixture-secret\n";}
    int opens{},pickers{};bool browser_ok=true;
    std::string opened_url;
    std::optional<fs::path> destination=exported;
    Actions actions{[&]{++pickers;return destination;},[&](const std::string& url){++opens;opened_url=url;return browser_ok;}};
    Draft draft(true,log);
    check(!draft.open(actions)&&opens==0&&pickers==0,"explicit action cannot bypass preparation");
    draft.inputs("Fixture title","Description",false);
    check(draft.prepare(),"short preparation");
    const auto short_preview=draft.report()->preview();
    check(short_preview.find("No logs included")!=std::string::npos&&short_preview.find("private-fixture-marker")==std::string::npos,"runtime log opt-in only");
    check(short_preview.find("Unversioned desktop source build; compiled")!=std::string::npos,"truthful compiled metadata");
    check(!draft.open(actions)&&opens==0,"review required");
    draft.review(true);
    check(draft.open(actions)&&opens==1&&pickers==0,"short explicit draft action only");
    check(opened_url.find("https://github.com/d-b-c-e/outrun2-decomp/issues/new?")==0,"fixed product draft destination");
    check(!fs::exists(exported),"short draft writes nothing");
    draft.inputs("Changed title","Description",false);
    check(!draft.report()&&!draft.reviewed()&&!draft.submission(),"title invalidates preview and consent");
    check(draft.prepare(),"prepare changed title");draft.review(true);
    draft.inputs("Changed title","Changed description",false);
    check(!draft.report()&&!draft.reviewed(),"description invalidates review");
    check(draft.prepare(),"prepare changed description");draft.review(true);
    draft.inputs("Changed title","Changed description",true);
    check(!draft.report()&&!draft.reviewed(),"selection invalidates review");
    check(draft.prepare(),"explicit known runtime log");
    check(draft.report()->selected_log_count()==1&&draft.report()->preview().find("private-fixture-marker")!=std::string::npos,"only selected log included");
    check(draft.report()->preview().find("private-fixture-secret")==std::string::npos,"qualified token redaction used");
    const auto frozen=draft.report()->preview();draft.review(true);
    fs::remove(log);
    check(draft.report()->preview()==frozen,"review is immutable after file removal");
    check(draft.open(actions),"reviewed dispatch never rereads the selected log");
    check(!draft.prepare()&&!draft.report()&&!draft.reviewed()&&!draft.submission(),"missing selected log clears old preview and review");
    check(!draft.message().empty()&&draft.message().find("private-fixture")==std::string::npos,"failure feedback has no raw data");
    const auto failed_opens=opens;
    check(!draft.open(actions)&&opens==failed_opens,"failed preparation cannot dispatch an old draft");
    draft.inputs("Changed title","Changed description",false);
    check(draft.prepare(),"deselecting a missing log permits no-log report");draft.review(true);
    draft.review(false);check(!draft.reviewed()&&!draft.submission(),"consent can be revoked");
    draft.inputs("","Description",false);
    check(!draft.prepare()&&!draft.report(),"invalid input preparation refuses");
    draft.inputs("Long fixture",std::string(6000,'x'),false);
    check(draft.prepare(),"long draft preparation");draft.review(true);
    check(draft.submission()->needs_attachment,"long draft attachment fallback");
    const auto complete=draft.report()->preview();
    destination.reset();const auto before=opens;
    check(!draft.open(actions)&&opens==before&&!fs::exists(exported),"export cancellation opens and writes nothing");
    destination=exported;
    {std::ofstream f(exported);f<<"preserve existing";}
    check(!draft.open(actions)&&opens==before,"existing export refused before browser");
    {std::ifstream f(exported);std::string value;std::getline(f,value);check(value=="preserve existing","no overwrite");}
    fs::remove(exported);
    destination=root/"missing"/"report.txt";
    check(!draft.open(actions)&&opens==before&&!fs::exists(root/"missing"),"missing export parent refused without folder creation");
    destination=root/"report.ini";
    check(!draft.open(actions)&&!fs::exists(*destination),"only txt export accepted");
    destination=exported;
    check(draft.open(actions)&&opens==before+1,"long export precedes explicit browser action");
    {std::ifstream f(exported,std::ios::binary);const std::string value((std::istreambuf_iterator<char>(f)),{});check(value==complete,"full reviewed snapshot exported");}
    check(opened_url.size()<=dbce::support::browser_url_limit&&draft.message().find("Attaching uploads")!=std::string::npos,"bounded fallback and immediate-upload instructions");
    draft.inputs("Short retry","Description",false);check(draft.prepare(),"short retry preparation");draft.review(true);browser_ok=false;
    check(!draft.open(actions)&&draft.message().find("Browser launch failed")!=std::string::npos,"browser failure feedback");
    draft.inputs("Long retry",std::string(6000,'x'),false);check(draft.prepare(),"long retry preparation");draft.review(true);
    actions.choose_export=[]()->std::optional<fs::path>{throw std::runtime_error("private raw picker error");};
    check(!draft.open(actions)&&draft.message().find("Export dialog failed")!=std::string::npos&&draft.message().find("private raw")==std::string::npos,"picker failure is safe feedback");
    Draft fenced(false,log);fenced.inputs("Title","Description",true);fenced.review(true);
    check(!fenced.prepare()&&!fenced.report()&&!fenced.reviewed(),"latched test/playback preparation fenced");
    const auto fenced_opens=opens;
    check(!fenced.open(actions)&&opens==fenced_opens,"fenced actions never dispatch");
    fs::remove(exported);check(fs::remove(root),"private fixtures cleaned");
    std::printf("desktop support report: %d checks passed; no devices/game/browser/network\n",checks);
}
