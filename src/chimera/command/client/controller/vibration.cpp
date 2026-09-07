// SPDX-License-Identifier: GPL-3.0-only

#include "../../command.hpp"
#include "../../../output/output.hpp"

#include "../../../event/damage.hpp"
#include "../../../event/tick.hpp"
#include "../../../halo_data/player.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>


// ============================================================
// XInput types kept local so Chimera does not depend on
// <Xinput.h>, which is not present in every MinGW setup.
// ============================================================

struct ChimeraXInputVibration {
    WORD wLeftMotorSpeed;
    WORD wRightMotorSpeed;
};

// Layout matches XINPUT_GAMEPAD / XINPUT_STATE exactly. Only used to
// query connection status via XInputGetState, so the button/stick
// fields are never read.
struct ChimeraXInputGamepad {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
};

struct ChimeraXInputState {
    DWORD dwPacketNumber;
    ChimeraXInputGamepad Gamepad;
};

namespace Chimera {

    using XInputSetStateFunction =
        DWORD (WINAPI *)(DWORD, ChimeraXInputVibration *);

    using XInputGetStateFunction =
        DWORD (WINAPI *)(DWORD, ChimeraXInputState *);


    // ========================================================
    // XInput
    // ========================================================

    static HMODULE g_xinput_module = nullptr;
    static XInputSetStateFunction g_xinput_set_state = nullptr;
    static XInputGetStateFunction g_xinput_get_state = nullptr;


    // ========================================================
    // Controller connection state
    //
    // XInputGetState is only polled once per second (not every
    // tick) since querying an empty controller slot is not free
    // on every platform and this runs inside the game's tick.
    // ========================================================

    static bool g_controller_connected = false;
    static ULONGLONG g_last_controller_check_ms = 0;

    static constexpr ULONGLONG CONTROLLER_POLL_INTERVAL_MS = 1000;


    // ========================================================
    // Output state
    // ========================================================

    static DWORD g_controller = 0;
    static std::uint16_t g_left_motor = 0;
    static std::uint16_t g_right_motor = 0;
    static bool g_vibrating = false;

    // Manual commands own the controller until another automatic
    // event is generated. Automatic effects never leave rumble on.
    static bool g_manual_override = false;

    // Global strength multiplier, 100 = default.
    static std::uint16_t g_master_strength = 100;

    static bool g_vibration_enabled = true;


    // ========================================================
    // Event switches
    // ========================================================

    static bool g_hit_vibration_enabled = true;
    static bool g_hurt_vibration_enabled = true;
    static bool g_fire_vibration_enabled = true;
    static bool g_melee_vibration_enabled = true;


    // ========================================================
    // Tunable rumble profiles
    //
    // Values are deliberately below 65535. XInput rumble is not
    // a frequency API; left/right amplitudes are the two available
    // channels. The mixer below creates the useful "shape".
    // ========================================================

    struct RumbleProfile {
        std::uint16_t left;
        std::uint16_t right;
        ULONGLONG duration_ms;
        ULONGLONG fade_ms;
    };

    // Halo/MCC-like starting point. The asymmetric channels make
    // different events distinguishable without excessive strength.
    static RumbleProfile g_hit_profile {
        40000, 27000, 105, 70
    };

    static RumbleProfile g_hurt_profile {
        28000, 52000, 175, 105
    };

    static RumbleProfile g_fire_profile {
        20500, 14500, 95, 55
    };

    static RumbleProfile g_melee_profile {
        57000, 42000, 145, 80
    };


    // ========================================================
    // Effect mixer
    //
    // The old implementation had one global pulse. A new shot,
    // hit, hurt or melee event therefore completely replaced the
    // previous event. This mixer keeps independent effects alive
    // and combines them, so simultaneous events feel layered.
    // ========================================================

    enum class RumbleEffectType : std::uint8_t {
        HIT,
        HURT,
        FIRE,
        MELEE
    };

    struct RumbleEffect {
        bool active = false;
        RumbleEffectType type = RumbleEffectType::FIRE;
        std::uint16_t left = 0;
        std::uint16_t right = 0;
        ULONGLONG start_ms = 0;
        ULONGLONG end_ms = 0;
        ULONGLONG fade_ms = 0;
    };

    static constexpr std::size_t RUMBLE_EFFECT_COUNT = 8;
    static std::array<RumbleEffect, RUMBLE_EFFECT_COUNT> g_effects {};

    // Fire is intentionally reused instead of stacked indefinitely.
    // Four independent impact slots allow rapid hit/hurt/melee events
    // to overlap without creating an unbounded queue.
    static constexpr std::size_t FIRE_SLOT = 0;
    static constexpr std::size_t HIT_SLOT = 1;
    static constexpr std::size_t HURT_SLOT = 2;
    static constexpr std::size_t MELEE_SLOT = 3;
    static constexpr std::size_t IMPACT_SLOT_BEGIN = 4;


    // ========================================================
    // Fire/melee edge detection
    // ========================================================

    static TickCount g_last_seen_fire_time = 0;
    static bool g_fire_state_initialized = false;
    static bool g_last_seen_melee = false;


    // ========================================================
    // Event registration state
    // ========================================================

    static bool g_damage_event_registered = false;
    static bool g_tick_event_registered = false;


    // ========================================================
    // Helpers
    // ========================================================

    static std::uint16_t scale_motor(
        std::uint16_t value
    ) noexcept {
        std::uint32_t scaled =
            static_cast<std::uint32_t>(value) * g_master_strength;

        scaled /= 100;

        return static_cast<std::uint16_t>(
            std::min<std::uint32_t>(scaled, 65535)
        );
    }


    static std::uint16_t clamp_motor(
        std::uint32_t value
    ) noexcept {
        return static_cast<std::uint16_t>(
            std::min<std::uint32_t>(value, 65535)
        );
    }


    // Smooth fade-out. The first part stays at full amplitude and
    // the final fade_ms is eased out with a smoothstep curve.
    static double effect_gain(
        const RumbleEffect &effect,
        ULONGLONG now
    ) noexcept {

        if(!effect.active || now >= effect.end_ms) {
            return 0.0;
        }

        if(effect.fade_ms == 0) {
            return 1.0;
        }

        ULONGLONG fade_start =
            effect.end_ms > effect.fade_ms
                ? effect.end_ms - effect.fade_ms
                : effect.start_ms;

        if(now <= fade_start) {
            return 1.0;
        }

        double t = static_cast<double>(now - fade_start) /
                   static_cast<double>(effect.end_ms - fade_start);

        t = std::clamp(t, 0.0, 1.0);

        // Smoothstep: less mechanical than a linear cutoff.
        return 1.0 - (t * t * (3.0 - 2.0 * t));
    }


    // ========================================================
    // Load XInput dynamically
    // ========================================================

    static bool load_xinput() noexcept {
        if(g_xinput_set_state != nullptr) {
            return true;
        }

        const char *dlls[] = {
            "xinput1_4.dll",
            "xinput1_3.dll",
            "xinput9_1_0.dll"
        };

        for(const char *dll : dlls) {
            HMODULE module = LoadLibraryA(dll);
            if(module == nullptr) {
                continue;
            }

            FARPROC set_state_proc =
                GetProcAddress(module, "XInputSetState");
            FARPROC get_state_proc =
                GetProcAddress(module, "XInputGetState");

            if(set_state_proc != nullptr) {
                g_xinput_module = module;
                g_xinput_set_state =
                    reinterpret_cast<XInputSetStateFunction>(
                        set_state_proc
                    );

                // XInputGetState should always be present alongside
                // XInputSetState, but degrade gracefully if not: the
                // controller will simply never be seen as connected.
                if(get_state_proc != nullptr) {
                    g_xinput_get_state =
                        reinterpret_cast<XInputGetStateFunction>(
                            get_state_proc
                        );
                }

                console_output(
                    "[chimera_vibration] XInput cargado: %s",
                    dll
                );

                return true;
            }

            FreeLibrary(module);
        }

        console_error(
            "[chimera_vibration] No se pudo cargar XInput."
        );

        return false;
    }


    // ========================================================
    // Controller connection detection
    //
    // Polled at most once per second unless force=true. Only logs
    // on state transitions, never on every check.
    // ========================================================

    static void refresh_controller_connection(
        ULONGLONG now,
        bool force
    ) noexcept {

        if(
            !force &&
            (now - g_last_controller_check_ms) <
                CONTROLLER_POLL_INTERVAL_MS
        ) {
            return;
        }

        g_last_controller_check_ms = now;

        if(!load_xinput() || g_xinput_get_state == nullptr) {
            if(g_controller_connected) {
                g_controller_connected = false;
                g_left_motor = 0;
                g_right_motor = 0;
                g_vibrating = false;

                console_output(
                    "[chimera_vibration] Control desconectado."
                );
            }
            return;
        }

        ChimeraXInputState state {};
        DWORD result = g_xinput_get_state(g_controller, &state);
        bool connected = result == ERROR_SUCCESS;

        if(connected == g_controller_connected) {
            return;
        }

        g_controller_connected = connected;

        if(connected) {
            console_output(
                "[chimera_vibration] Control %lu conectado.",
                static_cast<unsigned long>(g_controller)
            );
        }
        else {
            // Cached motor state is stale once the controller is
            // gone; reset it so a reconnect doesn't skip re-sending
            // the first real vibration (apply_vibration_raw skips
            // sends when left/right match the cached values).
            g_left_motor = 0;
            g_right_motor = 0;
            g_vibrating = false;

            console_output(
                "[chimera_vibration] Control %lu desconectado.",
                static_cast<unsigned long>(g_controller)
            );
        }
    }


    // ========================================================
    // Low-level output
    // ========================================================

    static bool apply_vibration_raw(
        std::uint16_t left,
        std::uint16_t right
    ) noexcept {

        if(!load_xinput()) {
            return false;
        }

        // Avoid sending the same XInput state every tick. This is
        // particularly useful for automatic-fire weapons.
        if(
            left == g_left_motor &&
            right == g_right_motor
        ) {
            return true;
        }

        ChimeraXInputVibration vibration {};
        vibration.wLeftMotorSpeed = left;
        vibration.wRightMotorSpeed = right;

        DWORD result =
            g_xinput_set_state(
                g_controller,
                &vibration
            );

        if(result != ERROR_SUCCESS) {
            console_error(
                "[chimera_vibration] "
                "No se pudo activar controller %lu (error %lu).",
                static_cast<unsigned long>(g_controller),
                static_cast<unsigned long>(result)
            );
            return false;
        }

        g_left_motor = left;
        g_right_motor = right;
        g_vibrating = left != 0 || right != 0;

        return true;
    }


    static void stop_vibration() noexcept {
        g_manual_override = false;

        if(!g_controller_connected) {
            return;
        }

        if(!load_xinput()) {
            return;
        }

        apply_vibration_raw(0, 0);
    }


    // ========================================================
    // Mixer
    // ========================================================

    static void clear_expired_effects(
        ULONGLONG now
    ) noexcept {
        for(auto &effect : g_effects) {
            if(effect.active && now >= effect.end_ms) {
                effect.active = false;
            }
        }
    }


    static void mix_and_apply(
        ULONGLONG now
    ) noexcept {

        if(g_manual_override) {
            return;
        }

        if(!g_controller_connected) {
            // Nothing to send, but effect timers still need to
            // expire normally so nothing bursts once reconnected.
            clear_expired_effects(now);
            return;
        }

        std::uint32_t left = 0;
        std::uint32_t right = 0;

        for(auto &effect : g_effects) {
            if(!effect.active) {
                continue;
            }

            double gain = effect_gain(effect, now);
            if(gain <= 0.0) {
                continue;
            }

            left += static_cast<std::uint32_t>(
                static_cast<double>(effect.left) * gain
            );

            right += static_cast<std::uint32_t>(
                static_cast<double>(effect.right) * gain
            );
        }

        clear_expired_effects(now);

        apply_vibration_raw(
            scale_motor(clamp_motor(left)),
            scale_motor(clamp_motor(right))
        );
    }


    static void trigger_effect(
        RumbleEffectType type,
        const RumbleProfile &profile
    ) noexcept {

        if(!g_vibration_enabled || !g_controller_connected) {
            return;
        }

        ULONGLONG now = GetTickCount64();

        std::size_t slot = IMPACT_SLOT_BEGIN;

        switch(type) {
            case RumbleEffectType::FIRE:
                slot = FIRE_SLOT;
                break;
            case RumbleEffectType::HIT:
                slot = HIT_SLOT;
                break;
            case RumbleEffectType::HURT:
                slot = HURT_SLOT;
                break;
            case RumbleEffectType::MELEE:
                slot = MELEE_SLOT;
                break;
        }

        // For impact effects, use a spare slot when the main slot is
        // already active. This preserves very close impacts.
        if(
            type != RumbleEffectType::FIRE &&
            g_effects[slot].active &&
            now < g_effects[slot].end_ms
        ) {
            for(std::size_t i = IMPACT_SLOT_BEGIN;
                i < RUMBLE_EFFECT_COUNT;
                ++i) {
                if(!g_effects[i].active) {
                    slot = i;
                    break;
                }
            }
        }

        auto &effect = g_effects[slot];
        effect.active = true;
        effect.type = type;
        effect.left = profile.left;
        effect.right = profile.right;
        effect.start_ms = now;
        effect.end_ms = now + profile.duration_ms;
        effect.fade_ms =
            std::min(profile.fade_ms, profile.duration_ms);

        mix_and_apply(now);
    }


    // ========================================================
    // Manual commands
    // ========================================================

    static bool apply_manual_vibration(
        std::uint16_t left,
        std::uint16_t right
    ) noexcept {

        if(!g_controller_connected) {
            console_error(
                "[chimera_vibration] No hay ningun control conectado."
            );
            return false;
        }

        for(auto &effect : g_effects) {
            effect.active = false;
        }

        g_manual_override = true;

        return apply_vibration_raw(
            scale_motor(left),
            scale_motor(right)
        );
    }


    // ========================================================
    // DAMAGE EVENT
    // ========================================================

    static bool vibration_on_damage(
        ObjectID &object,
        TagID &,
        float &,
        PlayerID &causing_player,
        ObjectID &
    ) {

        if(!g_vibration_enabled) {
            return true;
        }

        if(
            !g_hit_vibration_enabled &&
            !g_hurt_vibration_enabled
        ) {
            return true;
        }

        auto &player_table =
            PlayerTable::get_player_table();

        auto *client_player =
            player_table.get_client_player();

        if(client_player == nullptr) {
            return true;
        }

        if(g_hit_vibration_enabled) {
            PlayerID client_player_id =
                get_client_player_id();

            if(causing_player == client_player_id) {
                trigger_effect(
                    RumbleEffectType::HIT,
                    g_hit_profile
                );
            }
        }

        if(
            g_hurt_vibration_enabled &&
            object == client_player->object_id
        ) {
            trigger_effect(
                RumbleEffectType::HURT,
                g_hurt_profile
            );
        }

        return true;
    }


    // ========================================================
    // TICK
    // ========================================================

    static void vibration_tick() {
        ULONGLONG now = GetTickCount64();

        refresh_controller_connection(now, false);

        if(!g_vibration_enabled) {
            return;
        }

        // Manual test/commands deliberately bypass automatic effects.
        if(!g_manual_override) {
            clear_expired_effects(now);
            mix_and_apply(now);
        }

        if(
            !g_fire_vibration_enabled &&
            !g_melee_vibration_enabled
        ) {
            return;
        }

        auto &player_table =
            PlayerTable::get_player_table();

        auto *client_player =
            player_table.get_client_player();

        if(client_player == nullptr) {
            return;
        }

        // ----------------------------------------------------
        // Fire: last_fire_time is the reliable game-side event.
        // ----------------------------------------------------

        if(g_fire_vibration_enabled) {
            TickCount current_fire_time =
                client_player->last_fire_time;

            if(!g_fire_state_initialized) {
                g_last_seen_fire_time = current_fire_time;
                g_fire_state_initialized = true;
            }
            else if(current_fire_time != g_last_seen_fire_time) {
                g_last_seen_fire_time = current_fire_time;

                trigger_effect(
                    RumbleEffectType::FIRE,
                    g_fire_profile
                );
            }
        }

        // ----------------------------------------------------
        // Melee: rising edge only.
        // ----------------------------------------------------

        if(g_melee_vibration_enabled) {
            bool is_meleeing = client_player->melee != 0;

            if(is_meleeing && !g_last_seen_melee) {
                trigger_effect(
                    RumbleEffectType::MELEE,
                    g_melee_profile
                );
            }

            g_last_seen_melee = is_meleeing;
        }

        // The event may have created a new effect. Update once more
        // so the newly triggered profile is reflected immediately.
        mix_and_apply(GetTickCount64());
    }


    // ========================================================
    // Event registration
    // ========================================================

    static void update_damage_event() noexcept {
        bool should_register =
            g_vibration_enabled &&
            (g_hit_vibration_enabled || g_hurt_vibration_enabled);

        if(should_register && !g_damage_event_registered) {
            add_damage_event(vibration_on_damage);
            g_damage_event_registered = true;
            return;
        }

        if(!should_register && g_damage_event_registered) {
            remove_damage_event(vibration_on_damage);
            g_damage_event_registered = false;
        }
    }


    static void update_tick_event() noexcept {
        bool should_register =
            g_vibration_enabled &&
            (
                g_fire_vibration_enabled ||
                g_melee_vibration_enabled ||
                g_hit_vibration_enabled ||
                g_hurt_vibration_enabled
            );

        if(should_register && !g_tick_event_registered) {
            add_tick_event(vibration_tick);
            g_tick_event_registered = true;

            g_fire_state_initialized = false;
            g_last_seen_melee = false;
            return;
        }

        if(!should_register && g_tick_event_registered) {
            remove_tick_event(vibration_tick);
            g_tick_event_registered = false;
        }
    }


    // ========================================================
    // Initialization
    // ========================================================

    void set_up_vibration() noexcept {
        g_vibration_enabled = true;
        g_hit_vibration_enabled = true;
        g_hurt_vibration_enabled = true;
        g_fire_vibration_enabled = true;
        g_melee_vibration_enabled = true;
        g_manual_override = false;

        refresh_controller_connection(GetTickCount64(), true);

        update_damage_event();
        update_tick_event();
    }


    // ========================================================
    // COMMAND
    // ========================================================

    bool vibration_command(
        int argc,
        const char **argv
    ) {

        // ----------------------------------------------------
        // No arguments: status
        // ----------------------------------------------------

        if(argc == 0) {
            refresh_controller_connection(GetTickCount64(), true);

            console_output(
                "[chimera_vibration] controller=%lu connected=%s "
                "left=%u right=%u active=%s strength=%u "
                "hit=%s hurt=%s fire=%s melee=%s",
                static_cast<unsigned long>(g_controller),
                BOOL_TO_STR(g_controller_connected),
                static_cast<unsigned int>(g_left_motor),
                static_cast<unsigned int>(g_right_motor),
                BOOL_TO_STR(g_vibrating),
                static_cast<unsigned int>(g_master_strength),
                BOOL_TO_STR(g_hit_vibration_enabled),
                BOOL_TO_STR(g_hurt_vibration_enabled),
                BOOL_TO_STR(g_fire_vibration_enabled),
                BOOL_TO_STR(g_melee_vibration_enabled)
            );
            return true;
        }

        // ----------------------------------------------------
        // MCC profile
        // ----------------------------------------------------

        if(argc == 1 && std::strcmp(argv[0], "mcc") == 0) {
            g_vibration_enabled = true;
            g_hit_vibration_enabled = true;
            g_hurt_vibration_enabled = true;
            g_fire_vibration_enabled = true;
            g_melee_vibration_enabled = true;
            g_master_strength = 100;
            g_manual_override = false;

            update_damage_event();
            update_tick_event();

            console_output(
                "[chimera_vibration] Perfil MCC activado."
            );
            return true;
        }

        // ----------------------------------------------------
        // Individual switches
        // ----------------------------------------------------

        if(argc == 2 && std::strcmp(argv[0], "hit") == 0) {
            g_hit_vibration_enabled = STR_TO_BOOL(argv[1]);
            if(g_hit_vibration_enabled) {
                g_vibration_enabled = true;
                g_manual_override = false;
            }
            update_damage_event();
            update_tick_event();
            return true;
        }

        if(argc == 2 && std::strcmp(argv[0], "hurt") == 0) {
            g_hurt_vibration_enabled = STR_TO_BOOL(argv[1]);
            if(g_hurt_vibration_enabled) {
                g_vibration_enabled = true;
                g_manual_override = false;
            }
            update_damage_event();
            update_tick_event();
            return true;
        }

        if(argc == 2 && std::strcmp(argv[0], "fire") == 0) {
            g_fire_vibration_enabled = STR_TO_BOOL(argv[1]);
            if(g_fire_vibration_enabled) {
                g_vibration_enabled = true;
                g_manual_override = false;
            }
            update_tick_event();
            return true;
        }

        if(argc == 2 && std::strcmp(argv[0], "melee") == 0) {
            g_melee_vibration_enabled = STR_TO_BOOL(argv[1]);
            if(g_melee_vibration_enabled) {
                g_vibration_enabled = true;
                g_manual_override = false;
            }
            update_tick_event();
            return true;
        }

        // ----------------------------------------------------
        // Global strength 0..200 (%)
        // ----------------------------------------------------

        if(argc == 2 && std::strcmp(argv[0], "strength") == 0) {
            int value = std::atoi(argv[1]);
            value = std::clamp(value, 0, 200);
            g_master_strength = static_cast<std::uint16_t>(value);

            if(g_master_strength == 0) {
                stop_vibration();
            }
            else if(!g_manual_override) {
                mix_and_apply(GetTickCount64());
            }

            console_output(
                "[chimera_vibration] strength=%u%%",
                static_cast<unsigned int>(g_master_strength)
            );
            return true;
        }

        // ----------------------------------------------------
        // Test automatic mixer
        // ----------------------------------------------------

        if(argc == 1 && std::strcmp(argv[0], "test") == 0) {
            g_manual_override = false;
            for(auto &effect : g_effects) {
                effect.active = false;
            }

            trigger_effect(
                RumbleEffectType::MELEE,
                g_melee_profile
            );
            return true;
        }

        // ----------------------------------------------------
        // Manual output
        // ----------------------------------------------------

        if(argc == 2 && std::strcmp(argv[0], "left") == 0) {
            int value = std::clamp(std::atoi(argv[1]), 0, 65535);
            return apply_manual_vibration(
                static_cast<std::uint16_t>(value),
                g_right_motor
            );
        }

        if(argc == 2 && std::strcmp(argv[0], "right") == 0) {
            int value = std::clamp(std::atoi(argv[1]), 0, 65535);
            return apply_manual_vibration(
                g_left_motor,
                static_cast<std::uint16_t>(value)
            );
        }

        if(argc == 2 && std::strcmp(argv[0], "both") == 0) {
            int value = std::clamp(std::atoi(argv[1]), 0, 65535);
            auto motor = static_cast<std::uint16_t>(value);
            return apply_manual_vibration(motor, motor);
        }

        // ----------------------------------------------------
        // Manual stop
        // ----------------------------------------------------

        if(
            argc == 1 &&
            (
                std::strcmp(argv[0], "0") == 0 ||
                std::strcmp(argv[0], "off") == 0
            )
        ) {
            g_vibration_enabled = false;
            g_manual_override = false;

            for(auto &effect : g_effects) {
                effect.active = false;
            }

            stop_vibration();
            update_damage_event();
            update_tick_event();
            return true;
        }

        // ----------------------------------------------------
        // Master on
        // ----------------------------------------------------

        if(
            argc == 1 &&
            (
                std::strcmp(argv[0], "1") == 0 ||
                std::strcmp(argv[0], "on") == 0
            )
        ) {
            g_vibration_enabled = true;
            g_manual_override = false;
            update_damage_event();
            update_tick_event();
            return true;
        }

        // ----------------------------------------------------
        // Controller 0..3
        // ----------------------------------------------------

        if(argc == 2 && std::strcmp(argv[0], "controller") == 0) {
            int controller = std::atoi(argv[1]);

            if(controller < 0 || controller > 3) {
                console_error(
                    "[chimera_vibration] Controller debe estar entre 0 y 3."
                );
                return false;
            }

            stop_vibration();
            g_controller = static_cast<DWORD>(controller);

            refresh_controller_connection(GetTickCount64(), true);

            console_output(
                "[chimera_vibration] Controller=%d connected=%s",
                controller,
                BOOL_TO_STR(g_controller_connected)
            );
            return true;
        }

        console_error(
            "[chimera_vibration] Uso: chimera_vibration "
            "[test|0|1|on|off|mcc] | "
            "hit [0|1] | hurt [0|1] | fire [0|1] | melee [0|1] | "
            "strength <0..200> | left <0..65535> | right <0..65535> | "
            "both <0..65535> | controller <0..3>"
        );

        return false;
    }

}