#pragma once
// D3D9 point sprites (POINTLIST with D3DRS_POINTSPRITEENABLE), as "Point Sprites (Direct3D 9)"
// (learn.microsoft.com/windows/win32/direct3d9/point-sprites) and "Rasterization Rules (Direct3D 9)" define them.
// Shared by the soft device and the D3D11 device (which draws the soft device's triangles).
// - Size: with D3DRS_POINTSCALEENABLE, Ss = Vh * Si * sqrt(1 / (A + B*De + C*De^2)), De the eye-space distance
//   sqrt(Xe^2 + Ye^2 + Ze^2), Si = D3DRS_POINTSIZE (the game's FVFs carry no per-vertex size), Vh the viewport height.
//   Without it, Ss = Si pixels. Then S = Pmax if Ss > Pmax (Pmax = min(D3DCAPS9::MaxPointSize, D3DRS_POINTSIZE_MAX)),
//   then S = Pmin if S < Pmin (D3DRS_POINTSIZE_MIN), in that order.
// - Shape: a screen-aligned square of side S centred on the point, rasterized as two triangles under the triangle rules,
//   the vertex colours duplicated; texture coordinates (0,0) top-left, (1,0) top-right, (0,1) bottom-left, (1,1)
//   bottom-right on screen (wined3d matches D3D with GL's GL_UPPER_LEFT point-sprite origin).
// - Clipping: not drawn when its depth is outside the viewport's MinZ..MaxZ (clip z outside [0, w], w > 0), nor when
//   the square, its size included, is entirely outside the viewport; a partly visible square is drawn, cut at the
//   viewport's edges.
#include <cstdint>
namespace outrun::platform {
struct PcPointSpriteParams {
    float size{1.f},min{1.f},max{64.f};   // D3DRS_POINTSIZE, _MIN, _MAX
    float a{1.f},b{0.f},c{0.f};           // D3DRS_POINTSCALE_A, _B, _C
    bool scale{};                         // D3DRS_POINTSCALEENABLE
    float max_point_size{8192.f};         // D3DCAPS9::MaxPointSize; the game's _MAX (64) binds first
};
// The screen-space side in pixels. False when it is not a finite, non-negative number: a non-finite eye position,
// A + B*De + C*De^2 negative or NaN, or an undefined 0 * infinity.
bool pc_point_sprite_size(const PcPointSpriteParams&,const float eye[3],float viewport_h,float& size);
// The square's clip-space corners (top-left, top-right, bottom-left, bottom-right) for a point at clip (x, y, z, w)
// with side `size` pixels in a viewport_w x viewport_h viewport. False when D3D9 draws nothing (see above) or an input
// is not finite.
bool pc_point_sprite_corners(const float clip[4],float size,float viewport_w,float viewport_h,float corners[4][4]);
// The texture coordinate of each corner, in the same order.
inline constexpr float PcPointSpriteUv[4][2]{{0.f,0.f},{1.f,0.f},{0.f,1.f},{1.f,1.f}};
}
