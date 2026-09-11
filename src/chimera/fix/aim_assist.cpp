// SPDX-License-Identifier: GPL-3.0-only

#include "aim_assist.hpp"
#include "../chimera.hpp"
#include "../signature/hook.hpp"
#include "../signature/signature.hpp"
#include "../output/output.hpp"

extern "C" {
    std::uint8_t *using_analog_movement = nullptr;
    std::byte *not_using_analog_movement_jmp = nullptr;
    std::byte *yes_using_analog_movement_jmp = nullptr;
    float aim_assist_strength = 1.0F;

    void on_aim_assist();
    void on_aim_assist_strength();
}

namespace Chimera {
    bool aim_assist_command(int argc, const char **argv) {
        static auto &active = **reinterpret_cast<char **>(get_chimera().get_signature("aim_assist_enabled_sig").data() + 1);

        if(argc == 1) {
            const char *value = argv[0];
            bool strength_value = false;
            for(const char *p = value; *p != '\0'; p++) {
                if(*p == '.' || *p == 'e' || *p == 'E') {
                    strength_value = true;
                    break;
                }
            }

            if(strength_value) {
                float strength = std::stof(value);
                if(strength < 0.0F) {
                    strength = 0.0F;
                }
                else if(strength > 2.0F) {
                    strength = 2.0F;
                }
                aim_assist_strength = strength;
            }
            else {
                active = STR_TO_BOOL(value);
            }
        }

        console_output(BOOL_TO_STR(active));
        return true;
    }

    void set_up_aim_assist_fix() noexcept {
        auto *should_use_aim_assist_addr = get_chimera().get_signature("should_use_aim_assist_sig").data();
        using_analog_movement = *reinterpret_cast<std::uint8_t **>(should_use_aim_assist_addr + 2);
        static const SigByte nop[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
        write_code_s(should_use_aim_assist_addr, nop);

        auto *aim_assist = get_chimera().get_signature("aim_assist_sig").data();
        not_using_analog_movement_jmp = aim_assist + 0x2 + 0x6 + 0x36E;
        yes_using_analog_movement_jmp = aim_assist + 0x2 + 0x6;

        static Hook hook;
        const void *old_fn;
        write_function_override(aim_assist, hook, reinterpret_cast<const void *>(on_aim_assist), &old_fn);

        // Native aim assist writes its final correction at this point. Scale only
        // that correction so the native target selection, visibility checks,
        // team filtering, angle calculation, and 30-tick timing remain untouched.
        static Hook strength_hook;
        write_jmp_call(aim_assist + 0x357, strength_hook, reinterpret_cast<const void *>(on_aim_assist_strength), nullptr);
    }
}
