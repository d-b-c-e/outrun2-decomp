#include "platform/camera_mount_fit.hpp"
#include "platform/frontend_vehicle_data.hpp"
#include "platform/embedded_vehicle_draw_data.hpp"
#include "platform/retail_asset_store.hpp"
#include "driving/pc_driving.hpp"
#include <cstdio>
#include <cstdlib>
using namespace outrun;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"camera mounts line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static assets::PreviewMesh body(const std::vector<std::array<float,2>>& profile){
    assets::PreviewMesh mesh;
    for(auto p:profile)for(float x:{-1.f,1.f}){assets::PreviewVertex v;v.position={x,p[1],-p[0]};mesh.vertices.push_back(v);}
    for(unsigned i=0;i+1<profile.size();++i){const unsigned a=i*2;mesh.indices.insert(mesh.indices.end(),{a,a+1,a+2,a+1,a+3,a+2});}
    return mesh;
}
int main(int argc,char** argv){
    const auto car=body({{2.2f,.2f},{2.f,.7f},{.5f,.85f},{0.f,1.4f},{-1.2f,1.4f},{-2.f,.7f}});
    const auto fit=platform::camera_mount_fit({car});
    CHECK(fit.valid&&fit.profile_found&&!fit.cab_over);
    CHECK(fit.glass>.3f&&fit.glass<.7f&&fit.hood>.8f&&fit.hood<1.f);
    CHECK(fit.bonnet[0]==0.f&&fit.bonnet[1]>fit.hood&&std::fabs(fit.bonnet[2]-fit.glass-.1f)<1e-5f);
    CHECK(std::fabs(fit.bumper[2]-fit.front-.05f)<1e-5f&&fit.bumper[1]<=fit.bottom+.45f);
    const auto van=platform::camera_mount_fit({body({{2.f,.1f},{1.9f,2.4f},{-2.f,2.4f}})});
    CHECK(van.valid&&van.profile_found&&van.cab_over&&van.bonnet[1]>van.top&&van.bonnet[2]>van.front-.4f);
    auto broken=car;broken.indices[0]=999;CHECK(!platform::camera_mount_fit({broken}).valid);
    broken=car;broken.indices.push_back(0);CHECK(!platform::camera_mount_fit({broken}).valid);
    broken=car;broken.vertices[0].position[0]=20.f;CHECK(!platform::camera_mount_fit({broken}).valid);
    CHECK(!platform::camera_mount_fit(std::vector<assets::PreviewMesh>{}).valid);
    if(argc==2){
        CHECK(platform::exe_image_autoload());
        platform::RetailAssetStore store;std::string error;CHECK(platform::retail_asset_store_open(store,argv[1],&error));
        unsigned profiled=0;
        for(unsigned model=0;model<30;++model){
            std::vector<std::uint8_t> bytes;
            CHECK(platform::retail_asset_read_relative_inflated(store,platform::VehicleResourcePaths[model],bytes,128u<<20,&error));
            const driving::Bytes layout(const_cast<std::uint8_t*>(platform::EmbeddedVehicleLayouts)+model*0x128,0x128);
            const auto token=layout.u32(0); // 469600 / 46AE70 main exterior body; +7C is an accessory
            const auto pmt=assets::inspect_pc_pmt(assets::ByteView(bytes));
            std::printf("model %u main body %x objects %zu\n",model,token,pmt.objects.size());std::fflush(stdout);
            CHECK(token>>16==platform::VehicleResourceIds[model]);
            const auto f=platform::camera_mount_fit(pmt,token&0xffffu);
            std::printf("  valid=%d profile=%d width=%.3f height=%.3f front=%.3f hood=%.3f glass=%.3f bonnet=(%.3f,%.3f) bumper=(%.3f,%.3f)\n",
                int(f.valid),int(f.profile_found),double(f.width),double(f.top-f.bottom),double(f.front),double(f.hood),double(f.glass),
                double(f.bonnet[1]),double(f.bonnet[2]),double(f.bumper[1]),double(f.bumper[2]));std::fflush(stdout);
            CHECK(f.valid);profiled+=f.profile_found?1u:0u;
        }
        CHECK(profiled>=15);std::printf("profiled %u of 30 stock bodies; retail bytes read only\n",profiled);
    }
    std::printf("camera mount fit: %u checks\n",checks);
}
