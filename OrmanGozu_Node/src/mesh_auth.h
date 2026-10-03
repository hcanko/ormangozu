#pragma once
#include <Arduino.h>
namespace MeshAuth {
    // HMAC-SHA256 truncated to 96 bits. Do NOT confuse this with end-to-end encryption.
    String tag(const String &body);
    bool verifyTag(const String &body, const String &receivedHex);
    // Two-Nest pilot: replay state is persisted for ONE registered peer and packet type.
    // Duplicate authenticated packets may be ACKed but must not run a second scan.
    bool freshCommandAndRemember(const String &source, uint32_t senderBoot, uint32_t sequence);
    bool freshAndRemember(const String &source, uint32_t sourceBoot, uint32_t originBoot,
                          uint32_t sequence, const String &type);
}
