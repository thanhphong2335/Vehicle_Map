#include "TextUtils.h"

namespace {
char vietnameseAscii(uint32_t codepoint) {
    if (codepoint >= 0x1EA0 && codepoint <= 0x1EB5) return (codepoint & 1U) ? 'a' : 'A';
    if (codepoint >= 0x1EB8 && codepoint <= 0x1EC7) return (codepoint & 1U) ? 'e' : 'E';
    if (codepoint >= 0x1EC8 && codepoint <= 0x1ECB) return (codepoint & 1U) ? 'i' : 'I';
    if (codepoint >= 0x1ECC && codepoint <= 0x1EE3) return (codepoint & 1U) ? 'o' : 'O';
    if (codepoint >= 0x1EE4 && codepoint <= 0x1EF1) return (codepoint & 1U) ? 'u' : 'U';
    if (codepoint >= 0x1EF2 && codepoint <= 0x1EF9) return (codepoint & 1U) ? 'y' : 'Y';

    switch (codepoint) {
        case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x0102: return 'A';
        case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x0103: return 'a';
        case 0x00C8: case 0x00C9: case 0x00CA: return 'E';
        case 0x00E8: case 0x00E9: case 0x00EA: return 'e';
        case 0x00CC: case 0x00CD: case 0x0128: return 'I';
        case 0x00EC: case 0x00ED: case 0x0129: return 'i';
        case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x01A0: return 'O';
        case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x01A1: return 'o';
        case 0x00D9: case 0x00DA: case 0x0168: case 0x01AF: return 'U';
        case 0x00F9: case 0x00FA: case 0x0169: case 0x01B0: return 'u';
        case 0x00DD: return 'Y';
        case 0x00FD: return 'y';
        case 0x0110: return 'D';
        case 0x0111: return 'd';
        default: return 0;
    }
}
}

String tftSafeText(const String& input) {
    String output;
    output.reserve(input.length());
    for (size_t index = 0; index < input.length();) {
        const uint8_t first = static_cast<uint8_t>(input[index]);
        if (first < 0x80) {
            output += static_cast<char>(first);
            ++index;
            continue;
        }

        uint32_t codepoint = 0;
        size_t length = 0;
        if ((first & 0xE0) == 0xC0 && index + 1 < input.length()) {
            codepoint = ((first & 0x1F) << 6) |
                        (static_cast<uint8_t>(input[index + 1]) & 0x3F);
            length = 2;
        } else if ((first & 0xF0) == 0xE0 && index + 2 < input.length()) {
            codepoint = ((first & 0x0F) << 12) |
                        ((static_cast<uint8_t>(input[index + 1]) & 0x3F) << 6) |
                        (static_cast<uint8_t>(input[index + 2]) & 0x3F);
            length = 3;
        } else {
            output += '?';
            ++index;
            continue;
        }

        const char ascii = vietnameseAscii(codepoint);
        output += ascii ? ascii : '?';
        index += length;
    }
    return output;
}
