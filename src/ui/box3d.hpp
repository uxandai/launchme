#pragma once
// A game case in real 3D (Półka and Cover Flow's held case, game_case_view.hpp): a lit, glossy keep
// case with rounded corners and softly bevelled edges, its front, back and spine printed, turned in
// the hand. Drawn by Direct3D 11 on the renderer's own device (swap_chain.hpp) into a multisampled
// texture that Direct2D then draws like any bitmap: the faces are Direct2D bitmaps on the same
// device (the shelf's own package drawing), so nothing is copied through the CPU.
// Windows only, feature level 10 and up; elsewhere (and on Linux) the caller draws the flat 2D case.
#ifdef _WIN32
#include <d2d1_1.h>
#include <d3d11_1.h>
#include <wrl/client.h>

#include <cstdint>

class Box3D {
public:
    // The case, in pixels of the frame: its front at rest is `width` x `height` around the frame's
    // middle; `depth` its thickness. Turned by `yaw` (round its upright axis; positive brings the
    // spine, its left side, towards the viewer) and `pitch` (positive tips its top away).
    struct Pose {
        float width = 0, height = 0, depth = 0;
        float corner = 0;     // The front's corners' radius.
        float bevel = 0;      // The soft edge round the front and the back.
        float yaw = 0, pitch = 0;
        float eye = 0;        // The camera's distance from the case's middle (the shelf's: 2.7 heights).
        float gloss = 1;      // The clear sleeve's shine (0: cardboard).
        float plastic[3]{0.08f, 0.08f, 0.09f}; // The case's edges.
        float light = 0;      // A slow drift of the lamp (radians), so a case at rest still catches it.
    };
    struct Faces {
        ID2D1Bitmap1* front = nullptr;
        ID2D1Bitmap1* back = nullptr;
        ID2D1Bitmap1* spine = nullptr; // Its print reads top to bottom.
    };
    Box3D() = default;
    ~Box3D() { reset(); }
    Box3D(const Box3D&) = delete;
    Box3D& operator=(const Box3D&) = delete;

    // Whether `device` can draw it (feature level 10_0 and up, shaders compiled); asked once a device.
    bool supported(ID3D11Device* device);
    // Draws the case into a frame of frame_width x frame_height pixels (transparent round it,
    // premultiplied) and returns it as a bitmap of `context` (the same device), else null. The faces'
    // Direct2D drawing is flushed first. The last frame is returned again while nothing changed.
    ID2D1Bitmap1* render(ID3D11Device* device, ID2D1DeviceContext* context, const Faces& faces, const Pose& pose,
                         uint32_t frame_width, uint32_t frame_height, uint64_t faces_version);
    // Device lost or the renderer's target rebuilt: everything goes.
    void reset();

private:
    bool create(ID3D11Device* device);
    bool frame(uint32_t width, uint32_t height, ID2D1DeviceContext* context);
    bool mesh(const Pose& pose);
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view(ID2D1Bitmap1* bitmap, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& cached,
                                                          ID2D1Bitmap1*& cached_bitmap);

    ID3D11Device* device_ = nullptr; // Not owned: the renderer's (its lifetime bounds ours, reset() on loss).
    bool failed_ = false;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> immediate_;
    Microsoft::WRL::ComPtr<ID3DDeviceContextState> state_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_, vertices_, indices_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_state_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> target_, depth_, resolved_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_view_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth_view_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> front_view_, back_view_, spine_view_;
    ID2D1Bitmap1* front_bitmap_ = nullptr;
    ID2D1Bitmap1* back_bitmap_ = nullptr;
    ID2D1Bitmap1* spine_bitmap_ = nullptr;
    uint32_t width_ = 0, height_ = 0, samples_ = 1;
    // The mesh's parts, in the index buffer: front (cap and bevel), back, the spine's strip, the plain plastic.
    struct Part { uint32_t first = 0, count = 0; };
    Part front_, back_, spine_, plastic_;
    float mesh_key_[5]{};
    Pose drawn_{};
    uint64_t drawn_faces_ = 0;
    bool drawn_valid_ = false;
};
#endif
