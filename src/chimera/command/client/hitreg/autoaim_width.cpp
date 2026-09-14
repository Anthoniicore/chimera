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
    // Biped tag data + 0x458 = autoaim_width (world units). Confirmed by Devieth + c20.
    static constexpr std::size_t AUTOAIM_WIDTH_OFFSET = 0x458;
    static constexpr float DEFAULT_TARGET_WIDTH = 0.05f;

    struct SavedWidth {
        std::byte *tag_data;
        float original;
    };

    static std::vector<SavedWidth> saved_widths;
    static bool currently_applied = false;
    static float current_target = DEFAULT_TARGET_WIDTH;
    static bool map_load_hooked = false;
    static bool enabled = false;

    static void restore_widths() noexcept {
        for(const auto &s : saved_widths) {
            if(s.tag_data) {
                *reinterpret_cast<float *>(s.tag_data + AUTOAIM_WIDTH_OFFSET) = s.original;
            }
        }
        saved_widths.clear();
        currently_applied = false;
    }

    static void apply_autoaim_width() noexcept {
        restore_widths();

        auto &header = get_tag_data_header();
        Tag *tags = header.tag_array;
        auto count = header.tag_count;
        if(!tags || count == 0) {
            return;
        }

        for(std::uint32_t i = 0; i < count; i++) {
            Tag &tag = tags[i];
            if(tag.primary_class != TagClassInt::TAG_CLASS_BIPED) {
                continue;
            }
            if(!tag.data) {
                continue;
            }

            float *width_ptr = reinterpret_cast<float *>(tag.data + AUTOAIM_WIDTH_OFFSET);
            float original = *width_ptr;

            if(original > 0.0f && original < 2.0f) {
                saved_widths.push_back({ tag.data, original });
                *width_ptr = current_target;
            }
        }

        currently_applied = !saved_widths.empty();
    }

    static void on_map_load_reapply() noexcept {
        if(enabled) {
            apply_autoaim_width();
        }
        else {
            saved_widths.clear();
            currently_applied = false;
        }
    }

    bool hitreg_autoaim_width_command(int argc, const char **argv) {
        if(argc == 0) {
            if(enabled && currently_applied) {
                console_output("true (%.3f) - %zu bipeds patched", static_cast<double>(current_target), saved_widths.size());
            }
            else if(enabled) {
                console_output("true (%.3f) - pending map / no bipeds", static_cast<double>(current_target));
            }
            else {
                console_output("false");
            }
            return true;
        }

        if(std::strcmp(argv[0], "false") == 0 || std::strcmp(argv[0], "0") == 0 || std::strcmp(argv[0], "off") == 0) {
            enabled = false;
            restore_widths();
            if(map_load_hooked) {
                remove_map_load_event(on_map_load_reapply);
                map_load_hooked = false;
            }
            console_output("autoaim_width fix disabled (stock values restored)");
            return true;
        }

        float new_value = current_target;
        if(std::strcmp(argv[0], "true") == 0 || std::strcmp(argv[0], "1") == 0 || std::strcmp(argv[0], "on") == 0) {
            new_value = DEFAULT_TARGET_WIDTH;
        }
        else {
            new_value = static_cast<float>(std::atof(argv[0]));
            if(!std::isfinite(new_value)) {
                console_error("Expected a number or true/false");
                return false;
            }
        }

        if(new_value < 0.03f) {
            console_output("Warning: values below ~0.03 can break headshots. Clamping to 0.045");
            new_value = 0.045f;
        }
        if(new_value > 0.15f) {
            console_output("Warning: values above ~0.08 usually make hitreg worse. Proceeding anyway.");
        }

        current_target = new_value;
        enabled = true;
        apply_autoaim_width();

        if(!map_load_hooked) {
            add_map_load_event(on_map_load_reapply);
            map_load_hooked = true;
        }

        console_output("autoaim_width set to %.3f on %zu biped tags", static_cast<double>(current_target), saved_widths.size());
        return true;
    }
}
