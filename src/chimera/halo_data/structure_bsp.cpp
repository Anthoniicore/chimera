// SPDX-License-Identifier: GPL-3.0-only

#include "structure_bsp.hpp"
#include "../signature/signature.hpp"
#include "../signature/hook.hpp"
#include "../chimera.hpp"
#include "game_variables.hpp"

namespace Chimera {
    StructureBsp *global_structure_bsp_get() noexcept {
        return *global_structure_bsp;
    }
}
