#include "tjsCommHead.h"

#include "EventIntf.h"
#include "RenderManager.h"
#include "SysInitIntf.h"
#include "DrawDevice.h"
#include "WindowImpl.h"
#include "TVPWindow.h"
#include "krkrvita/frame_update_gate.hpp"
#include "krkrvita/retail_bootstrap.hpp"
#include "krkrvita/vitagl_presenter.hpp"
#include "krkrvita/vita_video_frame.hpp"
#include "krkrvita/yuri_window_update_policy.hpp"
#include "yuri_window_layer.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#ifdef KRKRVITA_YURI_OPENGL_COMPOSITOR
extern "C" bool krkrvita_yuri_get_opengl_texture(
    iTVPTexture2D* texture, unsigned int* name, int* internal_width,
    int* internal_height, float* scale_width, float* scale_height);
#endif

namespace {

class VitaWindowLayer;
VitaWindowLayer* active_layer = nullptr;
std::vector<VitaWindowLayer*> window_layers;
krkrvita::FrameDamageRegion captured_frame_damage;
std::uint64_t captured_frame_damage_generation = 0;
bool captured_frame_damage_valid = false;
bool presentation_reference_reported = false;
void select_last_visible_window();

class VitaWindowLayer final : public iWindowLayer {
public:
    explicit VitaWindowLayer(tTJSNI_Window* window);
    ~VitaWindowLayer();

    tTJSNI_Window* window() const { return window_; }
    void RequestSurfacePresentation() {
        if (present_texture_)
            pending_damage_.mark_full(
                static_cast<int>(present_texture_->GetWidth()),
                static_cast<int>(present_texture_->GetHeight()));
        surface_updates_.request();
    }

    void SetPaintBoxSize(tjs_int width, tjs_int height) override {
        const bool size_changed = layer_width_ != std::max<tjs_int>(0, width) ||
                                  layer_height_ != std::max<tjs_int>(0, height);
        layer_width_ = std::max<tjs_int>(0, width);
        layer_height_ = std::max<tjs_int>(0, height);
        if (!cursor_initialized_ && layer_width_ > 0 && layer_height_ > 0) {
            cursor_x_ = layer_width_ / 2;
            cursor_y_ = layer_height_ / 2;
            cursor_initialized_ = true;
            krkrvita_vitagl_set_cursor(cursor_x_, cursor_y_, true);
            cursor_updates_.request();
        }
        if (window_width_ == 0) window_width_ = layer_width_;
        if (window_height_ == 0) window_height_ = layer_height_;
        if (window_ && layer_width_ > 0 && layer_height_ > 0) {
            if (iTVPDrawDevice* draw_device = window_->GetDrawDevice()) {
                const tTVPRect rect(0, 0, layer_width_, layer_height_);
                draw_device->SetWindowSize(layer_width_, layer_height_);
                draw_device->SetClipRectangle(rect);
                draw_device->SetDestRectangle(rect);
            }
        }
        if (size_changed) {
            pending_damage_.mark_full(layer_width_, layer_height_);
            surface_updates_.request();
        }
    }

    bool GetFormEnabled() override { return visible_; }
    void SetDefaultMouseCursor() override {}

    void GetCursorPos(tjs_int& x, tjs_int& y) override {
        x = cursor_x_;
        y = cursor_y_;
    }

    void GetPointerArea(tjs_int& width, tjs_int& height) const {
        // Mouse/touch events are consumed by the primary layer and the
        // presenter draws this paint box. The script-visible Window size can
        // legitimately differ and must not be used as pointer coordinates.
        width = layer_width_;
        height = layer_height_;
    }

    void SetCursorPos(tjs_int x, tjs_int y) override {
        const tjs_int next_x = clamp_x(x);
        const tjs_int next_y = clamp_y(y);
        if (!cursor_initialized_ || cursor_x_ != next_x || cursor_y_ != next_y)
            cursor_updates_.request();
        cursor_x_ = next_x;
        cursor_y_ = next_y;
        cursor_initialized_ = true;
        krkrvita_vitagl_set_cursor(cursor_x_, cursor_y_, true);
    }

    void SetHintText(const ttstr&) override {}
    void SetAttentionPoint(tjs_int, tjs_int, const tTVPFont*) override {}

    void ZoomRectangle(tjs_int& left, tjs_int& top, tjs_int& right,
                       tjs_int& bottom) override {
        left = scale_coordinate(left);
        top = scale_coordinate(top);
        right = scale_coordinate(right);
        bottom = scale_coordinate(bottom);
    }

    void BringToFront() override {
        if (active_layer != this) surface_updates_.request();
        active_layer = this;
    }

    void ShowWindowAsModal() override {
        visible_ = true;
        BringToFront();
        // Yuri's event/application loop remains the owner of modal pumping.
        // The Vita adapter deliberately does not introduce a second frontend
        // loop here.
    }

    bool GetVisible() override { return visible_; }

    void SetVisible(bool visible) override {
        const bool changed = visible_ != visible;
        visible_ = visible;
        if (visible) {
            BringToFront();
            if (changed) surface_updates_.request();
        } else if (active_layer == this) {
            select_last_visible_window();
        }
    }

    const char* GetCaption() override { return caption_.c_str(); }
    void SetCaption(const std::string& caption) override { caption_ = caption; }

    void SetWidth(tjs_int width) override {
        window_width_ = std::max<tjs_int>(0, width);
    }

    void SetHeight(tjs_int height) override {
        window_height_ = std::max<tjs_int>(0, height);
    }

    void SetSize(tjs_int width, tjs_int height) override {
        SetWidth(width);
        SetHeight(height);
    }

    void GetSize(tjs_int& width, tjs_int& height) override {
        width = window_width_;
        height = window_height_;
    }

    tjs_int GetWidth() const override { return window_width_; }
    tjs_int GetHeight() const override { return window_height_; }

    void GetWinSize(tjs_int& width, tjs_int& height) override {
        GetSize(width, height);
    }

    void SetZoom(tjs_int numerator, tjs_int denominator) override {
        if (numerator <= 0 || denominator <= 0) return;
        ZoomNumer = numerator;
        ZoomDenom = denominator;
    }

    void UpdateDrawBuffer(iTVPTexture2D* texture) override {
        if (!visible_ || active_layer != this || !texture) return;
        if (texture->GetFormat() != TVPTextureFormat::RGBA) return;
        const int width = static_cast<int>(texture->GetWidth());
        const int height = static_cast<int>(texture->GetHeight());
        if (present_texture_ != texture) {
            texture->AddPresentationRef();
            if (!presentation_reference_reported) {
                krkrvita_boot_trace("yuri-presentation-reference-ready");
                presentation_reference_reported = true;
            }
            if (present_texture_)
                present_texture_->ReleasePresentationRef();
            present_texture_ = texture;
            pending_damage_.mark_full(width, height);
        } else if (pending_damage_.surface_width() != width ||
                   pending_damage_.surface_height() != height) {
            pending_damage_.mark_full(width, height);
        }
        if (captured_frame_damage_valid &&
            captured_frame_damage_generation != last_damage_generation_ &&
            captured_frame_damage.surface_width() == width &&
            captured_frame_damage.surface_height() == height) {
            pending_damage_.merge(captured_frame_damage);
            last_damage_generation_ = captured_frame_damage_generation;
        } else if (!captured_frame_damage_valid ||
                   captured_frame_damage_generation != last_damage_generation_) {
            // UpdateDrawBuffer is also used outside LayerManager completion.
            // Unknown damage must preserve correctness, so only that uncommon
            // path falls back to a complete upload.
            pending_damage_.mark_full(width, height);
            last_damage_generation_ = captured_frame_damage_generation;
        }
        // BasicDrawDevice commonly reuses one software texture. The call to
        // UpdateDrawBuffer, not pointer identity, is Yuri's update signal.
        surface_updates_.request();
    }

    bool PresentFrame() {
        if (!visible_ || active_layer != this || !present_texture_)
            return false;
        const krkrvita::FrameUpdateGate::Ticket surface_ticket =
            surface_updates_.begin();
        const krkrvita::FrameUpdateGate::Ticket cursor_ticket =
            cursor_updates_.begin();
        if (surface_ticket == 0 && cursor_ticket == 0) {
            // Traditional overlay/mixer movies are outside Yuri's software
            // layer tree. They still need a presentation cadence while the
            // completed software framebuffer itself remains unchanged.
            if (krkrvita_vitagl_video_active())
                return krkrvita_vitagl_redraw();
            static bool duplicate_skip_reported = false;
            if (!duplicate_skip_reported &&
                krkrvita_vitagl_presented_frames() != 0) {
                krkrvita_boot_trace("vitagl-duplicate-frame-upload-skipped");
                duplicate_skip_reported = true;
            }
            return false;
        }

        // A cursor-only update can reuse the last VitaGL texture. A Yuri
        // surface generation must be uploaded even when the texture object is
        // unchanged, because the software pixels live behind that object.
        if (surface_ticket == 0) {
            const bool presented = krkrvita_vitagl_redraw();
            cursor_updates_.complete(cursor_ticket, presented);
            return presented;
        }
        const int width = static_cast<int>(present_texture_->GetWidth());
        const int height = static_cast<int>(present_texture_->GetHeight());
#ifdef KRKRVITA_YURI_OPENGL_COMPOSITOR
        unsigned int texture_name = 0;
        int internal_width = 0;
        int internal_height = 0;
        float scale_width = 1.0f;
        float scale_height = 1.0f;
        if (krkrvita_yuri_get_opengl_texture(
                present_texture_, &texture_name, &internal_width,
                &internal_height, &scale_width, &scale_height)) {
            const bool presented = krkrvita_vitagl_present_texture(
                texture_name, width, height, internal_width, internal_height,
                scale_width, scale_height);
            surface_updates_.complete(surface_ticket, presented);
            cursor_updates_.complete(cursor_ticket, presented);
            return presented;
        }
#endif
        const int pitch = static_cast<int>(present_texture_->GetPitch());
        const void* pixels = present_texture_->GetScanLineForRead(0);
        static bool software_framebuffer_reported = false;
        if (pixels && !software_framebuffer_reported) {
            krkrvita_boot_trace("yuri-software-framebuffer-ready");
            software_framebuffer_reported = true;
        }
        if (pending_damage_.empty())
            pending_damage_.mark_full(width, height);
        const bool presented =
            pixels && width > 0 && height > 0 && pitch >= width * 4 &&
            krkrvita_vitagl_present_damage(
                pixels, pitch, width, height, pending_damage_);
        if (presented) pending_damage_.configure(width, height);
        surface_updates_.complete(surface_ticket, presented);
        cursor_updates_.complete(cursor_ticket, presented);
        return presented;
    }

    void InvalidateClose() override {
        window_ = nullptr;
        delete this;
    }

    bool GetWindowActive() override { return active_layer == this; }

    void Close() override {
        if (!window_ || closing_) return;
        closing_ = true;
        TVPPostInputEvent(new tTVPOnCloseInputEvent(window_));
    }

    void OnCloseQueryCalled(bool can_close) override {
        closing_ = false;
        if (!can_close) return;
        SetVisible(false);
        if (window_ && window_->IsMainWindow()) {
            iTJSDispatch2* owner = window_->GetOwnerNoAddRef();
            if (owner) owner->Invalidate(0, nullptr, nullptr, owner);
        }
    }

    void InternalKeyDown(tjs_uint16 key, tjs_uint32 shift) override {
        if (window_)
            TVPPostInputEvent(new tTVPOnKeyDownInputEvent(window_, key, shift));
    }

    void OnKeyUp(tjs_uint16 key, int shift) override {
        if (window_)
            TVPPostInputEvent(new tTVPOnKeyUpInputEvent(window_, key, shift));
    }

    void OnKeyPress(tjs_uint16 key, int, bool, bool) override {
        if (window_ && key)
            TVPPostInputEvent(new tTVPOnKeyPressInputEvent(window_, key));
    }

    tTVPImeMode GetDefaultImeMode() const override { return imDisable; }
    void SetImeMode(tTVPImeMode) override {}
    void ResetImeMode() override {}

    void UpdateWindow(tTVPUpdateType type) override {
        if (!window_) return;
        const bool full_exposure = krkrvita::yuri_needs_full_window_exposure(
            type == utEntire, present_texture_ != nullptr);
        if (!full_exposure) {
            static bool dirty_region_update_reported = false;
            if (!dirty_region_update_reported) {
                krkrvita_boot_trace("yuri-normal-update-preserved-dirty-region");
                dirty_region_update_reported = true;
            }
        }
        if (full_exposure) {
            const tTVPRect rect(0, 0, layer_width_, layer_height_);
            window_->NotifyWindowExposureToLayer(rect);
        }
        TVPDeliverWindowUpdateEvents();
    }

    void SetVisibleFromScript(bool visible) override { SetVisible(visible); }
    void SetUseMouseKey(bool enabled) override { use_mouse_key_ = enabled; }
    bool GetUseMouseKey() const override { return use_mouse_key_; }
    void ResetMouseVelocity() override {}
    void ResetTouchVelocity(tjs_int) override {}

    bool GetMouseVelocity(float& x, float& y, float& speed) const override {
        x = 0.0f;
        y = 0.0f;
        speed = 0.0f;
        return false;
    }

    void TickBeat() override {}
    cocos2d::Node* GetPrimaryArea() override { return nullptr; }

    void pointer_move(tjs_int x, tjs_int y) {
        const tjs_int next_x = clamp_x(x);
        const tjs_int next_y = clamp_y(y);
        if (!cursor_initialized_ || cursor_x_ != next_x || cursor_y_ != next_y)
            cursor_updates_.request();
        cursor_x_ = next_x;
        cursor_y_ = next_y;
        cursor_initialized_ = true;
        krkrvita_vitagl_set_cursor(cursor_x_, cursor_y_, true);
        if (window_) {
            TVPPostInputEvent(
                new tTVPOnMouseMoveInputEvent(window_, cursor_x_, cursor_y_, 0),
                TVP_EPT_DISCARDABLE);
        }
    }

    void pointer_wheel(tjs_int delta, tjs_int x, tjs_int y) {
        pointer_move(x, y);
        if (window_)
            TVPPostInputEvent(new tTVPOnMouseWheelInputEvent(
                window_, 0, delta, cursor_x_, cursor_y_));
    }

    void pointer_button(bool down, tTVPMouseButton button, tjs_int x,
                        tjs_int y) {
        pointer_move(x, y);
        if (!window_) return;
        if (down) {
            TVPPostInputEvent(new tTVPOnMouseDownInputEvent(
                window_, cursor_x_, cursor_y_, button, 0));
        } else {
            // Kirikiri's generic onClick is the primary/left-button action.
            // Emitting it for Cross's mapped right click would both advance
            // text and open the right-click action on the same release.
            if (button == mbLeft)
                TVPPostInputEvent(
                    new tTVPOnClickInputEvent(window_, cursor_x_, cursor_y_));
            TVPPostInputEvent(new tTVPOnMouseUpInputEvent(
                window_, cursor_x_, cursor_y_, button, 0));
        }
    }

private:
    tjs_int clamp_x(tjs_int x) const {
        return std::clamp<tjs_int>(x, 0, std::max<tjs_int>(0, layer_width_ - 1));
    }

    tjs_int clamp_y(tjs_int y) const {
        return std::clamp<tjs_int>(y, 0,
                                   std::max<tjs_int>(0, layer_height_ - 1));
    }

    tjs_int scale_coordinate(tjs_int value) const {
        return static_cast<tjs_int>((static_cast<tjs_int64>(value) * ZoomNumer) /
                                    ZoomDenom);
    }

    tTJSNI_Window* window_ = nullptr;
    tjs_int layer_width_ = 0;
    tjs_int layer_height_ = 0;
    tjs_int window_width_ = 0;
    tjs_int window_height_ = 0;
    tjs_int cursor_x_ = 0;
    tjs_int cursor_y_ = 0;
    std::string caption_;
    bool visible_ = false;
    bool closing_ = false;
    bool use_mouse_key_ = false;
    bool cursor_initialized_ = false;
    iTVPTexture2D* present_texture_ = nullptr;
    krkrvita::FrameDamageRegion pending_damage_;
    std::uint64_t last_damage_generation_ = 0;
    krkrvita::FrameUpdateGate surface_updates_;
    krkrvita::FrameUpdateGate cursor_updates_;
};

VitaWindowLayer::VitaWindowLayer(tTJSNI_Window* window) : window_(window) {
    window_layers.push_back(this);
    if (window_layers.size() == 1)
        krkrvita_boot_trace("yuri-first-window-created");
}

VitaWindowLayer::~VitaWindowLayer() {
    if (present_texture_) present_texture_->ReleasePresentationRef();
    window_layers.erase(
        std::remove(window_layers.begin(), window_layers.end(), this),
        window_layers.end());
    if (active_layer == this) select_last_visible_window();
}

void select_last_visible_window() {
    active_layer = nullptr;
    for (auto it = window_layers.rbegin(); it != window_layers.rend(); ++it) {
        if ((*it)->GetVisible()) {
            active_layer = *it;
            active_layer->RequestSurfacePresentation();
            break;
        }
    }
}

} // namespace

extern "C" void krkrvita_yuri_begin_frame_damage(int width, int height) {
    captured_frame_damage.configure(width, height);
    ++captured_frame_damage_generation;
    if (captured_frame_damage_generation == 0)
        ++captured_frame_damage_generation;
    captured_frame_damage_valid = width > 0 && height > 0;
}

extern "C" void krkrvita_yuri_add_frame_damage(
    int left, int top, int right, int bottom) {
    if (!captured_frame_damage_valid) return;
    captured_frame_damage.add(left, top, right, bottom);
}

iWindowLayer* TVPCreateAndAddWindow(tTJSNI_Window* window) {
    auto* layer = new VitaWindowLayer(window);
    active_layer = layer;
    return layer;
}

void TVPRemoveWindowLayer(iWindowLayer* layer) {
    if (!layer) return;
    delete static_cast<VitaWindowLayer*>(layer);
}

tTJSNI_Window* TVPGetActiveWindow() {
    return active_layer ? active_layer->window() : nullptr;
}

void krkrvita_yuri_pointer_move(std::int32_t x, std::int32_t y) {
    if (active_layer) active_layer->pointer_move(x, y);
}

void krkrvita_yuri_pointer_button(bool down, std::int32_t button,
                                  std::int32_t x, std::int32_t y) {
    if (!active_layer) return;
    const auto mouse_button = button == 1 ? mbRight : mbLeft;
    active_layer->pointer_button(down, mouse_button, x, y);
}

void krkrvita_yuri_pointer_wheel(std::int32_t delta, std::int32_t x,
                                 std::int32_t y) {
    if (active_layer) active_layer->pointer_wheel(delta, x, y);
}

void krkrvita_yuri_key(bool down, std::uint16_t key, std::uint32_t shift) {
    if (!active_layer) return;
    if (down)
        active_layer->InternalKeyDown(key, shift);
    else
        active_layer->OnKeyUp(key, static_cast<int>(shift));
}

bool krkrvita_yuri_active_pointer(std::int32_t& width, std::int32_t& height,
                                  std::int32_t& x, std::int32_t& y) {
    if (!active_layer) return false;
    tjs_int logical_width = 0;
    tjs_int logical_height = 0;
    tjs_int window_width = 0;
    tjs_int window_height = 0;
    tjs_int cursor_x = 0;
    tjs_int cursor_y = 0;
    active_layer->GetPointerArea(logical_width, logical_height);
    active_layer->GetSize(window_width, window_height);
    active_layer->GetCursorPos(cursor_x, cursor_y);
    if (logical_width <= 0 || logical_height <= 0) return false;
    static bool mismatch_reported = false;
    if (!mismatch_reported &&
        (window_width != logical_width || window_height != logical_height)) {
        krkrvita_boot_trace("yuri-pointer-window-layer-mismatch-corrected");
        mismatch_reported = true;
    }
    width = logical_width;
    height = logical_height;
    x = cursor_x;
    y = cursor_y;
    return true;
}

bool krkrvita_yuri_present_frame() {
    return active_layer && active_layer->PresentFrame();
}
