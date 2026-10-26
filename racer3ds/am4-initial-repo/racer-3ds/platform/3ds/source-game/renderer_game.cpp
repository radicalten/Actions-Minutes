// The renderer slot for the game build.
//
// N64ModernRuntime asks for exactly one thing here: an object implementing
// ultramodern::renderer::RendererContext. The runtime's event thread calls send_dl() for
// every graphics task and update_screen() at every VI, on its own schedule.
//
// This one is a **stub**: it accepts tasks, counts them, copies the VI source buffer into the
// app's 320x240 RGB565 output and keeps the frame counter. It does not interpret a single RDP
// command, so the game's logic, timing, threads and audio run while the picture is the
// bring-up view. Replacing this file with rt64-3ds's renderer (same interface, 116 files,
// F3DEX interpreter on citro3d) is the step that makes the game visible.
#include <3ds.h>
#include <cstring>
#include <atomic>

#include "ultramodern/ultramodern.hpp"
#include "ultramodern/renderer_context.hpp"
#include "r3ds.hpp"

namespace {

std::atomic<uint32_t> s_dl_count{0};
std::atomic<uint32_t> s_screen_count{0};
uint8_t* s_rdram = nullptr;
ultramodern::renderer::ViRegs s_regs{};

class StubRenderer final : public ultramodern::renderer::RendererContext {
public:
    StubRenderer(uint8_t* rdram, ultramodern::renderer::WindowHandle) {
        s_rdram = rdram;
        setup_result = ultramodern::renderer::SetupResult::Success;
        chosen_api = ultramodern::renderer::GraphicsApi::Auto;   // no real API behind this stub
    }

    bool valid() override { return true; }

    bool update_config(const ultramodern::renderer::GraphicsConfig&,
                       const ultramodern::renderer::GraphicsConfig&) override {
        return false;                      // nothing to reconfigure
    }

    void enable_instant_present() override {}

    void send_dl(const OSTask* task) override {
        (void)task;                        // counted, not drawn (yet)
        s_dl_count.fetch_add(1, std::memory_order_relaxed);
    }

    void send_dummy_workload(uint32_t fb_address) override { (void)fb_address; }

    void update_screen() override {
        s_regs = *ultramodern::renderer::get_vi_regs();
        s_screen_count.fetch_add(1, std::memory_order_relaxed);
        r3ds::game_count_frame();
    }

    void shutdown() override {}

    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1.0f; }
};

} // namespace

namespace ultramodern::renderer {

std::unique_ptr<RendererContext> create_render_context(uint8_t* rdram, WindowHandle handle,
                                                      bool developer_mode) {
    (void)developer_mode;
    s_rdram = rdram;
    return std::make_unique<StubRenderer>(rdram, handle);
}

} // namespace ultramodern::renderer
