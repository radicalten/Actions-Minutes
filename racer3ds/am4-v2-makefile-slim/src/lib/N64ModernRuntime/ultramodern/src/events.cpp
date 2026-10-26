#include <thread>
#include <atomic>
#include <deque>
#include <mutex>
#include <chrono>
#include <cinttypes>
#include <variant>
#include <unordered_map>
#include <utility>
#include <mutex>
#include <queue>
#include <cstring>

#include "blockingconcurrentqueue.h"

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "deterministic.hpp"
#include "ultramodern/extensions.h"

#include "ultramodern/rsp.hpp"
#include "ultramodern/renderer_context.hpp"

static ultramodern::events::callbacks_t events_callbacks{};

void ultramodern::events::set_callbacks(const ultramodern::events::callbacks_t& callbacks) {
    events_callbacks = callbacks;
}

struct SpTaskAction {
    OSTask task;
    // Deterministic mode: signalled once the renderer has parsed the display list, so the game
    // (already told the task is done) can't overwrite it mid-parse.
    moodycamel::LightweightSemaphore* parsed = nullptr;
    std::chrono::high_resolution_clock::time_point submitted;
};

struct ScreenUpdateAction {
    ultramodern::renderer::ViRegs regs;
};

struct UpdateConfigAction {
};

struct DummyWorkloadAction {
    int32_t fb_address;
};

using Action = std::variant<SpTaskAction, ScreenUpdateAction, UpdateConfigAction, DummyWorkloadAction>;

struct ViState {
    const OSViMode* mode = nullptr;
    PTR(void) framebuffer;
    PTR(OSMesg) mq;
    OSMesg msg;
    uint32_t state;
    uint32_t control;
    int retrace_count = 1;
};

#define VI_STATE_BLACK 0x20
#define VI_STATE_REPEATLINE 0x40

static struct {
    struct {
        ultramodern::HostThread thread;
        int cur_state;
        int field;
        ViState states[2];
        ultramodern::renderer::ViRegs regs;
        ultramodern::renderer::ViRegs update_screen_regs;

        ViState* get_next_state() {
            return &states[cur_state ^ 1];
        }
        ViState* get_cur_state() {
            return &states[cur_state];
        }
        void update_vi() {
            ViState* next_state = get_next_state();
            const OSViMode* next_mode = next_state->mode;
            if (next_mode == nullptr) {
                // No mode set yet: leave the registers alone, but still swap states so that
                // osViSetEvent registrations take effect. Retraces happen regardless of mode,
                // and a game may wait for one before it calls osViSetMode.
                cur_state ^= 1;
                *get_next_state() = *get_cur_state();
                return;
            }

            const OSViCommonRegs* common_regs = &next_mode->comRegs;
            const OSViFieldRegs* field_regs = &next_mode->fldRegs[field];
            PTR(void) framebuffer = osVirtualToPhysical(next_state->framebuffer);
            PTR(void) origin = framebuffer + field_regs->origin;

            // Process the VI state flags.
            uint32_t hStart = common_regs->hStart;
            if (next_state->state & VI_STATE_BLACK) {
                hStart = 0;
            }

            uint32_t yScale = field_regs->yScale;
            if (next_state->state & VI_STATE_REPEATLINE) {
                yScale = 0;
                origin = framebuffer;
            }

            // TODO implement osViFade

            // Update VI registers.
            regs.VI_ORIGIN_REG = origin;
            regs.VI_WIDTH_REG = common_regs->width;
            regs.VI_TIMING_REG = common_regs->burst;
            regs.VI_V_SYNC_REG = common_regs->vSync;
            regs.VI_H_SYNC_REG = common_regs->hSync;
            regs.VI_LEAP_REG = common_regs->leap;
            regs.VI_H_START_REG = hStart;
            regs.VI_V_START_REG = field_regs->vStart; // TODO implement osViExtendVStart
            regs.VI_V_BURST_REG = field_regs->vBurst;
            regs.VI_INTR_REG = field_regs->vIntr;
            regs.VI_X_SCALE_REG = common_regs->xScale; // TODO implement osViSetXScale
            regs.VI_Y_SCALE_REG = yScale; // TODO implement osViSetYScale
            regs.VI_STATUS_REG = next_state->control;
            
            // Swap VI states.
            cur_state ^= 1;
            *get_next_state() = *get_cur_state();
        }
    } vi;
    struct {
        ultramodern::HostThread gfx_thread;
        ultramodern::HostThread task_thread;
        PTR(OSMesgQueue) mq = NULLPTR;
        OSMesg msg = (OSMesg)0;
    } sp;
    struct {
        PTR(OSMesgQueue) mq = NULLPTR;
        OSMesg msg = (OSMesg)0;
    } dp;
    struct {
        PTR(OSMesgQueue) mq = NULLPTR;
        OSMesg msg = (OSMesg)0;
    } ai;
    struct {
        PTR(OSMesgQueue) mq = NULLPTR;
        OSMesg msg = (OSMesg)0;
    } si;
    // The same message queue may be used for multiple events, so share a mutex for all of them
    std::mutex message_mutex;
    uint8_t* rdram;
    moodycamel::BlockingConcurrentQueue<Action> action_queue{};
    moodycamel::BlockingConcurrentQueue<OSTask*> sp_task_queue{};
    moodycamel::ConcurrentQueue<OSThread*> deleted_threads{};
} events_context{};

ultramodern::renderer::ViRegs* ultramodern::renderer::get_vi_regs() {
    return &events_context.vi.update_screen_regs;
}

extern "C" void osSetEventMesg(RDRAM_ARG OSEvent event_id, PTR(OSMesgQueue) mq_, OSMesg msg) {
    std::lock_guard lock{ events_context.message_mutex };

    switch (event_id) {
        case OS_EVENT_SP:
            events_context.sp.msg = msg;
            events_context.sp.mq = mq_;
            break;
        case OS_EVENT_DP:
            events_context.dp.msg = msg;
            events_context.dp.mq = mq_;
            break;
        case OS_EVENT_AI:
            events_context.ai.msg = msg;
            events_context.ai.mq = mq_;
            break;
        case OS_EVENT_SI:
            events_context.si.msg = msg;
            events_context.si.mq = mq_;
    }
}

extern "C" void osViSetEvent(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, u32 retrace_count) {
    std::lock_guard lock{ events_context.message_mutex };
    ViState* next_state = events_context.vi.get_next_state();
    next_state->mq = mq_;
    next_state->msg = msg;
    next_state->retrace_count = retrace_count;
}

uint64_t total_vis = 0;


extern std::atomic_bool exited;
extern moodycamel::LightweightSemaphore graphics_shutdown_ready;

void set_dummy_vi(bool odd);

extern "C" volatile uint32_t ultramodern_dbg_counts[8];   // mesgqueue.cpp

void vi_thread_func() {
    ultramodern::set_native_thread_name("VI Thread");
    // This thread should be prioritized over every other thread in the application, as it's what allows
    // the game to generate new audio and gfx lists.
    ultramodern::set_native_thread_priority(ultramodern::ThreadPriority::Critical);
    using namespace std::chrono_literals;

    int remaining_retraces = 1;

    while (!exited) {
        ultramodern_dbg_counts[4] = ultramodern_dbg_counts[4] + 1;
        // Determine the next VI time (more accurate than adding 16ms each VI interrupt)
        auto next = ultramodern::get_start() + (total_vis * 1000000us) / (60 * ultramodern::get_speed_multiplier());
        //if (next > std::chrono::high_resolution_clock::now()) {
        //    printf("Sleeping for %" PRIu64 " us to get from %" PRIu64 " us to %" PRIu64 " us \n",
        //        (next - std::chrono::high_resolution_clock::now()) / 1us,
        //        (std::chrono::high_resolution_clock::now() - events_context.start) / 1us,
        //        (next - events_context.start) / 1us);
        //} else {
        //    printf("No need to sleep\n");
        //}
        // Detect if there's more than a second to wait and wait a fixed amount instead for the next VI if so, as that usually means the system clock went back in time.
        if (std::chrono::floor<std::chrono::seconds>(next - std::chrono::high_resolution_clock::now()) > 1s) {
            // printf("Skipping the next VI wait\n");
            next = std::chrono::high_resolution_clock::now();
        }
        ultramodern::sleep_until(next);
        auto time_now = ultramodern::time_since_start();
        // Calculate how many VIs have passed
        uint64_t new_total_vis = (time_now * (60 * ultramodern::get_speed_multiplier()) / 1000ms) + 1;
        if (new_total_vis > total_vis + 1) {
            //printf("Skipped % " PRId64 " frames in VI interupt thread!\n", new_total_vis - total_vis - 1);
        }
        total_vis = new_total_vis;

        // If the game hasn't started yet, set a dummy VI mode and origin.
        if (!ultramodern::is_game_started()) {
            static bool odd = false;
            set_dummy_vi(odd);
            odd = !odd;

            events_context.action_queue.enqueue(DummyWorkloadAction{events_context.vi.get_next_state()->framebuffer});
        }

        // Queue a screen update for the graphics thread with the current VI register state.
        // Doing this before the VI update is equivalent to updating the screen after the previous frame's scanout finished.
        if (ultramodern::det::enabled() && ultramodern::is_game_started()) {
            // Deterministic mode: retraces reach the game from det::idle (vi_retrace below); this thread only presents.
            ultramodern::renderer::ViRegs regs;
            {
                std::lock_guard lock{ events_context.message_mutex };
                regs = events_context.vi.regs;
            }
            events_context.action_queue.enqueue(ScreenUpdateAction{ regs });
            if (events_callbacks.vi_callback != nullptr) {
                events_callbacks.vi_callback();
            }
            continue;
        }
        events_context.action_queue.enqueue(ScreenUpdateAction{ events_context.vi.regs });

        // Update VI registers and swap VI modes.
        events_context.vi.update_vi();

        // If the game has started, handle sending VI and AI events.
        if (ultramodern::is_game_started()) {
            remaining_retraces--;
            
            std::lock_guard lock{ events_context.message_mutex };
            ViState* cur_state = events_context.vi.get_cur_state();
            if (remaining_retraces == 0) {
                if (cur_state->mq != NULLPTR) {
                    // Send a message to the VI queue, and do not set it to be requeued if the queue was full.
                    // The worst case scenario is that the game misses a VI message and has to wait a little longer for the next. 
                    ultramodern::enqueue_external_message_src(cur_state->mq, cur_state->msg, false, ultramodern::EventMessageSource::Vi);
                }
                remaining_retraces = cur_state->retrace_count;
            }
            if (events_context.ai.mq != NULLPTR) {
                // Send a message to the VI queue, and do not set it to be requeued if the queue was full for the same reason as the VI message above.
                ultramodern::enqueue_external_message_src(events_context.ai.mq, events_context.ai.msg, false, ultramodern::EventMessageSource::Ai);
            }
        }

        if (events_callbacks.vi_callback != nullptr) {
            events_callbacks.vi_callback();
        }
    }
}

void sp_complete() {
    uint8_t* rdram = events_context.rdram;
    std::lock_guard lock{ events_context.message_mutex };
    ultramodern::enqueue_external_message_src(events_context.sp.mq, events_context.sp.msg, false, ultramodern::EventMessageSource::Sp);
}

void dp_complete() {
    uint8_t* rdram = events_context.rdram;
    std::lock_guard lock{ events_context.message_mutex };
    ultramodern::enqueue_external_message_src(events_context.dp.mq, events_context.dp.msg, false, ultramodern::EventMessageSource::Dp);
}

// The list in send_dl: whether it has been reported drawn, and how long it
// waited for the graphics thread (graphics thread only).
static bool list_drawn_reported = false;
static uint32_t current_list_wait_us = 0;

uint32_t ultramodern::renderer::list_wait_us() {
    return current_list_wait_us;
}

void ultramodern::renderer::report_list_drawn() {
    if (list_drawn_reported) {
        return;
    }
    list_drawn_reported = true;
    dp_complete();
}

// Asynchronous audio tasks (see ultramodern.hpp). The game hears that such a
// task is done when it submits it and may reuse its OSTask at once, so the
// task thread runs a copy taken then; the marker in its queue stands for the
// oldest copy.
static std::atomic_bool async_audio = false;
static std::atomic_uint32_t audio_submitted = 0;
static std::atomic_uint32_t audio_completed = 0;
static std::mutex async_audio_mutex;
static std::deque<OSTask> async_audio_copies;
static OSTask async_audio_marker_storage;
static OSTask* const async_audio_marker = &async_audio_marker_storage;

void ultramodern::set_async_audio_tasks(bool on) { async_audio = on; }
bool ultramodern::async_audio_tasks() { return async_audio; }
uint32_t ultramodern::audio_tasks_submitted() { return audio_submitted; }
uint32_t ultramodern::audio_tasks_completed() { return audio_completed; }

void task_thread_func(uint8_t* rdram, moodycamel::LightweightSemaphore* thread_ready) {
    ultramodern::set_native_thread_name("SP Task Thread");
    ultramodern::set_native_thread_priority(ultramodern::ThreadPriority::Normal);

    // Notify the caller thread that this thread is ready.
    thread_ready->signal();

    while (true) {
        // Wait until an RSP task has been sent
        OSTask* task;
        events_context.sp_task_queue.wait_dequeue(task);

        if (task == nullptr) {
            return;
        }

        // An asynchronous audio task (submit_rsp_task): its completion has
        // been reported; run the copy and hand over the samples waiting on it.
        if (task == async_audio_marker) {
            OSTask local;
            {
                std::lock_guard lock{ async_audio_mutex };
                local = async_audio_copies.front();
                async_audio_copies.pop_front();
            }
            if (!ultramodern::rsp::run_task(PASS_RDRAM &local)) {
                fprintf(stderr, "Failed to execute task type: %" PRIu32 "\n", local.t.type);
                ULTRAMODERN_QUICK_EXIT();
            }
            ultramodern::flush_audio_buffers(PASS_RDRAM ++audio_completed);
            continue;
        }

        // A graphics task comes here for its RSP acknowledgement alone (see
        // submit_rsp_task); the renderer has it from the action queue.
        if (task->t.type == M_GFXTASK) {
            sp_complete();
            continue;
        }

        if (!ultramodern::rsp::run_task(PASS_RDRAM task)) {
            fprintf(stderr, "Failed to execute task type: %" PRIu32 "\n", task->t.type);
            ULTRAMODERN_QUICK_EXIT();
        }

        // Tell the game that the RSP has completed
        sp_complete();
    }
}

std::atomic_uint32_t display_refresh_rate = 60;
std::atomic<float> resolution_scale = 1.0f;

uint32_t ultramodern::get_target_framerate(uint32_t original) {
    auto& config = ultramodern::renderer::get_graphics_config();

    switch (config.rr_option) {
        case ultramodern::renderer::RefreshRate::Original:
        default:
            return original;
        case ultramodern::renderer::RefreshRate::Manual:
            return config.rr_manual_value;
        case ultramodern::renderer::RefreshRate::Display:
            return display_refresh_rate.load();
    }
}

uint32_t ultramodern::get_display_refresh_rate() {
    return display_refresh_rate.load();
}

float ultramodern::get_resolution_scale() {
    return resolution_scale.load();
}

void ultramodern::trigger_config_action() {
    events_context.action_queue.enqueue(UpdateConfigAction{});
}

std::atomic<ultramodern::renderer::SetupResult> renderer_setup_result = ultramodern::renderer::SetupResult::Success;
std::atomic<ultramodern::renderer::GraphicsApi> renderer_chosen_api = ultramodern::renderer::GraphicsApi::Auto;

void gfx_thread_func(uint8_t* rdram, moodycamel::LightweightSemaphore* thread_ready, ultramodern::renderer::WindowHandle window_handle) {
    bool enabled_instant_present = false;
    using namespace std::chrono_literals;

    ultramodern::set_native_thread_name("Gfx Thread");
    ultramodern::set_native_thread_priority(ultramodern::ThreadPriority::Normal);

    auto old_config = ultramodern::renderer::get_graphics_config();

    auto renderer_context = ultramodern::renderer::create_render_context(rdram, window_handle, ultramodern::renderer::get_graphics_config().developer_mode);

    renderer_chosen_api.store(renderer_context->get_chosen_api());
    if (!renderer_context->valid()) {
        renderer_setup_result.store(renderer_context->get_setup_result());
        // Notify the caller thread that this thread is ready.
        thread_ready->signal();
        return;
    }

    if (events_callbacks.gfx_init_callback != nullptr) {
        events_callbacks.gfx_init_callback();
    }

    ultramodern::rsp::init();

    // Notify the caller thread that this thread is ready.
    thread_ready->signal();

    while (!exited) {
        // Try to pull an action from the queue
        Action action;
        if (events_context.action_queue.wait_dequeue_timed(action, 1ms)) {
            // Determine the action type and act on it
            if (const auto* task_action = std::get_if<SpTaskAction>(&action)) {
                // The RSP's part of the task was reported complete when it was submitted (submit_rsp_task, through the
                // task thread); the RDP's part follows the renderer below (dp_complete), so the game won't start another
                // graphics task before this one is drawn. Deterministic mode posts its own completions from the game thread.
                ultramodern::measure_input_latency();

                PTR(u64) displaylist = task_action->task.t.data_ptr;
                ultramodern::extensions::on_displaylist_submitted(displaylist);

                [[maybe_unused]] auto renderer_start = std::chrono::high_resolution_clock::now();
                list_drawn_reported = false;
                current_list_wait_us = static_cast<uint32_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(renderer_start - task_action->submitted).count());
                renderer_context->send_dl(&task_action->task);
                if (task_action->parsed != nullptr) {
                    task_action->parsed->signal();
                }
                [[maybe_unused]] auto renderer_end = std::chrono::high_resolution_clock::now();

                // Deterministic mode posts its own completions from the game thread.
                if (!ultramodern::det::enabled()) {
                    ultramodern::renderer::report_list_drawn();
                }
                // TODO hook the parsed event up to the actual parsing point when a callback is added to RT64.
                ultramodern::extensions::on_displaylist_parsed(displaylist);
                ultramodern::extensions::on_displaylist_completed(displaylist);
                // printf("Renderer ProcessDList time: %d us\n", static_cast<u32>(std::chrono::duration_cast<std::chrono::microseconds>(renderer_end - renderer_start).count()));
            }
            else if (const auto* screen_update_action = std::get_if<ScreenUpdateAction>(&action)) {
                events_context.vi.update_screen_regs = screen_update_action->regs;
                renderer_context->update_screen();
                display_refresh_rate = renderer_context->get_display_framerate();
                resolution_scale = renderer_context->get_resolution_scale();
            }
            else if (const auto* config_action = std::get_if<UpdateConfigAction>(&action)) {
                (void)config_action;
                auto new_config = ultramodern::renderer::get_graphics_config();
                if (renderer_context->update_config(old_config, new_config)) {
                    old_config = new_config;
                }
            }
            else if (const auto* dummy_workload_action = std::get_if<DummyWorkloadAction>(&action)) {
                renderer_context->send_dummy_workload(dummy_workload_action->fb_address);
            }
        }
    }

    graphics_shutdown_ready.wait();
    renderer_context->shutdown();
}

#define VI_CTRL_TYPE_16             0x00002
#define VI_CTRL_TYPE_32             0x00003
#define VI_CTRL_GAMMA_DITHER_ON     0x00004
#define VI_CTRL_GAMMA_ON            0x00008
#define VI_CTRL_DIVOT_ON            0x00010
#define VI_CTRL_SERRATE_ON          0x00040
#define VI_CTRL_ANTIALIAS_MASK      0x00300
#define VI_CTRL_ANTIALIAS_MODE_1    0x00100
#define VI_CTRL_ANTIALIAS_MODE_2    0x00200
#define VI_CTRL_ANTIALIAS_MODE_3    0x00300
#define VI_CTRL_PIXEL_ADV_MASK      0x01000
#define VI_CTRL_PIXEL_ADV_1         0x01000
#define VI_CTRL_PIXEL_ADV_2         0x02000
#define VI_CTRL_PIXEL_ADV_3         0x03000
#define VI_CTRL_DITHER_FILTER_ON    0x10000

static const OSViMode dummy_mode = []() {
    OSViMode ret{};

    ret.type = 2;
    ret.comRegs.ctrl = VI_CTRL_TYPE_16 | VI_CTRL_GAMMA_DITHER_ON | VI_CTRL_GAMMA_ON | VI_CTRL_DIVOT_ON | VI_CTRL_ANTIALIAS_MODE_1 | VI_CTRL_PIXEL_ADV_3;
    ret.comRegs.width = 0x140;
    ret.comRegs.burst = 0x03E52239;
    ret.comRegs.vSync = 0x20D;
    ret.comRegs.hSync = 0xC15;
    ret.comRegs.leap = 0x0C150C15;
    ret.comRegs.hStart = 0x006C02EC;
    ret.comRegs.xScale = 0x200;
    ret.comRegs.vCurrent = 0x0;

    for (int field = 0; field < 2; field++) {
        ret.fldRegs[field].origin = 0x280;
        ret.fldRegs[field].yScale = 0x400;
        ret.fldRegs[field].vStart = 0x2501FF;
        ret.fldRegs[field].vBurst = 0xE0204;
        ret.fldRegs[field].vIntr = 0x2;
    }

    return ret;
}();

void set_dummy_vi(bool odd) {
    ViState* next_state = events_context.vi.get_next_state();
    next_state->mode = &dummy_mode;
    next_state->control = next_state->mode->comRegs.ctrl;
    // Set up a dummy framebuffer.
    next_state->framebuffer = 0x80700000;
    if (odd) {
        next_state->framebuffer += 0x25800;
    }
}

extern "C" void osViSwapBuffer(RDRAM_ARG PTR(void) frameBufPtr) {
    std::lock_guard lock{ events_context.message_mutex };
    events_context.vi.get_next_state()->framebuffer = frameBufPtr;
}

extern "C" void osViSetMode(RDRAM_ARG PTR(OSViMode) mode_) {
    std::lock_guard lock{ events_context.message_mutex };
    OSViMode* mode = TO_PTR(OSViMode, mode_);
    ViState* next_state = events_context.vi.get_next_state();
    next_state->mode = mode;
    // libultra's osViSetMode overwrites the state (state = VI_STATE_MODE_UPDATED), which clears
    // BLACK and REPEATLINE. Games rely on this to unblank the screen after osViBlack(TRUE).
    next_state->state &= ~(VI_STATE_BLACK | VI_STATE_REPEATLINE);
    next_state->control = next_state->mode->comRegs.ctrl;
}

#define OS_VI_GAMMA_ON          0x0001
#define OS_VI_GAMMA_OFF         0x0002
#define OS_VI_GAMMA_DITHER_ON   0x0004
#define OS_VI_GAMMA_DITHER_OFF  0x0008
#define OS_VI_DIVOT_ON          0x0010
#define OS_VI_DIVOT_OFF         0x0020
#define OS_VI_DITHER_FILTER_ON  0x0040
#define OS_VI_DITHER_FILTER_OFF 0x0080

extern "C" void osViSetSpecialFeatures(uint32_t func) {
    std::lock_guard lock{ events_context.message_mutex };
    ViState* next_state = events_context.vi.get_next_state();
    uint32_t* control_out = &next_state->control;
    if ((func & OS_VI_GAMMA_ON) != 0) {
        *control_out |= VI_CTRL_GAMMA_ON;
    }

    if ((func & OS_VI_GAMMA_OFF) != 0) {
        *control_out &= ~VI_CTRL_GAMMA_ON;
    }

    if ((func & OS_VI_GAMMA_DITHER_ON) != 0) {
        *control_out |= VI_CTRL_GAMMA_DITHER_ON;
    }

    if ((func & OS_VI_GAMMA_DITHER_OFF) != 0) {
        *control_out &= ~VI_CTRL_GAMMA_DITHER_ON;
    }

    if ((func & OS_VI_DIVOT_ON) != 0) {
        *control_out |= VI_CTRL_DIVOT_ON;
    }

    if ((func & OS_VI_DIVOT_OFF) != 0) {
        *control_out &= ~VI_CTRL_DIVOT_ON;
    }

    if ((func & OS_VI_DITHER_FILTER_ON) != 0) {
        *control_out |= VI_CTRL_DITHER_FILTER_ON;
        *control_out &= ~VI_CTRL_ANTIALIAS_MASK;
    }

    if ((func & OS_VI_DITHER_FILTER_OFF) != 0) {
        *control_out &= ~VI_CTRL_DITHER_FILTER_ON;
        *control_out |= next_state->mode->comRegs.ctrl & VI_CTRL_ANTIALIAS_MASK;
    }
}

extern "C" void osViBlack(uint8_t active) {
    std::lock_guard lock{ events_context.message_mutex };
    ViState* next_state = events_context.vi.get_next_state();
    uint32_t* state_out = &next_state->state;
    if (active) {
        *state_out |= VI_STATE_BLACK;
    } else {
        *state_out &= ~VI_STATE_BLACK;
    }
}

extern "C" void osViRepeatLine(uint8_t active) {
    std::lock_guard lock{ events_context.message_mutex };
    ViState* next_state = events_context.vi.get_next_state();
    uint32_t* state_out = &next_state->state;
    if (active) {
        *state_out |= VI_STATE_REPEATLINE;
    } else {
        *state_out &= ~VI_STATE_REPEATLINE;
    }
}

extern "C" void osViSetXScale(float scale) {
    if (scale != 1.0f) {
        assert(false);
    }
}

extern "C" void osViSetYScale(float scale) {
    if (scale != 1.0f) {
        assert(false);
    }
}

extern "C" PTR(void) osViGetNextFramebuffer() {
    // A game polling this in a loop waits for a retrace; let virtual time reach it (deterministic mode).
    if (ultramodern::det::enabled()) {
        (void)ultramodern::det::now_for_read();
    }
    return events_context.vi.get_next_state()->framebuffer;
}

extern "C" PTR(void) osViGetCurrentFramebuffer() {
    // A game polling this in a loop waits for a retrace; let virtual time reach it (deterministic mode).
    if (ultramodern::det::enabled()) {
        (void)ultramodern::det::now_for_read();
    }
    return events_context.vi.get_cur_state()->framebuffer;
}

void ultramodern::submit_rsp_task(RDRAM_ARG PTR(OSTask) task_) {
    OSTask* task = TO_PTR(OSTask, task_);

    // Deterministic mode: post completions from this (game) thread at a fixed point instead of from
    // the task/gfx threads whenever they finish. Graphics still render on the gfx thread.
    if (ultramodern::det::enabled()) {
        if (task->t.type == M_GFXTASK) {
            moodycamel::LightweightSemaphore parsed;
            events_context.action_queue.enqueue(SpTaskAction{ *task, &parsed });
            parsed.wait();
            sp_complete();
            dp_complete();
        } else {
            if (!ultramodern::rsp::run_task(PASS_RDRAM task)) {
                fprintf(stderr, "Failed to execute task type: %" PRIu32 "\n", task->t.type);
                ULTRAMODERN_QUICK_EXIT();
            }
            sp_complete();
        }
        return;
    }

    // Send gfx tasks to the graphics action queue. The RSP's part of the task is reported complete here and now: the
    // graphics thread used to send that message when it took the list, which is only after the previous list's renderer
    // work, and with a renderer that takes a list for tens of milliseconds (rt64-3ds on a 3DS) the game's scheduler saw
    // the RSP busy for most of every frame and held its audio tasks until then. The message is queued like any external
    // one and reaches the scheduler at its next receive, after it has noted the task as started.
    if (task->t.type == M_GFXTASK) {
        events_context.action_queue.enqueue(SpTaskAction{ *task, nullptr, std::chrono::high_resolution_clock::now() });
        sp_complete();
        return;
    }
    // An asynchronous audio task: reported complete at once as well, and run by the task thread from a copy.
    if (task->t.type == M_AUDTASK && async_audio) {
        {
            std::lock_guard lock{ async_audio_mutex };
            async_audio_copies.push_back(*task);
        }
        audio_submitted++;
        sp_complete();
        events_context.sp_task_queue.enqueue(async_audio_marker);
        return;
    }
    // Set all other tasks as the RSP task
    events_context.sp_task_queue.enqueue(task);
}

// Deterministic mode's retrace (called from det::idle on a game thread): what vi_thread_func does
// for the game on a host-timed retrace.
void ultramodern::det::vi_retrace(RDRAM_ARG1) {
    static int remaining_retraces = 1;
    std::lock_guard lock{ events_context.message_mutex };
    events_context.vi.update_vi();
    ViState* cur_state = events_context.vi.get_cur_state();
    if (--remaining_retraces <= 0) {
        if (cur_state->mq != NULLPTR) {
            ultramodern::enqueue_external_message_src(cur_state->mq, cur_state->msg, false, ultramodern::EventMessageSource::Vi);
        }
        remaining_retraces = cur_state->retrace_count;
    }
    if (events_context.ai.mq != NULLPTR) {
        ultramodern::enqueue_external_message_src(events_context.ai.mq, events_context.ai.msg, false, ultramodern::EventMessageSource::Ai);
    }
}

void ultramodern::send_si_message() {
    ultramodern::enqueue_external_message_src(events_context.si.mq, events_context.si.msg, false, ultramodern::EventMessageSource::Si);
}

void ultramodern::init_events(RDRAM_ARG ultramodern::renderer::WindowHandle window_handle) {
    moodycamel::LightweightSemaphore gfx_thread_ready;
    moodycamel::LightweightSemaphore task_thread_ready;
    events_context.rdram = rdram;
    moodycamel::LightweightSemaphore* gfx_ready_ptr = &gfx_thread_ready;
    moodycamel::LightweightSemaphore* task_ready_ptr = &task_thread_ready;
    events_context.sp.gfx_thread = ultramodern::HostThread{ultramodern::HostThreadKind::Gfx, [=]() {
        gfx_thread_func(rdram, gfx_ready_ptr, window_handle);
    }};
    events_context.sp.task_thread = ultramodern::HostThread{ultramodern::HostThreadKind::SpTask, [=]() {
        task_thread_func(rdram, task_ready_ptr);
    }};

    // Wait for the two sp threads to be ready before continuing to prevent the game from
    // running before we're able to handle RSP tasks.
    gfx_thread_ready.wait();
    task_thread_ready.wait();

    ultramodern::renderer::SetupResult setup_result = renderer_setup_result.load();
    if (setup_result != ultramodern::renderer::SetupResult::Success) {
        auto show_renderer_error = [](const std::string& msg) {
            std::string error_msg = "An error has been encountered on startup: " + msg;

            ultramodern::error_handling::message_box(error_msg.c_str());
        };

        const std::string driver_os_suffix = "\nPlease make sure your GPU drivers and your OS are up to date.";
        switch (setup_result) {
            case ultramodern::renderer::SetupResult::Success:
                break;
            case ultramodern::renderer::SetupResult::DynamicLibrariesNotFound:
                show_renderer_error("Failed to load dynamic libraries. Make sure the DLLs are next to the recomp executable.");
                break;
            case ultramodern::renderer::SetupResult::InvalidGraphicsAPI:
                show_renderer_error(ultramodern::renderer::get_graphics_api_name(renderer_chosen_api.load()) + " is not supported on this platform. Please select a different graphics API.");
                break;
            case ultramodern::renderer::SetupResult::GraphicsAPINotFound:
                show_renderer_error("Unable to initialize " + ultramodern::renderer::get_graphics_api_name(renderer_chosen_api.load()) + "." + driver_os_suffix);
                break;
            case ultramodern::renderer::SetupResult::GraphicsDeviceNotFound:
                show_renderer_error("Unable to find compatible graphics device." + driver_os_suffix);
                break;
        }
        throw std::runtime_error("Failed to initialize the renderer");
    }

    events_context.vi.thread = ultramodern::HostThread{ultramodern::HostThreadKind::Vi, vi_thread_func};
}

void ultramodern::join_event_threads() {
    events_context.sp.gfx_thread.join();
    events_context.vi.thread.join();

    // Send a null RSP task to indicate that the RSP task thread should exit.
#ifdef __3DS__
    // Through an explicit producer: the implicit ones are keyed by
    // std::this_thread::get_id(), which on the 3DS main thread (not a libctru
    // thread) is the empty id the queue reserves, so that enqueue fails and
    // the join below never returns.
    moodycamel::ProducerToken token(events_context.sp_task_queue);
    events_context.sp_task_queue.enqueue(token, nullptr);
#else
    events_context.sp_task_queue.enqueue(nullptr);
#endif
    events_context.sp.task_thread.join();
}
