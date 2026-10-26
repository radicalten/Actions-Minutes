// Renderer bridge for the 3DS target.
//
// On the PC build the renderer is RT64: it receives the game's display list tasks
// (ultramodern::renderer::RendererContext::send_dl), turns RDP commands into D3D12/Vulkan
// work and presents through a swap chain. On the 3DS none of that exists - PICA200 has no
// equivalent of RT64's RDP pipeline - so this file provides:
//
//   * r3ds::dl_walk():  a bounds-checked walker over the F3DEX2 command stream that tallies
//                       opcodes. This is the seed of a PICA200 RDP interpreter: everything
//                       that converts RDP commands into citro3d draws plugs in here.
//   * PicaRenderer:     a RendererContext implementation that satisfies the runtime's
//                       interface, scans out the VI framebuffer on update_screen() and
//                       reports honest statistics instead of pretending to render.
//
// R3DS_LINK_RUNTIME is only defined when the recompiled game is linked in (GAME=1), which
// is the only configuration in which ultramodern's headers are available.
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstddef>

#include "r3ds.hpp"

#if defined(R3DS_LINK_RUNTIME)
#include <memory>
#include "ultramodern/renderer_context.hpp"
#include "ultramodern/ultra64.h"
#endif

namespace r3ds {

DlStats dl_walk(const uint8_t* dl, uint32_t size) {
    DlStats st{};
    if (!dl || size < 8) return st;

    const uint32_t max_cmds = size / 8u;
    for (uint32_t i = 0; i < max_cmds; i++) {
        const uint8_t* p = dl + (size_t)i * 8u;
        const uint8_t opcode = p[0];
        st.opcode_hist[opcode]++;
        st.commands++;
        st.bytes += 8;
        // A well-formed F3DEX2 list ends with G_ENDDL; stop there so the tally matches the
        // amount of work the renderer would actually be asked to do.
        if (opcode == 0xDF) { st.enddl++; break; }
    }
    return st;
}

// The runtime addresses RDRAM with sign-extended 64-bit kseg addresses and expects the host
// to reserve 4 GB of virtual address space so `addr - 0xFFFFFFFF80000000` is a valid index
// (librecomp: recomp::allocation_size). The 3DS has neither that address space nor that much
// memory, so the 3DS port resolves guest addresses to a physical offset instead: everything
// in kseg0/kseg1 is a 29-bit physical offset from RDRAM.
size_t n64_to_offset(uint64_t addr) {
    return (size_t)(addr & 0x1FFFFFFFULL);
}

#if defined(R3DS_LINK_RUNTIME)
namespace {

class PicaRenderer final : public ultramodern::renderer::RendererContext {
public:
    PicaRenderer(uint8_t* rdram, uint16_t* vi_surface)
        : rdram_(rdram), vi_surface_(vi_surface) {
        setup_result = ultramodern::renderer::SetupResult::Success;
        chosen_api = ultramodern::renderer::GraphicsApi::Auto;
    }

    bool valid() override { return true; }

    bool update_config(const ultramodern::renderer::GraphicsConfig&, const ultramodern::renderer::GraphicsConfig&) override {
        return true;
    }

    void enable_instant_present() override {}

    void send_dl(const OSTask* task) override {
        if (!task || !rdram_) return;
        const uint8_t* dl = rdram_ + n64_to_offset((uint64_t)task->t.data_ptr);
        last_dl_ = dl_walk(dl, (uint32_t)task->t.data_size);
        frames_submitted_++;
    }

    void send_dummy_workload(uint32_t) override {}

    void update_screen() override {
        // TODO(3ds/rdp): the display list above is not interpreted yet. What this does
        // today is the VI half of the job - take whatever the game wrote into its
        // framebuffer and put it on the 3DS screen.
        auto* regs = ultramodern::renderer::get_vi_regs();
        if (!regs || !rdram_ || !vi_surface_) return;
        vi_scanout_rdram(vi_surface_, rdram_, regs->VI_ORIGIN_REG & 0x00FFFFFFu,
                         regs->VI_WIDTH_REG, 240, regs->VI_STATUS_REG);
        frames_presented_++;
    }

    void shutdown() override {}

    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1.0f; }

    const DlStats& last_dl() const { return last_dl_; }
    uint64_t frames_presented() const { return frames_presented_; }

private:
    uint8_t* rdram_ = nullptr;
    uint16_t* vi_surface_ = nullptr;
    DlStats last_dl_{};
    uint64_t frames_submitted_ = 0;
    uint64_t frames_presented_ = 0;
};

} // namespace

std::unique_ptr<ultramodern::renderer::RendererContext> create_renderer_context(
    uint8_t* rdram, ultramodern::renderer::WindowHandle, bool /*developer_mode*/) {
    static uint16_t vi_surface[kViW * kViH];
    return std::make_unique<PicaRenderer>(rdram, vi_surface);
}
#endif // R3DS_LINK_RUNTIME

} // namespace r3ds
