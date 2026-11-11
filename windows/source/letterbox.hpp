#pragma once
// UILetterboxing (pc_screen.hpp): black outside the centred 4:3 on the back buffer, before anything is drawn over it.
// Moved out of the old F6 panel's present overlay unchanged; the present overlay (wheelkit_runtime.cpp) calls it.
namespace outrun::platform { struct NativeRuntimeContext; }
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
namespace outrun::desktop {
// Game thread, every input sample: whether the race is on (78026C, and START's stage 8367A8) for the present overlay.
void letterbox_note(const platform::NativeRuntimeContext*);
// Present thread, every presented frame. ClearView takes no pipeline state, so the renderer's bindings are untouched.
void letterbox_draw(ID3D11DeviceContext*,ID3D11RenderTargetView*,unsigned width,unsigned height);
void letterbox_release();
}
