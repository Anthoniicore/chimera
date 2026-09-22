// SPDX-License-Identifier: GPL-3.0-only

#include "../../command.hpp"
#include "../../../halo_data/tag.hpp"
#include "../../../halo_data/tag_class.hpp"
#include "../../../event/map_load.hpp"
#include "../../../output/output.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace Chimera {
    // Port of aLTis lightmap-specular Lua demo, as a native module.
    // Enables "lightmap is specular" on shader_environment tags and boosts
    // specular power/color so surfaces look more metallic and shiny.
    // NOTE: This feature still breaks with dynamic lights (flashlight, muzzle).
    // We optionally disable specular on light tags to reduce the worst cases.

    static bool enabled = false;
    static bool events_registered = false;
    static float intensity_scale = 2.0f; // multiplier on top of per-material baselines
    static bool disable_dynamic_light_specular = true;
    static bool force_white_specular = true;

    // Baseline specular power by material type (Halo physics material index).
    // Higher than the original Lua script so metals/glass look very shiny.
    static float material_brightness(std::uint16_t material) noexcept {
        switch(material) {
            case 0:  return 0.25f; // Dirt
            case 1:  return 0.25f; // Sand
            case 2:  return 0.40f; // Stone
            case 3:  return 1.50f; // Snow
            case 4:  return 0.35f; // Wood
            case 5:  return 3.00f; // Metal (hollow)
            case 6:  return 3.00f; // Metal (thin)
            case 7:  return 3.50f; // Metal (thick)
            case 8:  return 0.40f; // Rubber
            case 9:  return 2.50f; // Glass
            case 27: return 0.50f; // Plastic
            case 31: return 2.00f; // Ice
            default: return 0.50f;
        }
    }

    static void write_color3(std::byte *base, float r, float g, float b) noexcept {
        *reinterpret_cast<float *>(base + 0) = r;
        *reinterpret_cast<float *>(base + 4) = g;
        *reinterpret_cast<float *>(base + 8) = b;
    }

    static bool is_color_black(const std::byte *base) noexcept {
        return *reinterpret_cast<const float *>(base + 0) == 0.0f
            && *reinterpret_cast<const float *>(base + 4) == 0.0f
            && *reinterpret_cast<const float *>(base + 8) == 0.0f;
    }

    static void set_bit(std::byte *byte_ptr, unsigned bit, bool value) noexcept {
        auto &b = *reinterpret_cast<std::uint8_t *>(byte_ptr);
        if(value) {
            b = static_cast<std::uint8_t>(b | (1u << bit));
        }
        else {
            b = static_cast<std::uint8_t>(b & ~(1u << bit));
        }
    }

    static void apply_lightmap_specular(bool enable) noexcept {
        auto &header = get_tag_data_header();
        auto *tags = header.tag_array;
        auto count = header.tag_count;

        for(std::uint32_t i = 0; i < count; i++) {
            auto &tag = tags[i];
            if(tag.data == nullptr) {
                continue;
            }

            if(tag.primary_class == TagClassInt::TAG_CLASS_SHADER_ENVIRONMENT) {
                auto *data = tag.data;

                // Flags at +0x27C: bit 2 = "lightmap is specular"
                set_bit(data + 0x27C, 2, enable);

                if(!enable) {
                    continue;
                }

                // Only boost if the shader has a base map / lightmap reference
                // (Lua checked tag_data+0x128+0xC != 0xFFFFFFFF).
                auto lightmap_id = *reinterpret_cast<std::uint32_t *>(data + 0x128 + 0xC);
                if(lightmap_id == 0xFFFFFFFFu) {
                    continue;
                }

                auto material = *reinterpret_cast<std::uint16_t *>(data + 0x22);
                float target = material_brightness(material) * intensity_scale;
                if(target < 0.0f) {
                    target = 0.0f;
                }
                // Specular power / brightness at +0x290
                auto *power = reinterpret_cast<float *>(data + 0x290);
                if(*power < target) {
                    *power = target;
                }

                // Specular colors at +0x2A8 and +0x2B4
                if(force_white_specular) {
                    write_color3(data + 0x2A8, 1.0f, 1.0f, 1.0f);
                    write_color3(data + 0x2B4, 1.0f, 1.0f, 1.0f);
                }
                else if(is_color_black(data + 0x2A8)) {
                    write_color3(data + 0x2A8, 1.0f, 1.0f, 1.0f);
                }
            }
            else if(disable_dynamic_light_specular && tag.primary_class == TagClassInt::TAG_CLASS_LIGHT) {
                // Flags at start of light tag data: bit 1 disables specular contribution
                // when set (matches the Lua write_bit(tag_data, 1, 1)).
                if(enable) {
                    set_bit(tag.data, 1, true);
                }
            }
        }
    }

    static void on_map_load_lightmap_specular() noexcept {
        if(enabled) {
            apply_lightmap_specular(true);
        }
    }

    static void ensure_events() noexcept {
        if(!events_registered) {
            add_map_load_event(on_map_load_lightmap_specular);
            events_registered = true;
        }
    }

    static void maybe_remove_events() noexcept {
        if(events_registered && !enabled) {
            remove_map_load_event(on_map_load_lightmap_specular);
            events_registered = false;
        }
    }

    bool lightmap_specular_command(int argc, const char **argv) {
        if(argc == 0) {
            if(enabled) {
                console_output("chimera_lightmap_specular: on (scale %.2f)", static_cast<double>(intensity_scale));
            }
            else {
                console_output("chimera_lightmap_specular: off");
            }
            return true;
        }

        if(std::strcmp(argv[0], "false") == 0 || std::strcmp(argv[0], "0") == 0 || std::strcmp(argv[0], "off") == 0) {
            if(enabled) {
                apply_lightmap_specular(false);
                enabled = false;
                maybe_remove_events();
            }
            console_output("chimera_lightmap_specular: off");
            return true;
        }

        if(std::strcmp(argv[0], "true") == 0 || std::strcmp(argv[0], "1") == 0 || std::strcmp(argv[0], "on") == 0) {
            enabled = true;
            ensure_events();
            apply_lightmap_specular(true);
            console_output("chimera_lightmap_specular: on (scale %.2f)", static_cast<double>(intensity_scale));
            return true;
        }

        // Optional: numeric intensity scale (e.g. 1.0 = script-like, 2.0 = default boost, 3.0+ = very shiny)
        float scale = static_cast<float>(std::atof(argv[0]));
        if(!(scale > 0.0f) || scale > 10.0f || !std::isfinite(scale)) {
            console_error("Value must be on/off/true/false or a scale between 0.1 and 10.0");
            return false;
        }

        intensity_scale = scale;
        enabled = true;
        ensure_events();
        apply_lightmap_specular(true);
        console_output("chimera_lightmap_specular: on (scale %.2f)", static_cast<double>(intensity_scale));
        return true;
    }
}
