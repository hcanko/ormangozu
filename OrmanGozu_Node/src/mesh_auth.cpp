#include "mesh_auth.h"
#include "config.h"
#include <Preferences.h>
#include <mbedtls/md.h>
#include <cstring>
#include <cstdio>

namespace {
constexpr size_t MAC_BYTES = 12;
static_assert(sizeof(OG_MESH_KEY) >= 33, "Use a 32+ char alert key");
static_assert(sizeof(OG_CONTROL_KEY) >= 33, "Use a separate 32+ char control key");

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool digest(const String &body, uint8_t out[32]) {
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == nullptr) return false;
    const char *key = (body.startsWith("OGC2|") || body.startsWith("OGU1|"))
        ? OG_CONTROL_KEY : OG_MESH_KEY;
    return mbedtls_md_hmac(info,
          reinterpret_cast<const unsigned char *>(key), strlen(key),
          reinterpret_cast<const unsigned char *>(body.c_str()), body.length(), out) == 0;
}

const char *typePrefix(const String &type) {
    if (type == "ALERT") return "a";
    if (type == "ACK") return "k";
    if (type == "REPORT") return "r";
    if (type == "CONFIRM") return "c";
    return nullptr; // fail closed on unknown remote commands
}
} // namespace

namespace MeshAuth {
bool freshCommandAndRemember(const String &source, uint32_t senderBoot, uint32_t sequence) {
    if (source.isEmpty() || senderBoot == 0 || sequence == 0) return false;
    if(source != String(OG_CONTROLLER_ID)) return false;
    Preferences prefs;
    if (!prefs.begin("ogcmd", false)) return false;
    const uint32_t previousBoot = prefs.getUInt("boot", 0);
    const uint32_t previousSeq = prefs.getUInt("seq", 0);
    const bool fresh = senderBoot > previousBoot ||
        (senderBoot == previousBoot && sequence > previousSeq);
    if (!fresh) { prefs.end(); return false; }
    const bool okBoot = senderBoot == previousBoot ||
        prefs.putUInt("boot", senderBoot) == sizeof(uint32_t);
    const bool okSeq = prefs.putUInt("seq", sequence) == sizeof(uint32_t);
    prefs.end();
    return okBoot && okSeq;
}
String tag(const String &body) {
    uint8_t hash[32] = {};
    if (!digest(body, hash)) return "";
    char hex[MAC_BYTES * 2 + 1] = {};
    for (size_t i = 0; i < MAC_BYTES; ++i) snprintf(hex + i * 2, 3, "%02x", hash[i]);
    return String(hex);
}

bool verifyTag(const String &body, const String &receivedHex) {
    if (receivedHex.length() != MAC_BYTES * 2) return false;
    uint8_t expected[32] = {};
    if (!digest(body, expected)) return false;
    uint8_t difference = 0;
    for (size_t i = 0; i < MAC_BYTES; ++i) {
        const int hi = hexNibble(receivedHex[i * 2]);
        const int lo = hexNibble(receivedHex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        difference |= expected[i] ^ static_cast<uint8_t>((hi << 4) | lo);
    }
    return difference == 0;
}

bool freshAndRemember(const String &source, uint32_t sourceBoot, uint32_t originBoot,
                      uint32_t sequence, const String &type) {
    const char *prefix = typePrefix(type);
    if (!prefix || source.isEmpty() || sourceBoot == 0 || originBoot == 0 || sequence == 0) return false;
    Preferences prefs;
    // Per-source NVS namespaces avoid the previous hard-coded ONE-peer lock.
    // Device identity is authenticated by its MAC before this method is called.
    if (source.length()!=17 || !source.startsWith("NEST-")) return false;
    const String ns = "p" + source.substring(5); // <= 15-char NVS namespace
    if (!prefs.begin(ns.c_str(), false)) return false;
    String bootKey = String(prefix) + "boot";
    String originKey = String(prefix) + "origin";
    String seqKey = String(prefix) + "seq";
    uint32_t previousBoot = prefs.getUInt(bootKey.c_str(), 0);
    uint32_t previousOrigin = prefs.getUInt(originKey.c_str(), 0);
    uint32_t previousSequence = prefs.getUInt(seqKey.c_str(), 0);
    // REPORT echoes the originator's sequence, which can restart from 1 when
    // that OTHER Nest reboots. Include both source and origin boot in ordering.
    bool fresh = sourceBoot > previousBoot ||
                 (sourceBoot == previousBoot && originBoot > previousOrigin) ||
                 (sourceBoot == previousBoot && originBoot == previousOrigin &&
                  sequence > previousSequence);
    if (fresh) {
        // A returned ACK must never be sent as though durable state had been recorded
        // if NVS writes failed. These are low-frequency event packets, not telemetry.
        bool okBoot = sourceBoot == previousBoot || prefs.putUInt(bootKey.c_str(), sourceBoot) == sizeof(uint32_t);
        bool okOrigin = prefs.putUInt(originKey.c_str(), originBoot) == sizeof(uint32_t);
        bool okSeq = prefs.putUInt(seqKey.c_str(), sequence) == sizeof(uint32_t);
        fresh = okBoot && okOrigin && okSeq;
    }
    prefs.end();
    return fresh;
}
} // namespace MeshAuth
