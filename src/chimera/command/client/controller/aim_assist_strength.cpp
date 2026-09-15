// SPDX-License-Identifier: GPL-3.0-only

#include "../../command.hpp"
#include "../../../halo_data/tag.hpp"
#include "../../../halo_data/tag_class.hpp"
#include "../../../event/map_load.hpp"
#include "../../../output/output.hpp"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace Chimera {
    // Weapon tag aim-assist block (HEK / invader / Chimera map_loading offsets).
    // Confirmed: autoaim_angle @ 0x3E4, deviation_angle @ 0x3F4 in map_loading.cpp.
    static constexpr std::size_t OFF_AUTOAIM_ANGLE     = 0x3E4; // radians
    static constexpr std::size_t OFF_AUTOAIM_RANGE     = 0x3E8; // world units
    static constexpr std::size_t OFF_MAGNETISM_ANGLE   = 0x3EC; // radians (controller stickiness)
    static constexpr std::size_t OFF_MAGNETISM_RANGE   = 0x3F0; // world units
    // 0x3F4 = deviation_angle — leave alone (vehicle aim cone)

    // player_magnetism HS global (CE 1.10-class); 1 = controller magnetism on
    static constexpr std::uintptr_t ADDR_PLAYER_MAGNETISM = 0x68CD81u;

    static constexpr float DEFAULT_STRENGTH = 1.5f;
    static constexpr float MIN_STRENGTH = 1.0f;
    static constexpr float MAX_STRENGTH = 2.5f;

    struct SavedWeaponAA {
        std::byte *data;
        float autoaim_angle;
        float autoaim_range;
        float magnetism_angle;
        float magnetism_range;
    };

    static std::vector<SavedWeaponAA> saved;
    static bool enabled = false;
    static bool map_load_hooked = false;
    static float strength = DEFAULT_STRENGTH;

    static float *as_float(std::byte *data, std::size_t off) noexcept {
        return reinterpret_cast<float *>(data + off);
    }

    static void restore() noexcept {
        for(const auto &s : saved) {
            if(!s.data) {
                continue;
            }
            *as_float(s.data, OFF_AUTOAIM_ANGLE) = s.autoaim_angle;
            *as_float(s.data, OFF_AUTOAIM_RANGE) = s.autoaim_range;
            *as_float(s.data, OFF_MAGNETISM_ANGLE) = s.magnetism_angle;
            *as_float(s.data, OFF_MAGNETISM_RANGE) = s.magnetism_range;
        }
        saved.clear();
    }

    static bool plausible_angle(float r) noexcept {
        // 0 < angle < ~90 degrees in radians
        return std::isfinite(r) && r > 0.0f && r < 1.6f;
    }

    static bool plausible_range(float r) noexcept {
        return std::isfinite(r) && r > 0.0f && r < 500.0f;
    }

    static void apply() noexcept {
        restore();

        auto &header = get_tag_data_header();
        Tag *tags = header.tag_array;
        auto count = header.tag_count;
        if(!tags || count == 0) {
            return;
        }

        for(std::uint32_t i = 0; i < count; i++) {
            Tag &tag = tags[i];
            if(tag.primary_class != TagClassInt::TAG_CLASS_WEAPON) {
                continue;
            }
            if(!tag.data) {
                continue;
            }

            float aa = *as_float(tag.data, OFF_AUTOAIM_ANGLE);
            float ar = *as_float(tag.data, OFF_AUTOAIM_RANGE);
            float ma = *as_float(tag.data, OFF_MAGNETISM_ANGLE);
            float mr = *as_float(tag.data, OFF_MAGNETISM_RANGE);

            // Only patch weapons that already have some aim assist configured
            if(!plausible_angle(aa) && !plausible_angle(ma)) {
                continue;
            }
            if(!plausible_range(ar) && !plausible_range(mr)) {
                continue;
            }

            saved.push_back({ tag.data, aa, ar, ma, mr });

            if(plausible_angle(aa)) {
                *as_float(tag.data, OFF_AUTOAIM_ANGLE) = aa * strength;
            }
            if(plausible_range(ar)) {
                *as_float(tag.data, OFF_AUTOAIM_RANGE) = ar * strength;
            }
            if(plausible_angle(ma)) {
                *as_float(tag.data, OFF_MAGNETISM_ANGLE) = ma * strength;
            }
            if(plausible_range(mr)) {
                *as_float(tag.data, OFF_MAGNETISM_RANGE) = mr * strength;
            }
        }

        // Ensure controller magnetism global is on while strength is active
        *reinterpret_cast<std::uint8_t *>(ADDR_PLAYER_MAGNETISM) = 1;
    }

    static void on_map_load() noexcept {
        if(enabled) {
            apply();
        }
        else {
            saved.clear();
        }
    }

    bool aim_assist_strength_command(int argc, const char **argv) {
        if(argc == 0) {
            if(enabled) {
                console_output("true (%.2fx) - %zu weapons patched", static_cast<double>(strength), saved.size());
            }
            else {
                console_output("false");
            }
            return true;
        }

        if(std::strcmp(argv[0], "false") == 0 || std::strcmp(argv[0], "0") == 0 || std::strcmp(argv[0], "off") == 0) {
            enabled = false;
            restore();
            if(map_load_hooked) {
                remove_map_load_event(on_map_load);
                map_load_hooked = false;
            }
            console_output("aim assist strength disabled (stock weapon values restored)");
            return true;
        }

        float mult = strength;
        if(std::strcmp(argv[0], "true") == 0 || std::strcmp(argv[0], "1") == 0 || std::strcmp(argv[0], "on") == 0) {
            mult = DEFAULT_STRENGTH;
        }
        else {
            mult = static_cast<float>(std::atof(argv[0]));
            if(!std::isfinite(mult)) {
                console_error("Expected a multiplier (e.g. 1.5) or true/false");
                return false;
            }
        }

        if(mult < MIN_STRENGTH) {
            console_output("Clamped to minimum %.1f", static_cast<double>(MIN_STRENGTH));
            mult = MIN_STRENGTH;
        }
        if(mult > MAX_STRENGTH) {
            console_output("Clamped to maximum %.1f (higher is very sticky)", static_cast<double>(MAX_STRENGTH));
            mult = MAX_STRENGTH;
        }

        strength = mult;
        enabled = true;
        apply();

        if(!map_load_hooked) {
            add_map_load_event(on_map_load);
            map_load_hooked = true;
        }

        console_output("aim assist strength %.2fx on %zu weapons (magnetism + autoaim)", static_cast<double>(strength), saved.size());
        return true;
    }
}
