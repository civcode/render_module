#include "control/input_protocol.hpp"

#include <cstdint>
#include <iomanip>
#include <iostream>

using namespace render_module::detail;
namespace dc = render_module::detail::dc;

int main() {
    std::cout << '[';
    bool first = true;
    for (unsigned type = 1; type <= 133; ++type) {
        if (type > 14 && type < 128) continue;

        dc::Packet packet;
        packet.type = dc::Type(type);
        packet.sequence = 0x12345678;
        packet.timestampUs = 0x0102030405060708ULL;
        packet.state.x = .25f;
        packet.state.y = .75f;
        packet.state.fence = packet.sequence;
        packet.state.epoch = 0x11223344;
        packet.state.focused = true;
        packet.state.source = 2;
        packet.state.buttons = 21;
        packet.state.modifiers = 3;
        packet.state.keys[0] = 2;
        packet.state.keys[13] = 2;
        packet.width = 1280;
        packet.height = 720;
        packet.dpr = 1.5f;
        packet.button = 4;
        packet.down = true;
        packet.key = 65;
        packet.modifiers = 3;
        packet.horizontal = -2.5f;
        packet.vertical = 3.25f;
        packet.text = TextUtf8{"hé😀"};
        packet.echoUs = 0x1122334455667788ULL;
        packet.fastBuffered = 4096;
        packet.controlBuffered = 8192;
        packet.control = true;
        packet.enabled = true;
        packet.code = type == 128 ? 512 : 1;

        const auto bytes = dc::Encode(packet,
            type < 128 ? dc::Direction::Client : dc::Direction::Server);
        if (bytes.empty()) {
            std::cerr << "Could not encode protocol vector type " << type << '\n';
            return 1;
        }
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"type\":" << std::dec << type << ",\"hex\":\"" << std::hex;
        for (const auto byte : bytes)
            std::cout << unsigned(byte >> 4) << unsigned(byte & 15);
        std::cout << "\"}";
    }
    std::cout << "]\n";
    return 0;
}
