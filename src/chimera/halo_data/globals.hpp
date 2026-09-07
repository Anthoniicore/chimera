// SPDX-License-Identifier: GPL-3.0-only

#ifndef CHIMERA_GLOBALS_HPP
#define CHIMERA_GLOBALS_HPP

#include "pad.hpp"

namespace Chimera {
    struct GameStateHeader {
        unsigned long allocation_size_checksum;
        char map_name[256];
        char build_number[32];
        short player_count;
        short difficulty;
        unsigned long cache_file_checksum;

        PAD(0x1C);

        unsigned long checksum;
    };
    static_assert(sizeof(GameStateHeader) == 0x14C);

    struct GameStateGlobals {
        void *base_address;
        long cpu_allocation_size;
        long gpu_allocation_size;
        long allocation_size_checksum;
        bool locked;
        bool saved_game_valid;

        long revert_time;

        GameStateHeader *header;
    };
    static_assert(sizeof(GameStateGlobals) == 0x1C);

    enum ScriptingGlobalType {
        SCRIPTING_GLOBAL_NOT_FOUND = 0,
        SCRIPTING_GLOBAL_BOOLEAN,
        SCRIPTING_GLOBAL_REAL,
        SCRIPTING_GLOBAL_SHORT,
        SCRIPTING_GLOBAL_LONG,
        SCRIPTING_GLOBAL_UNIMPLEMENTED
    };

    union ScriptingGlobalValue {
        bool boolean;
        float real;
        short short_int;
        long long_int;
    };

    struct ScriptingGlobal {
        ScriptingGlobalType type = SCRIPTING_GLOBAL_NOT_FOUND;
        ScriptingGlobalValue value;
    };

    ScriptingGlobal read_global(const char *global_name) noexcept;

    bool set_global(const char *global_name, ScriptingGlobalValue value) noexcept;
}

#endif
