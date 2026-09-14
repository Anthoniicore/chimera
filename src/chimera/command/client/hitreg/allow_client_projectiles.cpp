// SPDX-License-Identifier: GPL-3.0-only

#include "../../command.hpp"
#include "../../../output/output.hpp"

#include <cstdint>
#include <cstring>

namespace Chimera {
    // Halo CE 1.10-class HS global: allow_client_side_weapon_projectiles
    static constexpr std::uintptr_t ADDR_ALLOW_CLIENT_SIDE_WEAPON_PROJECTILES = 0x624AA4u;

    static std::uint8_t *allow_client_projectiles() noexcept {
        return reinterpret_cast<std::uint8_t *>(ADDR_ALLOW_CLIENT_SIDE_WEAPON_PROJECTILES);
    }

    bool hitreg_allow_client_projectiles_command(int argc, const char **argv) {
        std::uint8_t *flag = allow_client_projectiles();

        if(argc == 0) {
            console_output("%s", *flag ? "true" : "false");
            return true;
        }

        bool enable = false;
        if(std::strcmp(argv[0], "false") == 0 || std::strcmp(argv[0], "0") == 0 || std::strcmp(argv[0], "off") == 0) {
            enable = false;
        }
        else if(std::strcmp(argv[0], "true") == 0 || std::strcmp(argv[0], "1") == 0 || std::strcmp(argv[0], "on") == 0) {
            enable = true;
        }
        else {
            console_error("Expected true/false");
            return false;
        }

        *flag = enable ? 1 : 0;
        console_output("allow_client_side_weapon_projectiles = %s", enable ? "true" : "false");
        return true;
    }
}
