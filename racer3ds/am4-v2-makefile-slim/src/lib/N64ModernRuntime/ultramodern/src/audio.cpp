#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include <cassert>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>

static uint32_t sample_rate = 48000;

static ultramodern::audio_callbacks_t audio_callbacks;

void ultramodern::set_audio_callbacks(const ultramodern::audio_callbacks_t& callbacks) {
    audio_callbacks = callbacks;
}

void ultramodern::init_audio() {
    // Pick an initial dummy sample rate; this will be set by the game later to the true sample rate.
    set_audio_frequency(48000);
}

void ultramodern::set_audio_frequency(uint32_t freq) {
    if (audio_callbacks.set_frequency) {
        audio_callbacks.set_frequency(freq);
    }
    sample_rate = freq;
}

// Asynchronous audio tasks (ultramodern.hpp): buffers the game handed over
// before the tasks that fill them had run, each with the count of tasks that
// must complete first. The host's queue_samples is called under the lock, from
// the game's thread or the task thread.
namespace {
struct PendingBuffer {
    PTR(int16_t) data;
    uint32_t byte_count;
    uint32_t needs;
};
std::mutex pending_mutex;
std::deque<PendingBuffer> pending_buffers;
std::atomic_uint32_t pending_bytes = 0;

void queue_now(RDRAM_ARG PTR(int16_t) audio_data_, uint32_t byte_count) {
    uint32_t sample_count = byte_count / sizeof(int16_t);
    if (sample_count > 0 && audio_callbacks.queue_samples) {
        audio_callbacks.queue_samples(TO_PTR(int16_t, audio_data_), sample_count);
    }
}
}

uint32_t ultramodern::pending_audio_bytes() {
    return pending_bytes;
}

void ultramodern::flush_audio_buffers(RDRAM_ARG uint32_t completed) {
    std::lock_guard lock{ pending_mutex };
    // Wrapping counters: a buffer is due when `completed` has reached its count.
    while (!pending_buffers.empty() && (int32_t)(completed - pending_buffers.front().needs) >= 0) {
        const PendingBuffer buf = pending_buffers.front();
        pending_buffers.pop_front();
        // Out of the pending count first: a length read meanwhile from the
        // game's thread then misses this buffer for a moment (the game makes
        // one frame a little larger) rather than counting it twice (smaller,
        // and the device can run short).
        pending_bytes -= buf.byte_count;
        queue_now(PASS_RDRAM buf.data, buf.byte_count);
    }
}

void ultramodern::queue_audio_buffer(RDRAM_ARG PTR(int16_t) audio_data_, uint32_t byte_count) {
    // Ensure that the byte count is an integer multiple of samples.
    assert((byte_count & 1) == 0);

    if (ultramodern::async_audio_tasks()) {
        const uint32_t submitted = ultramodern::audio_tasks_submitted();
        // Two tasks still to run: the game is about to build a third into
        // the first one's command list and output buffer. Hold it here.
        while ((int32_t)(submitted - ultramodern::audio_tasks_completed()) >= 2) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        std::lock_guard lock{ pending_mutex };
        if (ultramodern::audio_tasks_completed() != submitted || !pending_buffers.empty()) {
            pending_buffers.push_back(PendingBuffer{ audio_data_, byte_count, submitted });
            pending_bytes += byte_count;
            return;
        }
        queue_now(PASS_RDRAM audio_data_, byte_count);
        return;
    }

    // Queue the swapped audio data.
    queue_now(PASS_RDRAM audio_data_, byte_count);
}

// For SDL2
//uint32_t buffer_offset_frames = 1;
// For Godot
float buffer_offset_frames = 0.5f;

// If there's ever any audio popping, check here first. Some games are very sensitive to
// the remaining sample count and reporting a number that's too high here can lead to issues.
// Reporting a number that's too low can lead to audio lag in some games.
uint32_t ultramodern::get_remaining_audio_bytes() {
    // Get the number of remaining buffered audio bytes.
    uint32_t buffered_byte_count;
    if (audio_callbacks.get_frames_remaining != nullptr) {
        buffered_byte_count = audio_callbacks.get_frames_remaining() * 2 * sizeof(int16_t);
    }
    else {
        buffered_byte_count = 100;
    }
    // Buffers of asynchronous audio tasks that have yet to reach the host are
    // counted by the host itself (pending_audio_bytes), before its margin.
    // Adjust the reported count to be some number of refreshes in the future, which helps ensure that
    // there are enough samples even if the audio thread experiences a small amount of lag. This prevents
    // audio popping on games that use the buffered audio byte count to determine how many samples
    // to generate.
    uint32_t samples_per_vi = (sample_rate / 60);
    if (buffered_byte_count > static_cast<uint32_t>(buffer_offset_frames * sizeof(int16_t) * samples_per_vi)) {
        buffered_byte_count -= static_cast<uint32_t>(buffer_offset_frames * sizeof(int16_t) * samples_per_vi);
    }
    else {
        buffered_byte_count = 0;
    }
    return buffered_byte_count;
}
