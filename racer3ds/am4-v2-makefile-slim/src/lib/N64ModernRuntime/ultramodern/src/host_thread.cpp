#include <cstdio>
#include <cstdlib>
#include <utility>

#include "ultramodern/host_thread.hpp"

static ultramodern::host_thread_spec_callback_t spec_callback = nullptr;

void ultramodern::set_host_thread_spec_callback(host_thread_spec_callback_t callback) {
    spec_callback = callback;
}

ultramodern::HostThreadSpec ultramodern::get_host_thread_spec(HostThreadKind kind) {
    if (spec_callback != nullptr) {
        return spec_callback(kind);
    }
#ifdef __3DS__
    // Defaults for a New 3DS: game code on the app core, the renderer and the
    // audio microcode on core 2 (audio above the renderer: core 2 does not
    // timeslice), the pacing threads above the game so their sleeps wake it.
    switch (kind) {
        case HostThreadKind::GameStart:
        case HostThreadKind::Game:    return { 128 * 1024, 0x30, 0 };
        case HostThreadKind::Gfx:     return { 256 * 1024, 0x2C, 2 };
        case HostThreadKind::SpTask:  return { 64 * 1024,  0x24, 2 };
        case HostThreadKind::Vi:
        case HostThreadKind::Timer:   return { 32 * 1024,  0x28, 0 };
        case HostThreadKind::Cleaner:
        case HostThreadKind::Saving:
        default:                      return { 32 * 1024,  0x30, 0 };
    }
#else
    (void)kind;
    return { 0, 0, 0 };
#endif
}

#ifdef __3DS__

static void host_thread_trampoline(void* arg) {
    auto* func = static_cast<std::function<void()>*>(arg);
    (*func)();
    delete func;
}

ultramodern::HostThread::HostThread(HostThreadKind kind, std::function<void()> func) {
    HostThreadSpec spec = get_host_thread_spec(kind);
    auto* boxed = new std::function<void()>(std::move(func));
    thread_ = threadCreate(host_thread_trampoline, boxed, spec.stack_size, spec.priority, spec.core, false);
    if (thread_ == nullptr && spec.core != -2) {
        // The requested core may be unavailable (a .3dsx under the Homebrew
        // Launcher, or an Old 3DS): fall back to the default core.
        fprintf(stderr, "[HostThread] core %d refused for kind %d, using the default core\n", spec.core, (int)kind);
        thread_ = threadCreate(host_thread_trampoline, boxed, spec.stack_size, spec.priority, -2, false);
    }
    if (thread_ == nullptr) {
        fprintf(stderr, "[HostThread] threadCreate failed for kind %d (stack %zu)\n", (int)kind, spec.stack_size);
        delete boxed;
        abort();
    }
}

ultramodern::HostThread::HostThread(HostThread&& other) noexcept : thread_(other.thread_) {
    other.thread_ = nullptr;
}

ultramodern::HostThread& ultramodern::HostThread::operator=(HostThread&& other) noexcept {
    if (this != &other) {
        if (thread_ != nullptr) {
            join();
        }
        thread_ = other.thread_;
        other.thread_ = nullptr;
    }
    return *this;
}

ultramodern::HostThread::~HostThread() {
    // std::thread would terminate here; joining is the safer failure mode.
    if (thread_ != nullptr) {
        join();
    }
}

bool ultramodern::HostThread::joinable() const {
    return thread_ != nullptr;
}

void ultramodern::HostThread::join() {
    if (thread_ != nullptr) {
        threadJoin(thread_, UINT64_MAX);
        threadFree(thread_);
        thread_ = nullptr;
    }
}

void ultramodern::HostThread::detach() {
    if (thread_ != nullptr) {
        threadDetach(thread_);
        thread_ = nullptr;
    }
}

#else

ultramodern::HostThread::HostThread(HostThreadKind kind, std::function<void()> func) : thread_(std::move(func)) {
    (void)kind;
}

ultramodern::HostThread::HostThread(HostThread&& other) noexcept : thread_(std::move(other.thread_)) {}

ultramodern::HostThread& ultramodern::HostThread::operator=(HostThread&& other) noexcept {
    thread_ = std::move(other.thread_);
    return *this;
}

ultramodern::HostThread::~HostThread() = default;

bool ultramodern::HostThread::joinable() const {
    return thread_.joinable();
}

void ultramodern::HostThread::join() {
    thread_.join();
}

void ultramodern::HostThread::detach() {
    thread_.detach();
}

#endif
