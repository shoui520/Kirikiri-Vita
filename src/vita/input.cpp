#include "krkrvita/runtime.hpp"

#include <psp2/ctrl.h>
#include <psp2/touch.h>

#include <algorithm>

namespace krkrvita {

VitaInput::VitaInput() {
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    SceTouchPanelInfo panel{};
    if (sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &panel) >= 0) {
        touch_min_x_ = panel.minDispX;
        touch_min_y_ = panel.minDispY;
        touch_span_x_ = std::max(1, panel.maxDispX - panel.minDispX);
        touch_span_y_ = std::max(1, panel.maxDispY - panel.minDispY);
    }
}

VitaInput::~VitaInput() {
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_STOP);
}

InputSnapshot VitaInput::poll() {
    InputSnapshot snapshot;
    SceCtrlData controller{};
    if (sceCtrlPeekBufferPositive(0, &controller, 1) > 0) {
        snapshot.buttons = controller.buttons;
        snapshot.pressed = controller.buttons & ~previous_buttons_;
        snapshot.released = previous_buttons_ & ~controller.buttons;
        previous_buttons_ = controller.buttons;
        snapshot.analog_x = (static_cast<int>(controller.lx) - 128) / 127.0f;
        snapshot.analog_y = (static_cast<int>(controller.ly) - 128) / 127.0f;
        snapshot.analog_x = std::clamp(snapshot.analog_x, -1.0f, 1.0f);
        snapshot.analog_y = std::clamp(snapshot.analog_y, -1.0f, 1.0f);
    }
    SceTouchData touch{};
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) > 0 && touch.reportNum > 0) {
        snapshot.touch.down = true;
        snapshot.touch.x = (touch.report[0].x - touch_min_x_) / touch_span_x_ * 960.0f;
        snapshot.touch.y = (touch.report[0].y - touch_min_y_) / touch_span_y_ * 544.0f;
        snapshot.touch.x = std::clamp(snapshot.touch.x, 0.0f, 959.0f);
        snapshot.touch.y = std::clamp(snapshot.touch.y, 0.0f, 543.0f);
    }
    return snapshot;
}

} // namespace krkrvita

