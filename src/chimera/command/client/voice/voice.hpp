// SPDX-License-Identifier: GPL-3.0-only

#ifndef CHIMERA_VOICE_HPP
#define CHIMERA_VOICE_HPP

namespace Chimera {
    /**
     * Register the hook that watches Halo's native "connect"/"disconnect"
     * console commands to keep voice chat's room assignment in sync. Must
     * be called explicitly from chimera.cpp's init sequence (not as a
     * global static object) - see the comment above its definition in
     * voice.cpp for why.
     */
    void set_up_voice_native_command_watcher() noexcept;
}

#endif
