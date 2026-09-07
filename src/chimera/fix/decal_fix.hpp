// SPDX-License-Identifier: GPL-3.0-only

#ifndef CHIMERA_DECAL_FIX_HPP
#define CHIMERA_DECAL_FIX_HPP

namespace Chimera {
    /**
     * Fix decal blend operations to match xbox.
     * Fix the game drawing decals with garbage vertices after reverting.
     */
    void set_up_decals_fix() noexcept;
}

#endif
