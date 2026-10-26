#include <bitset>
#include <thread>

#include "blockingconcurrentqueue.h"

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "deterministic.hpp"

struct QueuedMessage {
    PTR(OSMesgQueue) mq;
    OSMesg mesg;
    bool jam;
    bool requeue_if_blocked;
};

static moodycamel::BlockingConcurrentQueue<QueuedMessage> external_messages {};
std::bitset<32> requeue_enabled;

void ultramodern::set_message_queue_control(const ultramodern::MessageQueueControl& mqc) {
    requeue_enabled.reset();
    requeue_enabled.set(static_cast<int>(EventMessageSource::Timer), mqc.requeue_timer);
    requeue_enabled.set(static_cast<int>(EventMessageSource::Sp), mqc.requeue_sp);
    requeue_enabled.set(static_cast<int>(EventMessageSource::Si), mqc.requeue_si);
    requeue_enabled.set(static_cast<int>(EventMessageSource::Ai), mqc.requeue_ai);
    requeue_enabled.set(static_cast<int>(EventMessageSource::Vi), mqc.requeue_vi);
    requeue_enabled.set(static_cast<int>(EventMessageSource::Pi), mqc.requeue_pi);
    requeue_enabled.set(static_cast<int>(EventMessageSource::Dp), mqc.requeue_dp);
}

// In deterministic mode every message must come from a game thread (posted at a fixed point);
// one from a host thread arrives at a host-timed moment and breaks determinism.
static void check_deterministic_source() {
    static bool warned = false;
    if (ultramodern::det::enabled() && !ultramodern::is_game_thread() && !warned) {
        warned = true;
        fprintf(stderr, "[ultramodern] deterministic mode: a host thread posted a game message\n");
    }
}

// Raised after every external message is queued (see ultramodern.hpp); the
// running game thread's next preemption check delivers the queue. Cleared
// before the queue is drained, so a message queued meanwhile raises it again.
// Diagnostic counts for a host's stall watch (rt64-3ds stall_watch.cpp), each
// only ever incremented: 0 external messages queued, 1 external messages that
// could not be sent (queue full), 2 game threads resumed, 3 timer thread
// passes, 4 VI thread passes, 5 idle waits for an external message,
// 6 blocking receives that waited, 7 blocking sends that waited.
extern "C" volatile uint32_t ultramodern_dbg_counts[8] = {};
#define DBG_COUNT(i) (ultramodern_dbg_counts[i] = ultramodern_dbg_counts[i] + 1)

extern "C" uint32_t ultramodern_preempt_pending = 0;

void ultramodern::enqueue_external_message_src(PTR(OSMesgQueue) mq, OSMesg msg, bool jam, EventMessageSource src) {
    DBG_COUNT(0);
    check_deterministic_source();
    external_messages.enqueue({mq, msg, jam, requeue_enabled[static_cast<int>(src)]});
    __atomic_store_n(&ultramodern_preempt_pending, 1u, __ATOMIC_RELEASE);
}

void ultramodern::enqueue_external_message(PTR(OSMesgQueue) mq, OSMesg msg, bool jam, bool requeue_if_blocked) {
    DBG_COUNT(0);
    external_messages.enqueue({mq, msg, jam, requeue_if_blocked});
    __atomic_store_n(&ultramodern_preempt_pending, 1u, __ATOMIC_RELEASE);
}

bool do_send(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, bool jam, bool block);

void dequeue_external_messages(RDRAM_ARG1) {
    QueuedMessage to_send;
    std::vector<QueuedMessage> requeued_messages{};
    while (external_messages.try_dequeue(to_send)) {
        if (!do_send(PASS_RDRAM to_send.mq, to_send.mesg, to_send.jam, false)) {
            DBG_COUNT(1);
            if (to_send.requeue_if_blocked) requeued_messages.push_back(to_send);
        }
    }
    for (QueuedMessage& cur_mesg : requeued_messages) {
        external_messages.enqueue(cur_mesg);
    }
}


// libultra's OS_IM_ALL: every interrupt enabled, the state a thread starts in.
constexpr uint32_t kIntMaskAll = 0x003FFF01u;
thread_local uint32_t int_mask = kIntMaskAll;

uint32_t ultramodern::set_int_mask(uint32_t mask) {
    uint32_t old = int_mask;
    int_mask = mask;
    return old;
}

extern "C" void ultramodern_preempt(uint8_t* rdram) {
    // Only a game thread with a context can switch; the start-up code and
    // host threads that run recompiled code pass through here too. A thread
    // that masked every interrupt is in a critical section: it waits.
    if (!ultramodern::is_game_thread() || ultramodern::this_thread() == NULLPTR || (int_mask & 0x003FFF00u) == 0) {
        return;
    }
    __atomic_store_n(&ultramodern_preempt_pending, 0u, __ATOMIC_ACQ_REL);
    dequeue_external_messages(PASS_RDRAM1);
    ultramodern::check_running_queue(PASS_RDRAM1);
}

void ultramodern::wait_for_external_message(RDRAM_ARG1) {
    if (ultramodern::det::enabled()) {
        ultramodern::det::idle(PASS_RDRAM1);
        return;
    }
    QueuedMessage to_send;
    DBG_COUNT(5);
    external_messages.wait_dequeue(to_send);
    if (!do_send(PASS_RDRAM to_send.mq, to_send.mesg, to_send.jam, false)) {
        DBG_COUNT(1);
        if (to_send.requeue_if_blocked) external_messages.enqueue(to_send);
    }
}

void ultramodern::wait_for_external_message_timed(RDRAM_ARG u32 millis) {
    if (ultramodern::det::enabled()) {
        ultramodern::det::idle(PASS_RDRAM1);
        return;
    }
    QueuedMessage to_send;
    if (external_messages.wait_dequeue_timed(to_send, std::chrono::milliseconds{millis})) {
        if (!do_send(PASS_RDRAM to_send.mq, to_send.mesg, to_send.jam, false) && to_send.requeue_if_blocked) {
            external_messages.enqueue(to_send);
        }
    }
}

extern "C" void osCreateMesgQueue(RDRAM_ARG PTR(OSMesgQueue) mq_, PTR(OSMesg) msg, s32 count) {
    OSMesgQueue *mq = TO_PTR(OSMesgQueue, mq_);
    mq->blocked_on_recv = NULLPTR;
    mq->blocked_on_send = NULLPTR;
    mq->msgCount = count;
    mq->msg = msg;
    mq->validCount = 0;
    mq->first = 0;
}

s32 MQ_GET_COUNT(OSMesgQueue *mq) {
    return mq->validCount;
}

s32 MQ_IS_EMPTY(OSMesgQueue *mq) {
    return mq->validCount == 0;
}

s32 MQ_IS_FULL(OSMesgQueue* mq) {
    return MQ_GET_COUNT(mq) >= mq->msgCount;
}

bool do_send(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, bool jam, bool block) {
    OSMesgQueue* mq = TO_PTR(OSMesgQueue, mq_);
    if (!block) {
        // If non-blocking, fail if the queue is full.
        if (MQ_IS_FULL(mq)) {
            return false;
        }
    }
    else {
        // Otherwise, yield this thread until the queue has room.
        while (MQ_IS_FULL(mq)) {
            DBG_COUNT(7);
            debug_printf("[Message Queue] Thread %d is blocked on send\n", TO_PTR(OSThread, ultramodern::this_thread())->id);
            ultramodern::thread_queue_insert(PASS_RDRAM GET_MEMBER(OSMesgQueue, mq_, blocked_on_send), ultramodern::this_thread());
            ultramodern::run_next_thread_and_wait(PASS_RDRAM1);
        }
    }
    
    if (jam) {
        // Jams insert at the head of the message queue's buffer.
        mq->first = (mq->first + mq->msgCount - 1) % mq->msgCount;
        TO_PTR(OSMesg, mq->msg)[mq->first] = msg;
        mq->validCount++;
    }
    else {
        // Sends insert at the tail of the message queue's buffer.
        s32 last = (mq->first + mq->validCount) % mq->msgCount;
        TO_PTR(OSMesg, mq->msg)[last] = msg;
        mq->validCount++;
    }

    // If any threads were blocked on receiving from this message queue, pop the first one and schedule it.
    PTR(PTR(OSThread)) blocked_queue = GET_MEMBER(OSMesgQueue, mq_, blocked_on_recv);
    if (!ultramodern::thread_queue_empty(PASS_RDRAM blocked_queue)) {
        ultramodern::schedule_running_thread(PASS_RDRAM ultramodern::thread_queue_pop(PASS_RDRAM blocked_queue));
    }
    
    return true;
}

bool do_recv(RDRAM_ARG PTR(OSMesgQueue) mq_, PTR(OSMesg) msg_, bool block) {
    OSMesgQueue* mq = TO_PTR(OSMesgQueue, mq_);
    if (!block) {
        // If non-blocking, fail if the queue is empty
        if (MQ_IS_EMPTY(mq)) {
            return false;
        }
    } else {
        // Otherwise, yield this thread in a loop until the queue is no longer full
        while (MQ_IS_EMPTY(mq)) {
            DBG_COUNT(6);
            debug_printf("[Message Queue] Thread %d is blocked on receive\n", TO_PTR(OSThread, ultramodern::this_thread())->id);
            ultramodern::thread_queue_insert(PASS_RDRAM GET_MEMBER(OSMesgQueue, mq_, blocked_on_recv), ultramodern::this_thread());
            ultramodern::run_next_thread_and_wait(PASS_RDRAM1);
        }
    }

    if (msg_ != NULLPTR) {
        *TO_PTR(OSMesg, msg_) = TO_PTR(OSMesg, mq->msg)[mq->first];
    }
    
    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;

    // If any threads were blocked on sending to this message queue, pop the first one and schedule it.
    PTR(PTR(OSThread)) blocked_queue = GET_MEMBER(OSMesgQueue, mq_, blocked_on_send);
    if (!ultramodern::thread_queue_empty(PASS_RDRAM blocked_queue)) {
        ultramodern::schedule_running_thread(PASS_RDRAM ultramodern::thread_queue_pop(PASS_RDRAM blocked_queue));
    }

    return true;
}

extern "C" s32 osSendMesg(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, s32 flags) {
    OSMesgQueue *mq = TO_PTR(OSMesgQueue, mq_);
    bool jam = false;
    
    // Don't directly send to the message queue if this isn't a game thread to avoid contention.
    if (!ultramodern::is_game_thread()) {
        ultramodern::enqueue_external_message(mq_, msg, jam, false);
        return 0;
    }
    
    // Handle any messages that have been received from an external thread.
    dequeue_external_messages(PASS_RDRAM1);

    // Try to send the message.
    bool sent = do_send(PASS_RDRAM mq_, msg, jam, flags == OS_MESG_BLOCK);
    
    // Check the queue to see if this thread should swap execution to another.
    ultramodern::check_running_queue(PASS_RDRAM1);

    return sent ? 0 : -1;
}

extern "C" s32 osJamMesg(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, s32 flags) {
    OSMesgQueue *mq = TO_PTR(OSMesgQueue, mq_);
    bool jam = true;
    
    // Don't directly send to the message queue if this isn't a game thread to avoid contention.
    if (!ultramodern::is_game_thread()) {
        ultramodern::enqueue_external_message(mq_, msg, jam, false);
        return 0;
    }
    
    // Handle any messages that have been received from an external thread.
    dequeue_external_messages(PASS_RDRAM1);

    // Try to send the message.
    bool sent = do_send(PASS_RDRAM mq_, msg, jam, flags == OS_MESG_BLOCK);
    
    // Check the queue to see if this thread should swap execution to another.
    ultramodern::check_running_queue(PASS_RDRAM1);

    return sent ? 0 : -1;
}

extern "C" s32 osRecvMesg(RDRAM_ARG PTR(OSMesgQueue) mq_, PTR(OSMesg) msg_, s32 flags) {
    OSMesgQueue *mq = TO_PTR(OSMesgQueue, mq_);
    
    assert(ultramodern::is_game_thread() && "RecvMesg not allowed outside of game threads.");
    
    // Handle any messages that have been received from an external thread.
    dequeue_external_messages(PASS_RDRAM1);

    // Try to receive a message.
    bool received = do_recv(PASS_RDRAM mq_, msg_, flags == OS_MESG_BLOCK);
    
    // Check the queue to see if this thread should swap execution to another.
    ultramodern::check_running_queue(PASS_RDRAM1);

    return received ? 0 : -1;
}
