#ifndef __ultramodern_HPP__
#define __ultramodern_HPP__

#include <thread>
#include "ultramodern/host_thread.hpp"
#include <cassert>
#include <stdexcept>
#include <span>
#include <chrono>
#include <filesystem>

#undef MOODYCAMEL_DELETE_FUNCTION
#define MOODYCAMEL_DELETE_FUNCTION = delete
#include "lightweightsemaphore.h"

#include "ultra64.h"

#include "ultramodern/error_handling.hpp"
#include "ultramodern/events.hpp"
#include "ultramodern/input.hpp"
#include "ultramodern/renderer_context.hpp"
#include "ultramodern/rsp.hpp"
#include "ultramodern/threads.hpp"

struct UltraThreadContext {
    ultramodern::HostThread host_thread;
    moodycamel::LightweightSemaphore running;
    moodycamel::LightweightSemaphore initialized;
    // CPU accounting (ultramodern::get_thread_loads): the thread's slot and
    // the host tick at which it last started running (0: not running).
    int32_t load_slot = -1;
    uint32_t load_since = 0;
};

namespace ultramodern {

constexpr uint32_t save_size = 1024 * 1024 / 8; // Maximum save size, 1Mbit for flash

// Initialization.
void preinit(RDRAM_ARG renderer::WindowHandle window_handle);
void init_saving(RDRAM_ARG1);
void init_events(RDRAM_ARG renderer::WindowHandle window_handle);
void init_timers(RDRAM_ARG1);
void init_thread_cleanup();

// Saving
void change_save_file(const std::u8string& subfolder, const std::u8string& name);
std::filesystem::path get_save_file_path();

// Thread queues.
constexpr PTR(PTR(OSThread)) running_queue = (PTR(PTR(OSThread)))-1;

void thread_queue_insert(RDRAM_ARG PTR(PTR(OSThread)) queue, PTR(OSThread) toadd);
PTR(OSThread) thread_queue_pop(RDRAM_ARG PTR(PTR(OSThread)) queue);
bool thread_queue_remove(RDRAM_ARG PTR(PTR(OSThread)) queue_, PTR(OSThread) t_);
bool thread_queue_empty(RDRAM_ARG PTR(PTR(OSThread)) queue);
PTR(OSThread) thread_queue_peek(RDRAM_ARG PTR(PTR(OSThread)) queue);

// Message queues.
enum class EventMessageSource : int {
    Timer,
    Sp,
    Si,
    Ai,
    Vi,
    Pi,
    Dp,
};

struct MessageQueueControl {
    bool requeue_timer = true;
    bool requeue_sp = true;
    bool requeue_si = true;
    bool requeue_ai = false;
    bool requeue_vi = false;
    bool requeue_pi = false;
    bool requeue_dp = true;
};
void set_message_queue_control(const MessageQueueControl& mqc);
void enqueue_external_message_src(PTR(OSMesgQueue) mq, OSMesg msg, bool jam, EventMessageSource src);
void enqueue_external_message(PTR(OSMesgQueue) mq, OSMesg msg, bool jam, bool requeue_if_blocked);
void wait_for_external_message(RDRAM_ARG1);
void wait_for_external_message_timed(RDRAM_ARG1, u32 millis);

// Thread scheduling.
void check_running_queue(RDRAM_ARG1);
void run_next_thread_and_wait(RDRAM_ARG1);
void resume_thread_and_wait(RDRAM_ARG OSThread* t);
void schedule_running_thread(RDRAM_ARG PTR(OSThread) t);
void cleanup_thread(UltraThreadContext* thread_context);
struct thread_terminated : std::exception {};

// Asynchronous audio tasks, for a host whose audio microcode takes a large
// part of a frame (an interpreter on a slow CPU). An SDK-style audio manager
// builds its next task only after the previous one's completion message, and
// the game's scheduler starts tasks only at a retrace: a task that takes
// much of a retrace interval makes the manager miss every other retrace and
// produce half the samples. With this on, an audio task's completion is
// reported as soon as the task thread takes it; the buffers the game then
// hands to osAiSetNextBuffer are passed on to the host when the tasks
// submitted by then have really run, osAiGetLength counts them as queued
// meanwhile, and osAiSetNextBuffer holds the game while two tasks are still
// outstanding (their command lists and output buffers are about to be
// reused). Only for a game that takes every task's output through
// osAiSetNextBuffer and nowhere else.
void set_async_audio_tasks(bool on);
bool async_audio_tasks();
uint32_t audio_tasks_submitted();
uint32_t audio_tasks_completed();
// The task thread, after each audio task: hands over the buffers waiting on it.
void flush_audio_buffers(RDRAM_ARG uint32_t completed);
// Bytes in buffers handed to osAiSetNextBuffer whose tasks have yet to run.
// A host that turns asynchronous audio tasks on must count them as queued in
// its get_frames_remaining, before any margin it subtracts there: a margin
// taken from the device's queue alone, clamped at zero, with these added on
// top, reports a full frame too much whenever the device holds less than the
// margin, and the game's audio manager then stays at its smallest frame and
// lets the device run dry (crackling with no underrun on record).
uint32_t pending_audio_bytes();

// Preemption for recompiled code. The runtime delivers external events (a VI
// retrace, a task's completion, a timer) to game threads only when the running
// game thread sends or receives a message, or when the game is idle; a thread
// that computes for long between messages holds every higher-priority thread
// back by that long, where the console's interrupts would have switched at
// once. Host threads raise `ultramodern_preempt_pending` whenever they queue
// such an event; recompiled code that tests it at function entry (a plain
// load) and calls ultramodern_preempt delivers the events there and switches
// to a woken higher-priority thread, as the interrupt would have.
extern "C" uint32_t ultramodern_preempt_pending;
extern "C" void ultramodern_preempt(uint8_t* rdram);
// The game's interrupt mask (osSetIntMask), per thread: with every interrupt
// masked (OS_IM_NONE) the thread is in a critical section and is not
// preempted. Returns the previous mask.
uint32_t set_int_mask(uint32_t mask);

// CPU accounting per game thread: the host ticks each thread has run for and
// the times it was woken, for a host's statistics (which of the game's
// threads the CPU goes to). Both counters wrap at 32 bits: a reader keeps its
// previous reading and subtracts.
struct ThreadLoad {
    PTR(OSThread) thread;
    uint32_t ticks;
    uint32_t wakeups;
};
// Copies the known threads' totals into `out` (up to `max`); returns how many.
size_t get_thread_loads(ThreadLoad* out, size_t max);
// ThreadLoad::ticks per second.
uint32_t thread_load_tick_rate();

enum class ThreadPriority {
    Low,
    Normal,
    High,
    VeryHigh,
    Critical
};

void set_native_thread_name(const std::string& name);
void set_native_thread_priority(ThreadPriority pri);
PTR(OSThread) this_thread();
void set_entrypoint_thread();
bool is_entrypoint_thread();
bool is_game_thread();
void submit_rsp_task(RDRAM_ARG PTR(OSTask) task);
void send_si_message();
uint32_t get_speed_multiplier();

// Deterministic mode (opt-in, for lockstep netplay and replays). Game time stops following the host
// clock: it advances only when every game thread is blocked, and VI retraces, timers and RSP/RDP
// completions are delivered at those points in a fixed order. The same inputs then give the same
// game state on every machine. Requires the game to have an idle thread (one that ends in an
// infinite loop, recompiled to pause_self). Call enable() before recomp::start.
namespace det {
    void enable();
    bool enabled();
    // Called on a game thread at every virtual VI retrace, before it is delivered; retrace counts
    // from 0. It may block, which is how lockstep waits for a remote player's inputs.
    using frame_callback_t = void (*)(uint64_t retrace);
    void set_frame_callback(frame_callback_t callback);
    uint64_t retrace_count();
}

// Time
std::chrono::high_resolution_clock::time_point get_start();
std::chrono::high_resolution_clock::duration time_since_start();
void measure_input_latency();
void sleep_milliseconds(uint32_t millis);
void sleep_until(const std::chrono::high_resolution_clock::time_point& time_point);

// Graphics
uint32_t get_target_framerate(uint32_t original);
uint32_t get_display_refresh_rate();
float get_resolution_scale();
void trigger_config_action();

// Audio
void init_audio();
void set_audio_frequency(uint32_t freq);
void queue_audio_buffer(RDRAM_ARG PTR(s16) audio_data, uint32_t byte_count);
uint32_t get_remaining_audio_bytes();

struct audio_callbacks_t {
    using queue_samples_t = void(int16_t*, size_t);
    using get_samples_remaining_t = size_t();
    using set_frequency_t = void(uint32_t);
    queue_samples_t* queue_samples;
    get_samples_remaining_t* get_frames_remaining;
    set_frequency_t* set_frequency;
};

// TODO: Most of the members of this struct are not used by ultramodern. Should we move them to librecomp instead?
struct gfx_callbacks_t {
    using gfx_data_t = void*;
    using create_gfx_t = gfx_data_t();
    using create_window_t = renderer::WindowHandle(gfx_data_t);
    using update_gfx_t = void(gfx_data_t);

    create_gfx_t* create_gfx;
    create_window_t* create_window;
    update_gfx_t* update_gfx;
};

bool is_game_started();
void quit();
void join_event_threads();
void join_thread_cleaner_thread();
void join_saving_thread();

void set_audio_callbacks(const audio_callbacks_t& callbacks);

/**
 * Register all the callbacks used by `ultramodern`, most of them being optional.
 *
 * The following arguments contain mandatory callbacks that need to be registered (i.e., can't be `nullptr`):
 * - `rsp_callbacks`
 * - `renderer_callbacks`
 *
 * It must be called only once and it must be called before `ultramodern::preinit`.
 */
void set_callbacks(
    const rsp::callbacks_t& rsp_callbacks,
    const renderer::callbacks_t& renderer_callbacks,
    const audio_callbacks_t& audio_callbacks,
    const input::callbacks_t& input_callbacks,
    const gfx_callbacks_t& gfx_callbacks,
    const events::callbacks_t& events_callbacks,
    const error_handling::callbacks_t& error_handling_callbacks,
    const threads::callbacks_t& threads_callbacks
);
} // namespace ultramodern

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define debug_printf(...)
//#define debug_printf(...) printf(__VA_ARGS__);

#endif
