// SPDX-License-Identifier: GPL-3.0-only

#include "../../command.hpp"
#include "../../../signature/hook.hpp"
#include "../../../signature/signature.hpp"
#include "../../../signature/hac/codefinder.h"
#include "../../../chimera.hpp"
#include "../../../output/output.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <windows.h>

extern "C" {
    float magnetism_adhesion_multiplier = 1.0f;
    void *magnetism_adhesion_return = nullptr;
    void magnetism_adhesion_hook();
}

namespace Chimera {
    static Hook adhesion_hook;
    static bool hook_installed = false;
    static constexpr float DEFAULT_ADHESION = 1.5f;
    static constexpr float MIN_ADHESION = 1.0f;
    static constexpr float MAX_ADHESION = 3.0f;

    static void install_hook() noexcept {
        if(hook_installed) {
            return;
        }

        // Unique magnetism look-blend epilogue (~0x47428B on CE 1.10)
        static const SigByte pattern[] = {
            0xD9, 0x5C, 0x24, 0x34, 0xD8, 0x4C, 0x24, 0x60, 0xD9, 0x44, 0x24, 0x28,
            0xD8, 0x4C, 0x24, 0x38, 0xDE, 0xC1, 0xD9, 0x5C, 0x24, 0x38, 0xD9, 0x44, 0x24, 0x64
        };
        auto *addr = reinterpret_cast<std::byte *>(FindCode(GetModuleHandle(nullptr), pattern, sizeof(pattern) / sizeof(*pattern), 0));
        if(!addr) {
            console_error("magnetism_adhesion: signature not found on this build");
            return;
        }

        // 22-byte epilogue; Halo continues at addr+22 (flds [esp+0x64]).
        magnetism_adhesion_return = addr + 22;
        const void *ignored = nullptr;
        write_function_override(addr, adhesion_hook, reinterpret_cast<const void *>(magnetism_adhesion_hook), &ignored);
        hook_installed = true;
    }

    static void remove_hook() noexcept {
        if(!hook_installed) {
            return;
        }
        adhesion_hook.rollback();
        hook_installed = false;
        magnetism_adhesion_multiplier = 1.0f;
    }

    bool magnetism_adhesion_command(int argc, const char **argv) {
        if(argc == 0) {
            if(hook_installed && magnetism_adhesion_multiplier != 1.0f) {
                console_output("true (%.2fx adhesion)", static_cast<double>(magnetism_adhesion_multiplier));
            }
            else {
                console_output("false");
            }
            return true;
        }

        if(std::strcmp(argv[0], "false") == 0 || std::strcmp(argv[0], "0") == 0 || std::strcmp(argv[0], "off") == 0) {
            remove_hook();
            console_output("magnetism adhesion disabled (stock stickiness)");
            return true;
        }

        float mult = DEFAULT_ADHESION;
        if(std::strcmp(argv[0], "true") == 0 || std::strcmp(argv[0], "1") == 0 || std::strcmp(argv[0], "on") == 0) {
            mult = DEFAULT_ADHESION;
        }
        else {
            mult = static_cast<float>(std::atof(argv[0]));
            if(!std::isfinite(mult)) {
                console_error("Expected a multiplier (e.g. 1.5) or true/false");
                return false;
            }
        }

        if(mult < MIN_ADHESION) {
            console_output("Clamped to minimum %.1f", static_cast<double>(MIN_ADHESION));
            mult = MIN_ADHESION;
        }
        if(mult > MAX_ADHESION) {
            console_output("Clamped to maximum %.1f", static_cast<double>(MAX_ADHESION));
            mult = MAX_ADHESION;
        }

        if(mult == 1.0f) {
            remove_hook();
            console_output("magnetism adhesion 1.00x (stock)");
            return true;
        }

        magnetism_adhesion_multiplier = mult;
        install_hook();
        if(!hook_installed) {
            return false;
        }
        console_output("magnetism adhesion %.2fx (same activation cone, stronger stick)", static_cast<double>(mult));
        return true;
    }
}
