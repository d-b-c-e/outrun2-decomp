#pragma once
// Port of Art CameraMountFit / CameraMountMemory (fc23361c), using the actual
// exterior PMT body triangles. Coordinates here: x right, y up, z forward,
// metres. OutRun's native front is -z; conversion happens at the PMT boundary.
#include "pc_pmt.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
namespace outrun::platform {
struct CameraMountFit {
    bool valid{},profile_found{},cab_over{};
    float bottom{},top{},front{},back{},width{},hood{},glass{};
    std::array<float,3> bonnet{},bumper{};
};
inline CameraMountFit camera_mount_fit(const std::vector<assets::PreviewMesh>& meshes){
    CameraMountFit fit;
    float left=std::numeric_limits<float>::infinity(),right=-left;
    fit.bottom=fit.back=left;fit.top=fit.front=right;
    for(const auto& mesh:meshes){if(mesh.indices.size()%3)return {};for(const auto index:mesh.indices){
        if(index>=mesh.vertices.size())return {};
        const auto& p=mesh.vertices[index].position;
        for(float v:p)if(!std::isfinite(v))return {};
        left=std::min(left,p[0]);right=std::max(right,p[0]);
        fit.bottom=std::min(fit.bottom,p[1]);fit.top=std::max(fit.top,p[1]);
        fit.front=std::max(fit.front,-p[2]);fit.back=std::min(fit.back,-p[2]);
    }}
    fit.width=right-left;const float height=fit.top-fit.bottom,length=fit.front-fit.back;
    if(!(fit.width>=1.2f&&fit.width<=3.2f&&height>=0.8f&&height<=4.5f&&length>0.5f&&length<12.f))return {};
    constexpr unsigned samples=40;
    std::array<float,samples> profile;profile.fill(std::numeric_limits<float>::quiet_NaN());
    // Intersect triangles, not just vertices: flat low-poly roofs must not become holes.
    for(const auto& mesh:meshes)for(std::size_t t=0;t+2<mesh.indices.size();t+=3)
        for(float x0:{-fit.width/8.f,0.f,fit.width/8.f}){
            float cut[2][2]{};unsigned found=0;
            for(unsigned edge=0;edge<3&&found<2;++edge){
                const auto& a=mesh.vertices[mesh.indices[t+edge]].position;
                const auto& b=mesh.vertices[mesh.indices[t+(edge+1)%3]].position;
                const float da=a[0]-x0,db=b[0]-x0;if(da*db>0.f||da==db)continue;
                const float f=da/(da-db);cut[found][0]=a[1]+(b[1]-a[1])*f;
                cut[found][1]=-(a[2]+(b[2]-a[2])*f);++found;
            }
            if(found<2)continue;
            const float ya=cut[0][0],za=cut[0][1],yb=cut[1][0],zb=cut[1][1];
            const int first=int(std::ceil((fit.front-std::max(za,zb))/length*(samples-1)-1e-4f));
            const int last=int(std::floor((fit.front-std::min(za,zb))/length*(samples-1)+1e-4f));
            for(int i=std::max(0,first);i<=std::min(int(samples)-1,last);++i){
                const float z=fit.front-length*float(i)/float(samples-1);
                const float y=std::fabs(zb-za)<1e-6f?std::max(ya,yb):ya+(yb-ya)*(z-za)/(zb-za);
                if(std::isnan(profile[unsigned(i)])||y>profile[unsigned(i)])profile[unsigned(i)]=y;
            }
        }
    std::vector<unsigned> index;
    for(unsigned i=0;i<samples;++i)if(std::isfinite(profile[i]))index.push_back(i);
    fit.hood=fit.bottom+0.58f*height;fit.glass=fit.front-1.2f; // Art's explicit box-only fallback
    if(index.size()>=8){
        const auto z=[&](unsigned i){return fit.front-length*float(i)/float(samples-1);};
        const auto slope=[&](std::size_t k){return (profile[index[k+1]]-profile[index[k]])/(z(index[k])-z(index[k+1]));};
        std::size_t k=0;while(k+1<index.size()&&slope(k)>=0.35f)++k;
        if(profile[index[k]]-profile[index[0]]>0.9f){
            fit.cab_over=fit.profile_found=true;fit.hood=profile[index[k]];fit.glass=z(index[k])-0.10f;
        }else{
            while(k+1<index.size()&&slope(k)<0.35f)++k;
            float after=-std::numeric_limits<float>::infinity();
            for(std::size_t i=k+1;i<index.size();++i)after=std::max(after,profile[index[i]]);
            if(k+1<index.size()&&z(index[k])>=fit.front-0.6f*length&&after>=profile[index[k]]+0.12f){
                fit.profile_found=true;fit.hood=profile[index[k]];fit.glass=z(index[k]);
            }
        }
    }
    fit.bonnet={0.f,fit.hood+0.08f,fit.glass+0.10f};
    fit.bumper={0.f,fit.bottom+std::min((fit.hood-fit.bottom)/2.f,0.45f),fit.front+0.05f};
    fit.valid=true;return fit;
}
inline CameraMountFit camera_mount_fit(const assets::PmtView& pmt,unsigned body_object){
    return camera_mount_fit(assets::decode_static_object(pmt,body_object));
}
}
