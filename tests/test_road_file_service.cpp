#include "platform/native_runtime.hpp"
#include "platform/race_area_runtime.hpp"
#include "platform/retail_asset_store.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>

using namespace outrun::platform;
namespace {
unsigned checks{};
void check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"road_file_service: %s\n",why);std::exit(1);}}
}
int main(){
    auto state=std::make_unique<NativeRuntimeContext>();
    RetailAssetStore store{};state->event_function36.retail_assets=&store;
    check(!native_race_road_file_service(*state),"idle without a request");
    check(native_race_road_tables_46fe50(*state,0u,0u)==1u,"first road table request is pending");
    auto& area=native_race_area(*state);
    check(area.pending_roads.size()==1u&&!area.pending_roads.begin()->second.finished&&area.road_bytes==0u,
          "submit did not read or write the table");
    auto path=area.pending_roads.begin()->second.request.path;
    while(!path.empty()&&(path.front()=='\\'||path.front()=='/'))path.erase(path.begin());
    std::replace(path.begin(),path.end(),'\\','/');
    store.path_cache.emplace("I:"+path,std::vector<std::uint8_t>{4,0,0,0,0x11,0x22,0x33,0x44});
    check(native_race_road_file_service(*state)&&area.pending_roads.begin()->second.finished&&
          area.road_bytes==4u&&area.road_primary[0]==0x11u&&area.road_primary[3]==0x44u,
          "one game-thread service step copies the exact inflated table payload");
    check(!native_race_road_file_service(*state),"completed request is idle before release");
    check(native_race_road_tables_46fe50(*state,0u,0u)==1u&&area.pending_roads.empty()&&
          area.road_loader.status[0]==2u,"next loader step consumes and releases the completed request");
    check(native_race_road_tables_46fe50(*state,0u,0u)==1u&&area.pending_roads.size()==1u,
          "next table slot may submit its own request");
    check(native_race_road_tables_46fc30(*state,0u)==0u&&area.pending_roads.empty()&&
          !native_race_road_file_service(*state),"family reset cancels pending work before any late write");

    auto failed=std::make_unique<NativeRuntimeContext>();
    RetailAssetStore absent{};failed->event_function36.retail_assets=&absent;
    check(native_race_road_tables_46fe50(*failed,0u,0u)==1u,"missing file also starts as pending");
    auto& missing=native_race_area(*failed);
    check(native_race_road_file_service(*failed)&&!missing.pending_roads.begin()->second.error.empty()&&
          !native_race_road_file_service(*failed),"failed read records one error without spinning");
    bool reported=false;
    try{(void)native_race_road_tables_46fe50(*failed,0u,0u);}catch(const std::runtime_error&){reported=true;}
    check(reported,"loader reports the failed read on its next step");
    std::printf("road_file_service: %u checks passed\n",checks);
}
