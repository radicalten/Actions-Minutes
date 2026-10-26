#ifndef __ULTRAMODERN_HOST_THREAD_HPP__
#define __ULTRAMODERN_HOST_THREAD_HPP__

#include <cstddef>
#include <functional>

#ifdef __3DS__
extern "C" {
#include <3ds/types.h>
#include <3ds/thread.h>
}
#else
#include <thread>
#endif

// The host threads ultramodern and librecomp create, behind one type so a
// platform without std::thread attributes (the 3DS: libctru threads need an
// explicit stack size, priority and core) can size and place each kind.
// Everywhere else it is a thin wrapper over std::thread.
namespace ultramodern {
    enum class HostThreadKind {
        GameStart,  // runs preinit and the game's entrypoint
        Game,       // one per osCreateThread
        Gfx,        // renderer context + display lists
        SpTask,     // non-graphics RSP tasks (audio)
        Vi,         // 60 Hz pacing
        Timer,
        Cleaner,    // joins finished game threads
        Saving,
    };

    struct HostThreadSpec {
        size_t stack_size;
        int priority;   // platform units (3DS: 0x18 high .. 0x3F low)
        int core;       // platform units (3DS: 0, 1, 2, 3, -1 all, -2 default)
    };

    using host_thread_spec_callback_t = HostThreadSpec (*)(HostThreadKind kind);
    // Optional: a platform layer can size and place the threads. Unused on std::thread platforms.
    void set_host_thread_spec_callback(host_thread_spec_callback_t callback);
    HostThreadSpec get_host_thread_spec(HostThreadKind kind);

    class HostThread {
    public:
        HostThread() = default;
        HostThread(HostThreadKind kind, std::function<void()> func);
        HostThread(const HostThread&) = delete;
        HostThread& operator=(const HostThread&) = delete;
        HostThread(HostThread&& other) noexcept;
        HostThread& operator=(HostThread&& other) noexcept;
        ~HostThread();

        bool joinable() const;
        void join();
        void detach();
    private:
#ifdef __3DS__
        Thread thread_ = nullptr;
#else
        std::thread thread_;
#endif
    };
}

#endif
