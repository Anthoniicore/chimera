// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Chimera {
    bool initialize_voice_transport(std::uint16_t local_port = 0) noexcept;
    void shutdown_voice_transport() noexcept;
    bool voice_transport_initialized() noexcept;

    // Destination is configured independently from Halo's game socket.
    bool set_voice_transport_destination(const std::string &host, std::uint16_t port) noexcept;
    bool voice_transport_has_destination() noexcept;

    bool send_voice_transport_packet(const std::uint8_t *data, std::size_t size) noexcept;
    bool receive_voice_transport_packet(std::vector<std::uint8_t> &packet,
                                        std::string *source_host = nullptr,
                                        std::uint16_t *source_port = nullptr) noexcept;
}
