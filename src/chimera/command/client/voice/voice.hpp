// SPDX-License-Identifier: GPL-3.0-only

#ifndef CHIMERA_VOICE_HPP
#define CHIMERA_VOICE_HPP

namespace Chimera {
    /**
     * Register the hooks that keep voice chat's room assignment in sync with
     * whatever multiplayer server you're actually on: a native preconnect
     * hook (same one bookmark.cpp uses) for joining, and a polled
     * server_type() check for leaving. Must be called explicitly from
     * chimera.cpp's init sequence (not as a global static object) - a
     * static object here would run at an undefined point relative to other
     * globals like preconnect_events/command_events, and could silently
     * lose the registration.
     */
    void set_up_voice_connection_watcher() noexcept;
}

#endif
