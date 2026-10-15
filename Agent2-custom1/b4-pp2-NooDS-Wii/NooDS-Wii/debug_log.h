#pragma once

// Call once, after libfat/SD card is mounted, before any DebugLog*() call.
// Creates/truncates both log files.
void DebugLog_Init();

// High-frequency JIT/CPU trace -> sd:/noods/debug_trace.log
// Internally budget-capped so it can't flood the SD card or slow things
// down forever; stops silently once the budget is spent.
void DebugLog(const char* fmt, ...);

// Low-frequency once-per-second style status -> sd:/noods/debug_status.log
// Separate budget so it keeps recording long after debug_trace.log fills up.
void DebugLogStatus(const char* fmt, ...);
