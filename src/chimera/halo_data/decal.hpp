// SPDX-License-Identifier: GPL-3.0-only

#ifndef CHIMERA_DECAL_HPP
#define CHIMERA_DECAL_HPP

#include <cstdint>

#include "object.hpp"
#include "../math_trig/math_trig.hpp"

namespace Chimera {
    struct DecalDatun {
        std::uint16_t index;
        std::uint16_t flags;
        
        std::int16_t cluster_index;
        std::int16_t layer;
        Point3D position;
        
        std::int32_t creation_time;
        char sequence_index;
        char frames_remaining;
        char sprite_index;
        char bitmap_index;
        float lifetime;
        float decay_time;
        
        Pixel32 color;
        unsigned char intensity;
        std::byte pad;
        std::int16_t quad_count;
        
        std::int32_t definition_index;
        
        std::int32_t prev_decal_index;
        std::int32_t next_decal_index;
    };
    static_assert(sizeof(DecalDatun) == 56);

    struct DecalVertex {
        Point3D position;
        Pixel32 texcoord;
    };
    static_assert(sizeof(DecalVertex) == 16);

    struct DecalTable : GenericTable<void> {
        /**
         * Get the decal table
         * @return reference to the decal table
         */
        static DecalTable &get_decal_table() noexcept;
    };
}

#endif
