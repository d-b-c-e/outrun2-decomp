// The curve correction's mesh from the runtime's grids (curve_runtime.hpp). No game, no device: the runtime's grid
// math itself is tested in the Wheelkit repository (runtime/native/triples/tests/test_warp.cpp).
#include "curve_runtime.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace {
int checks=0;
void check(bool v,const char* what){++checks;if(!v){std::fprintf(stderr,"curve runtime: FAIL: %s\n",what);std::exit(1);}}
constexpr unsigned C=outrun::desktop::CurveMesh::Columns,R=outrun::desktop::CurveMesh::Rows,N=(C+1)*(R+1);
// The flat identity, with the centre column pulled inward on each row by `bow` (what a curved screen's grid looks like).
std::vector<float> grid(float bow){
    std::vector<float> g(2*N);
    for(unsigned row=0;row<=R;++row)for(unsigned col=0;col<=C;++col){
        const unsigned k=row*(C+1)+col;const float u=float(col)/C,v=float(row)/R;
        g[2*k]=u;g[2*k+1]=0.5f+(v-0.5f)*(1.f-bow*(1.f-std::abs(2.f*u-1.f)));}
    return g;
}
}
int main(){
    using namespace outrun::desktop;
    static CurveMesh mesh;
    const auto flat=grid(0.f),bent=grid(0.06f);
    check(curve_mesh_from_grids({flat.data(),bent.data(),flat.data()},mesh),"three valid grids");
    // Corners: clip -1..1 with the top row at +1, the flat view's corners as given.
    const auto& c=mesh.panels[1];
    check(c[0].x==-1.f&&c[0].y==1.f&&c[0].u==0.f&&c[0].v==0.f,"top left");
    check(c[C].x==1.f&&c[C].y==1.f&&c[C].u==1.f,"top right");
    check(c[N-1].x==1.f&&c[N-1].y==-1.f&&c[N-1].v==1.f,"bottom right");
    // The middle of the top row of the bent screen samples below the view's top edge; the flat one does not.
    check(c[C/2].v>0.02f&&mesh.panels[0][C/2].v==0.f,"the curve reaches the mesh, per screen");
    // Indices: two triangles per cell, all inside the panel.
    bool inside=true;for(auto i:mesh.indices)inside=inside&&i<N;
    check(inside&&mesh.indices[0]==0&&mesh.indices[1]==1&&mesh.indices[2]==C+1,"indices");
    // The view the renderer gets: the shared shader, thirds of the back buffer, each view drawn whole.
    outrun::platform::PcCurveMeshView v;
    check(curve_mesh_view(mesh,7680,1440,v)&&outrun::platform::pc_curve_mesh_valid(v),"the renderer's view of it");
    check(v.inner[2][0]==0.f&&v.inner[2][2]==1.f&&v.sample_rects[2][2]>0.66f&&v.sample_rects[2][2]<0.67f,"inner whole, the right third");
    check(!curve_mesh_view(mesh,7681,1440,v),"a back buffer that is not three thirds is refused");
    // Refusals leave the mesh as it was.
    auto bad=flat;bad[10]=1.5f;
    check(!curve_mesh_from_grids({flat.data(),bad.data(),flat.data()},mesh)&&mesh.panels[1][C/2].v>0.02f,"outside the view: refused");
    bad=flat;bad[11]=std::nanf("");
    check(!curve_mesh_from_grids({bad.data(),flat.data(),flat.data()},mesh),"not finite: refused");
    check(!curve_mesh_from_grids({flat.data(),nullptr,flat.data()},mesh),"a missing grid: refused");
    std::printf("curve runtime: %d checks passed\n",checks);
    return 0;
}
