// SPDX-License-Identifier: GPL-3.0-only

#include <cstring>

#include "../../halo_data/light.hpp"
#include "light.hpp"

namespace Chimera {
    #define MAX_LIGHT 0x380
    struct InterpolatedLight {
        bool interpolate = false;
        Point3D position;
        Point3D orientation[2];
        std::uint32_t some_counter = 0;
    };
    static InterpolatedLight light_buffers[2][MAX_LIGHT];
    static auto *current_tick = light_buffers[0];
    static auto *previous_tick = light_buffers[1];
    static bool tick_passed = false;

    void interpolate_light_before() noexcept {
        auto &light_table = LightTable::get_light_table();
        if(tick_passed) {
            if(current_tick == light_buffers[0]) {
                current_tick = light_buffers[1]; previous_tick = light_buffers[0];
            } else {
                current_tick = light_buffers[0]; previous_tick = light_buffers[1];
            }
            tick_passed = false;
            for(size_t i = 0; i < MAX_LIGHT; i++) {
                current_tick[i].interpolate = false;
                auto *light = light_table.get_element(i);
                if(!light) continue;
                auto &current = current_tick[i];
                current.some_counter = light->some_counter;
                if(current.some_counter > previous_tick[i].some_counter) {
                    current.interpolate = true;
                    current.position = light->position;
                    current.orientation[0] = light->orientation[0];
                    current.orientation[1] = light->orientation[1];
                }
            }
        }
        extern float interpolation_tick_progress;
        for(size_t i = 0; i < light_table.current_size && i < MAX_LIGHT; i++) {
            auto &current = current_tick[i];
            auto &previous = previous_tick[i];
            auto &memory = light_table.first_element[i];
            if(current.interpolate && previous.interpolate) {
                interpolate_point(previous.orientation[0], current.orientation[0], memory.orientation[0], interpolation_tick_progress);
                interpolate_point(previous.orientation[1], current.orientation[1], memory.orientation[1], interpolation_tick_progress);
                interpolate_point(previous.position, current.position, memory.position, interpolation_tick_progress);
            }
        }
    }

    void interpolate_light_after() noexcept {
        auto &light_table = LightTable::get_light_table();
        for(size_t i = 0; i < light_table.current_size && i < MAX_LIGHT; i++) {
            auto &current = current_tick[i];
            if(current.interpolate) {
                auto &memory = light_table.first_element[i];
                memory.position = current.position;
                memory.orientation[0] = current.orientation[0];
                memory.orientation[1] = current.orientation[1];
            }
        }
    }

    void interpolate_light_clear() noexcept { std::memset(light_buffers, 0, sizeof(light_buffers)); }
    void interpolate_light_on_tick() noexcept { tick_passed = true; }
}
