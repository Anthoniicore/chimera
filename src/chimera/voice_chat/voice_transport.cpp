// SPDX-License-Identifier: GPL-3.0-only

#include "voice_transport.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <cstring>
#include <limits>

namespace Chimera {
    namespace {
        SOCKET g_socket = INVALID_SOCKET;
        bool g_winsock_initialized = false;
        bool g_has_destination = false;
        sockaddr_storage g_destination{};
        int g_destination_length = 0;

        bool ensure_winsock() noexcept {
            if(g_winsock_initialized) return true;
            WSADATA data{};
            if(WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
            g_winsock_initialized = true;
            return true;
        }
    }

    bool initialize_voice_transport(std::uint16_t local_port) noexcept {
        if(g_socket != INVALID_SOCKET) return true;
        if(!ensure_winsock()) return false;

        g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if(g_socket == INVALID_SOCKET) return false;

        u_long nonblocking = 1;
        if(ioctlsocket(g_socket, FIONBIO, &nonblocking) != 0) {
            closesocket(g_socket);
            g_socket = INVALID_SOCKET;
            return false;
        }

        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = htons(local_port);
        if(bind(g_socket, reinterpret_cast<const sockaddr *>(&local), sizeof(local)) == SOCKET_ERROR) {
            closesocket(g_socket);
            g_socket = INVALID_SOCKET;
            return false;
        }
        return true;
    }

    void shutdown_voice_transport() noexcept {
        g_has_destination = false;
        g_destination_length = 0;
        if(g_socket != INVALID_SOCKET) {
            closesocket(g_socket);
            g_socket = INVALID_SOCKET;
        }
        if(g_winsock_initialized) {
            WSACleanup();
            g_winsock_initialized = false;
        }
    }

    bool voice_transport_initialized() noexcept { return g_socket != INVALID_SOCKET; }

    bool set_voice_transport_destination(const std::string &host, std::uint16_t port) noexcept {
        if(host.empty() || port == 0) return false;
        if(!initialize_voice_transport()) return false;

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;

        addrinfo *result = nullptr;
        const auto port_text = std::to_string(port);
        if(getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result) != 0 || !result) return false;

        bool success = false;
        if(result->ai_addrlen <= static_cast<int>(sizeof(g_destination))) {
            std::memcpy(&g_destination, result->ai_addr, result->ai_addrlen);
            g_destination_length = static_cast<int>(result->ai_addrlen);
            g_has_destination = true;
            success = true;
        }
        freeaddrinfo(result);
        return success;
    }

    bool voice_transport_has_destination() noexcept { return g_has_destination; }

    bool send_voice_transport_packet(const std::uint8_t *data, std::size_t size) noexcept {
        if(!data || size == 0 || size > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
        if(!voice_transport_initialized() || !g_has_destination) return false;
        const auto sent = sendto(g_socket,
                                 reinterpret_cast<const char *>(data),
                                 static_cast<int>(size),
                                 0,
                                 reinterpret_cast<const sockaddr *>(&g_destination),
                                 g_destination_length);
        return sent == static_cast<int>(size);
    }

    bool receive_voice_transport_packet(std::vector<std::uint8_t> &packet,
                                        std::string *source_host,
                                        std::uint16_t *source_port) noexcept {
        packet.clear();
        if(!voice_transport_initialized()) return false;

        std::array<std::uint8_t, 4096> buffer{};
        sockaddr_storage source{};
        int source_length = sizeof(source);
        const auto received = recvfrom(g_socket,
                                       reinterpret_cast<char *>(buffer.data()),
                                       static_cast<int>(buffer.size()),
                                       0,
                                       reinterpret_cast<sockaddr *>(&source),
                                       &source_length);
        if(received == SOCKET_ERROR || received <= 0) return false;

        packet.assign(buffer.begin(), buffer.begin() + received);

        if(source_host || source_port) {
            char host[NI_MAXHOST]{};
            char service[NI_MAXSERV]{};
            if(getnameinfo(reinterpret_cast<const sockaddr *>(&source),
                           source_length,
                           host,
                           sizeof(host),
                           service,
                           sizeof(service),
                           NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
                if(source_host) *source_host = host;
                if(source_port) *source_port = static_cast<std::uint16_t>(std::strtoul(service, nullptr, 10));
            }
        }
        return true;
    }
}
