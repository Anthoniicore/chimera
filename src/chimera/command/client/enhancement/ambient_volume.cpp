// SPDX-License-Identifier: GPL-3.0-only

#include "../../command.hpp"
#include "../../../halo_data/script.hpp"
#include "../../../event/map_load.hpp"
#include "../../../event/tick.hpp"
#include "../../../output/output.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Chimera {
    // sound_class_set_gain must not run on map_load — the HS engine is not ready yet
    // and will crash. Apply on tick 1 (same pattern as Chimera-Promod sound_volume).

    static bool  ambient_active     = false;
    static float ambient_gain       = 0.0f;
    static bool  pending_apply      = false;
    static bool  events_registered  = false;

    static void apply_ambient() noexcept {
        char script[96];
        std::snprintf(script, sizeof(script), "sound_class_set_gain ambient %.4f 0", static_cast<double>(ambient_gain));
        execute_script(script);
        std::snprintf(script, sizeof(script), "sound_class_set_gain device_nature %.4f 0", static_cast<double>(ambient_gain));
        execute_script(script);
        std::snprintf(script, sizeof(script), "sound_class_set_gain device_machinery %.4f 0", static_cast<double>(ambient_gain));
        execute_script(script);
    }

    static void restore_ambient() noexcept {
        execute_script("sound_class_set_gain ambient 1.0 0");
        execute_script("sound_class_set_gain device_nature 1.0 0");
        execute_script("sound_class_set_gain device_machinery 1.0 0");
    }

    static void on_tick_ambient() noexcept {
        if(pending_apply && get_tick_count() == 1) {
            apply_ambient();
            pending_apply = false;
        }
    }

    static void on_map_load_ambient() noexcept {
        if(ambient_active) {
            pending_apply = true;
        }
    }

    static void ensure_events() noexcept {
        if(!events_registered) {
            add_tick_event(on_tick_ambient);
            add_map_load_event(on_map_load_ambient);
            events_registered = true;
        }
    }

    static void maybe_remove_events() noexcept {
        if(events_registered && !ambient_active) {
            remove_tick_event(on_tick_ambient);
            remove_map_load_event(on_map_load_ambient);
            events_registered = false;
        }
    }

    bool ambient_volume_command(int argc, const char **argv) {
        if(argc == 0) {
            if(ambient_active) {
                console_output("chimera_ambient_volume: %.3f", static_cast<double>(ambient_gain));
            }
            else {
                console_output("chimera_ambient_volume: off");
            }
            return true;
        }

        if(std::strcmp(argv[0], "false") == 0 || std::strcmp(argv[0], "0") == 0 || std::strcmp(argv[0], "off") == 0) {
            if(ambient_active) {
                restore_ambient();
                ambient_active = false;
                pending_apply = false;
                maybe_remove_events();
            }
            console_output("chimera_ambient_volume: off");
            return true;
        }

        float gain = static_cast<float>(std::atof(argv[0]));
        if(gain < 0.0f || gain > 1.0f) {
            console_error("Value must be between 0.0 and 1.0 (or false/off)");
            return false;
        }

        ambient_gain = gain;
        ambient_active = true;
        ensure_events();

        // Already in-map: apply immediately. Otherwise wait for tick 1.
        if(get_tick_count() > 1) {
            apply_ambient();
            pending_apply = false;
        }
        else {
            pending_apply = true;
        }

        console_output("chimera_ambient_volume: %.3f", static_cast<double>(ambient_gain));
        return true;
    }
}
