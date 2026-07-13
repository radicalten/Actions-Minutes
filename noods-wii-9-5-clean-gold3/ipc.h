//ipc.h (optimized)
#pragma once

#include <cstdint>
#include <cstdio>
#include <array>

class Core;

// Fixed-size circular FIFO for IPC - avoids std::deque overhead
// DS IPC FIFO is exactly 16 words deep
class IpcFifo {
public:
    static constexpr uint32_t CAPACITY = 16;

    void clear() {
        head = tail = count = 0;
    }

    bool empty() const { return count == 0; }
    bool full()  const { return count == CAPACITY; }
    uint32_t size() const { return count; }

    void push_back(uint32_t val) {
        buf[tail] = val;
        tail = (tail + 1) & (CAPACITY - 1);
        ++count;
    }

    uint32_t front() const { return buf[head]; }

    void pop_front() {
        head = (head + 1) & (CAPACITY - 1);
        --count;
    }

    // Direct index access (for save state)
    uint32_t operator[](uint32_t i) const {
        return buf[(head + i) & (CAPACITY - 1)];
    }

private:
    uint32_t buf[CAPACITY] = {};
    uint32_t head = 0;
    uint32_t tail = 0;
    uint32_t count = 0;
};

class Ipc {
public:
    Ipc(Core *core): core(core) {}
    void saveState(FILE *file);
    void loadState(FILE *file);

    uint16_t readIpcSync(bool arm7)    { return ipcSync[arm7]; }
    uint16_t readIpcFifoCnt(bool arm7) { return ipcFifoCnt[arm7]; }
    uint32_t readIpcFifoRecv(bool arm7);

    void writeIpcSync(bool arm7, uint16_t mask, uint16_t value);
    void writeIpcFifoCnt(bool arm7, uint16_t mask, uint16_t value);
    void writeIpcFifoSend(bool arm7, uint32_t mask, uint32_t value);

private:
    Core *core;

    // Use fast circular FIFOs instead of std::deque
    IpcFifo fifos[2];

    uint16_t ipcSync[2]    = {};
    uint16_t ipcFifoCnt[2] = { 0x0101, 0x0101 };
    uint32_t ipcFifoRecv[2] = {};
};
