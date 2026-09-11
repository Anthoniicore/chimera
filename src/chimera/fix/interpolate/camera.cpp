// SPDX-License-Identifier: GPL-3.0-only

#include <algorithm>
#include <cstring>

#include "../../halo_data/object.hpp"
#include "../../halo_data/camera.hpp"
#include "../../halo_data/pause.hpp"
#include "../../halo_data/player.hpp"
#include "camera.hpp"

#include "../../signature/signature.hpp"
#include "../../chimera.hpp"

namespace Chimera {
    struct InterpolatedCamera {
        CameraType type;
        ObjectID followed_object;
        CameraData data;
    };

    static InterpolatedCamera camera_buffers[2];
    static auto *current_tick = camera_buffers + 0;
    static auto *previous_tick = camera_buffers + 1;
    static bool tick_passed = false;
    static bool skip;
    static bool rollback;
    extern bool spectate_enabled;

    void interpolate_camera_before() noexcept {
        if(game_paused()) return;

        auto type = camera_type();
        if(tick_passed) {
            if(current_tick == camera_buffers) {
                current_tick = camera_buffers + 1;
                previous_tick = camera_buffers + 0;
            }
            else {
                current_tick = camera_buffers + 0;
                previous_tick = camera_buffers + 1;
            }

            static auto **followed_object = reinterpret_cast<ObjectID **>(get_chimera().get_signature("followed_object_sig").data() + 10);
            current_tick->data = camera_data();
            current_tick->type = type;
            current_tick->followed_object = **followed_object;
            tick_passed = false;

            skip = (type == CameraType::CAMERA_CINEMATIC && current_tick->followed_object.is_null()) ||
                   (current_tick->followed_object != previous_tick->followed_object || current_tick->type != previous_tick->type);

            if(!skip && type == CameraType::CAMERA_FIRST_PERSON) {
                skip = distance_squared(previous_tick->data.position, current_tick->data.position) > 5.0 * 5.0;
            }
        }

        if(skip) return;

        auto &data = camera_data();
        extern float interpolation_tick_progress;

        bool vehicle_first_person = false;
        if(type == CameraType::CAMERA_FIRST_PERSON || type == CameraType::CAMERA_DEBUG) {
            auto *player = PlayerTable::get_player_table().get_client_player();
            if(player) {
                auto *object = ObjectTable::get_object_table().get_dynamic_object(player->object_id);
                if(object) {
                    vehicle_first_person = !object->object.parent_object_index.is_null();
                    if(type == CameraType::CAMERA_DEBUG && !TEST_FLAG(object->object.damage_flags, OBJECT_DAMAGE_FLAGS_DEAD_BIT)) {
                        skip = true;
                        return;
                    }
                    if(type == CameraType::CAMERA_FIRST_PERSON && !vehicle_first_person &&
                        distance_squared(previous_tick->data.position, current_tick->data.position) > 0.5 * 0.5 &&
                        magnitude_squared3d(object->object.translational_velocity) <= 0.5 * 0.5) {
                        skip = true;
                        return;
                    }
                }
            }
        }

        // Keep the camera in the same presentation time domain as the world.
        // Remote/world objects are rendered between the previous and current
        // simulation ticks. The local first-person camera must use the same
        // interpolation phase; otherwise the camera moves at the newest tick
        // while the world is still one presentation step behind, which creates
        // relative warping/jitter whenever the player moves.
        interpolate_point(previous_tick->data.position, current_tick->data.position, data.position, interpolation_tick_progress);

        // Don't interpolate rotation if in first person unless we're in a vehicle.
        if(type != CameraType::CAMERA_FIRST_PERSON || vehicle_first_person || spectate_enabled) {
            interpolate_point(previous_tick->data.orientation[0], current_tick->data.orientation[0], data.orientation[0], interpolation_tick_progress);
            interpolate_point(previous_tick->data.orientation[1], current_tick->data.orientation[1], data.orientation[1], interpolation_tick_progress);
            rollback = true;
        }
    }

    void interpolate_camera_after() noexcept {
        if(skip || game_paused()) return;
        auto &data = camera_data();
        data.position = current_tick->data.position;
        if(rollback) {
            std::copy(current_tick->data.orientation, current_tick->data.orientation + 2, data.orientation);
            rollback = false;
        }
    }

    void interpolate_camera_clear() noexcept {
        skip = true;
        rollback = false;
        std::memset(camera_buffers, 0, sizeof(camera_buffers));
    }

    void interpolate_camera_on_tick() noexcept { tick_passed = true; }
}
