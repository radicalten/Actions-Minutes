// Stand-ins for the mod system when it is compiled out (RECOMP_NO_MODS):
// platforms with no dynamic loading, page protection or spare memory, such
// as the 3DS. The recompiled patches still call recomp_trigger_event for
// every declared event, so that has to exist and do nothing.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "librecomp/mods.hpp"
#include "recomp.h"

namespace recomp::mods {
    void initialize_mods() {}
    void scan_mods() {}
    void close_mods() {}
    void register_config_exports() {}
    void register_hook_exports() {}
    void set_latest_game_mode_id(const std::string&) {}
    std::vector<ModDetails> get_all_mod_details(const std::string&) { return {}; }
    bool is_mod_enabled(const std::string&) { return false; }
    bool is_mod_auto_enabled(const std::string&) { return false; }
}

extern "C" {
    uint32_t builtin_base_event_index = 0;

    void recomp_trigger_event(uint8_t* rdram, recomp_context* ctx, uint32_t event_index) {
        (void)rdram; (void)ctx; (void)event_index;
    }
}
