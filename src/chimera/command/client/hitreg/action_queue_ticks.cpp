// SPDX-License-Identifier: GPL-3.0-only

#include "../../command.hpp"
#include "../../../output/output.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace Chimera {
    // Halo CE 1.10-class HS global value addresses (from RE of haloce.exe)
    static constexpr std::uintptr_t ADDR_SV_CLIENT_ACTION_QUEUE_TICK_LIMIT = 0x623D78u;
    static constexpr std::uintptr_t ADDR_CL_REMOTE_ACTION_QUEUE_TICK_LIMIT = 0x623D80u;

    static constexpr std::int32_t TICK_LIMIT_MIN = 1;
    static constexpr std::int32_t TICK_LIMIT_MAX = 128;

    static std::int32_t *sv_tick_limit() noexcept {
        return reinterpret_cast<std::int32_t *>(ADDR_SV_CLIENT_ACTION_QUEUE_TICK_LIMIT);
    }

    static std::int32_t *cl_tick_limit() noexcept {
        return reinterpret_cast<std::int32_t *>(ADDR_CL_REMOTE_ACTION_QUEUE_TICK_LIMIT);
    }

    bool hitreg_action_queue_ticks_command(int argc, const char **argv) {
        std::int32_t *sv = sv_tick_limit();
        std::int32_t *cl = cl_tick_limit();

        if(argc == 0) {
            console_output("sv_client_action_queue_tick_limit=%d  cl_remote_player_action_queue_tick_limit=%d", *sv, *cl);
            return true;
        }

        if(argc == 1) {
            console_output("Usage: chimera_hitreg_action_queue_ticks <sv|cl> <value>");
            console_output("  value range: 1-128 (stock is often 6; higher keeps more actions under lag)");
            return false;
        }

        const char *which = argv[0];
        std::int32_t value = static_cast<std::int32_t>(std::atoi(argv[1]));

        if(value < TICK_LIMIT_MIN) {
            value = TICK_LIMIT_MIN;
            console_output("Clamped to minimum 1");
        }
        if(value > TICK_LIMIT_MAX) {
            value = TICK_LIMIT_MAX;
            console_output("Clamped to maximum 128");
        }

        if(std::strcmp(which, "sv") == 0 || std::strcmp(which, "server") == 0) {
            *sv = value;
            console_output("sv_client_action_queue_tick_limit set to %d", value);
            return true;
        }
        if(std::strcmp(which, "cl") == 0 || std::strcmp(which, "client") == 0) {
            *cl = value;
            console_output("cl_remote_player_action_queue_tick_limit set to %d", value);
            return true;
        }

        console_error("First argument must be 'sv' or 'cl'");
        return false;
    }
}
