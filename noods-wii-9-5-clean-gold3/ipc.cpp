//ipc.cpp (optimized)
#include "core.h"

#define PPC_LIKELY(x)   __builtin_expect(!!(x), 1)
#define PPC_UNLIKELY(x) __builtin_expect(!!(x), 0)

void Ipc::saveState(FILE *file) {
    fwrite(ipcSync,     2, 2, file);
    fwrite(ipcFifoCnt,  2, 2, file);
    fwrite(ipcFifoRecv, 4, 2, file);

    for (int i = 0; i < 2; i++) {
        uint32_t count = fifos[i].size();
        fwrite(&count, sizeof(count), 1, file);
        for (uint32_t j = 0; j < count; j++) {
            uint32_t v = fifos[i][j];
            fwrite(&v, sizeof(v), 1, file);
        }
    }
}

void Ipc::loadState(FILE *file) {
    fread(ipcSync,     2, 2, file);
    fread(ipcFifoCnt,  2, 2, file);
    fread(ipcFifoRecv, 4, 2, file);

    for (int i = 0; i < 2; i++) {
        fifos[i].clear();
        uint32_t count, value;
        fread(&count, sizeof(count), 1, file);
        // Clamp to capacity for safety
        if (count > IpcFifo::CAPACITY) count = IpcFifo::CAPACITY;
        for (uint32_t j = 0; j < count; j++) {
            fread(&value, sizeof(value), 1, file);
            fifos[i].push_back(value);
        }
    }
}

void Ipc::writeIpcSync(bool arm7, uint16_t mask, uint16_t value) {
    mask &= 0x4F00;
    ipcSync[arm7] = (ipcSync[arm7] & ~mask) | (value & mask);

    if (PPC_UNLIKELY(core->arm7Hle && !arm7))
        return core->hleArm7.ipcSync((ipcSync[0] >> 8) & 0xF);

    // Copy input bits to other CPU's output bits
    // Only bits [11:8] (the output nibble) are mirrored
    uint16_t mirrorMask = (mask >> 8) & 0xF;
    uint16_t mirrorVal  = (((value & mask) >> 8) & 0xF);
    ipcSync[!arm7] = (ipcSync[!arm7] & ~mirrorMask) | mirrorVal;

    // Trigger remote IRQ if both sides have it enabled
    if (PPC_UNLIKELY((value & BIT(13)) && (ipcSync[!arm7] & BIT(14))))
        core->interpreter[!arm7].sendInterrupt(16);
}

void Ipc::writeIpcFifoCnt(bool arm7, uint16_t mask, uint16_t value) {
    // Clear FIFO if clear bit set and FIFO not empty
    if (PPC_UNLIKELY((value & BIT(3)) && !fifos[arm7].empty())) {
        fifos[arm7].clear();
        ipcFifoRecv[!arm7] = 0;

        // Update empty/full status bits atomically
        // arm7 side: set empty (bit0), clear full (bit1)
        ipcFifoCnt[arm7]  = (ipcFifoCnt[arm7]  | BIT(0)) & ~BIT(1);
        // !arm7 side: set recv empty (bit8), clear recv full (bit9)
        ipcFifoCnt[!arm7] = (ipcFifoCnt[!arm7] | BIT(8)) & ~BIT(9);

        // Trigger send FIFO empty IRQ if enabled
        if (ipcFifoCnt[arm7] & BIT(2))
            core->interpreter[arm7].sendInterrupt(17);
    }

    // Trigger send FIFO empty IRQ if enabling and FIFO already empty
    if ((ipcFifoCnt[arm7] & BIT(0)) && !(ipcFifoCnt[arm7] & BIT(2)) && (value & BIT(2)))
        core->interpreter[arm7].sendInterrupt(17);

    // Trigger receive FIFO not-empty IRQ if enabling and FIFO not empty
    if (!(ipcFifoCnt[arm7] & BIT(8)) && !(ipcFifoCnt[arm7] & BIT(10)) && (value & BIT(10)))
        core->interpreter[arm7].sendInterrupt(18);

    // Acknowledge error by clearing error bit if set in value
    ipcFifoCnt[arm7] &= ~(value & BIT(14));

    // Apply mask and write
    mask &= 0x8404;
    ipcFifoCnt[arm7] = (ipcFifoCnt[arm7] & ~mask) | (value & mask);
}

void Ipc::writeIpcFifoSend(bool arm7, uint32_t mask, uint32_t value) {
    if (PPC_LIKELY(ipcFifoCnt[arm7] & BIT(15))) {
        if (PPC_LIKELY(!fifos[arm7].full())) {
            uint32_t masked = value & mask;
            LOG_INFO("ARM%d sending value through IPC FIFO: 0x%X\n", arm7 ? 7 : 9, masked);

            if (PPC_UNLIKELY(core->arm7Hle && !arm7))
                return core->hleArm7.ipcFifo(masked);

            fifos[arm7].push_back(masked);
            uint32_t newSize = fifos[arm7].size();

            if (newSize == 1) {
                // FIFO was empty, now has one entry: clear empty bits
                ipcFifoCnt[arm7]  &= ~BIT(0);
                ipcFifoCnt[!arm7] &= ~BIT(8);

                // Trigger receive not-empty IRQ if enabled
                if (PPC_UNLIKELY(ipcFifoCnt[!arm7] & BIT(10)))
                    core->interpreter[!arm7].sendInterrupt(18);
            } else if (newSize == IpcFifo::CAPACITY) {
                // FIFO now full: set full bits
                ipcFifoCnt[arm7]  |= BIT(1);
                ipcFifoCnt[!arm7] |= BIT(9);
            }
        } else {
            // FIFO full: set send-full error
            ipcFifoCnt[arm7] |= BIT(14);
        }
    }
}

uint32_t Ipc::readIpcFifoRecv(bool arm7) {
    IpcFifo &srcFifo = fifos[!arm7];

    if (PPC_LIKELY(!srcFifo.empty())) {
        // Peek at front without removing
        ipcFifoRecv[arm7] = srcFifo.front();

        if (PPC_LIKELY(ipcFifoCnt[arm7] & BIT(15))) {
            srcFifo.pop_front();
            uint32_t remaining = srcFifo.size();

            if (remaining == 0) {
                // FIFO now empty: set empty bits
                ipcFifoCnt[arm7]  |= BIT(8);
                ipcFifoCnt[!arm7] |= BIT(0);

                // Trigger send FIFO empty IRQ if enabled
                if (PPC_UNLIKELY(ipcFifoCnt[!arm7] & BIT(2)))
                    core->interpreter[!arm7].sendInterrupt(17);
            } else if (remaining == IpcFifo::CAPACITY - 1) {
                // Was full (16), now 15: clear full bits
                ipcFifoCnt[arm7]  &= ~BIT(9);
                ipcFifoCnt[!arm7] &= ~BIT(1);
            }
        }
    } else {
        // FIFO empty: set receive-empty error
        ipcFifoCnt[arm7] |= BIT(14);
    }

    return ipcFifoRecv[arm7];
}
