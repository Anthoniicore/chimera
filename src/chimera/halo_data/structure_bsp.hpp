// SPDX-License-Identifier: GPL-3.0-only

#ifndef CHIMERA_STRUCTURE_BSP_HPP
#define CHIMERA_STRUCTURE_BSP_HPP

#include <cstdint>
#include "tag.hpp"
#include "rasterizer_common.hpp"
#include "pad.hpp"

namespace Chimera {
    struct LeafMap {
        const void *bsp;
        TagBlock leaves;
        TagBlock portals;
    };
    static_assert(sizeof(LeafMap) == 28);

    struct WindState {
        bool valid;

        float velocity_variance;
        Euler2DYP angular_variance;

        float velocity;
        Vector3D velocity3d;
    };
    static_assert(sizeof(WindState) == 32);

    struct WindGlobals {
        bool initialized;
        Vector3D variance[3][64];
        short count;
        WindState wind_states[32];
        long time;
    };

    struct StructureWeatherPaletteEntry
    {
        char name[32];
        TagReference particle_system;
        PAD(0x2);
        short runtime_particle_system_global_function_index;
        char particle_system_global_function_name[32];
        long particle_system_unused[11];
        TagReference wind;
        Vector3D wind_direction;
        float wind_magnitude;
        PAD(0x2);
        short wind_global_function_index;
        char wind_global_function_name[32];
        PAD(0x2C);
    };
    static_assert(sizeof(StructureWeatherPaletteEntry) == 240);

    struct StructureBsp {
        TagReference lightmap_group;
        float vehicle_floor;
        float vehicle_ceiling;
        PAD(0x14);
        RenderLighting default_lighting;
        PAD(0x4);
        TagBlock collision_materials;
        TagBlock collision_bsp;
        TagBlock nodes;
        Rectangle3DF world_bounds;
        TagBlock leaves;
        TagBlock surface_references;
        TagBlock surfaces;
        TagBlock lightmaps;
        PAD(0xC);
        TagBlock lens_flares;
        TagBlock lens_flare_markers;
        TagBlock clusters;
        TagData cluster_data;
        TagBlock cluster_portals;
        PAD(0xC);
        TagBlock breakable_surfaces;
        TagBlock fog_planes;
        TagBlock fog_regions;
        TagBlock fog_palette;
        PAD(0x18);
        TagBlock weather_palette;
        TagBlock weather_polyhedra;
        PAD(0x18);
        TagBlock pathfinding_surfaces;
        TagBlock pathfinding_edges;
        TagBlock background_sound_palette;
        TagBlock sound_environment_palette;
        TagData sound_cluster_data;
        PAD(0x18);
        TagBlock markers;
        TagBlock detail_object_data;
        TagBlock runtime_decals;
        PAD(0x8);
        LeafMap leaf_map;
    };
    static_assert(sizeof(StructureBsp) == 648);

    /**
     * Get the address of the currently loaded bsp tag.
     * @return Pointer to the scenario_structure_bsp tag data.
     */
    StructureBsp *global_structure_bsp_get() noexcept;
}

#endif
