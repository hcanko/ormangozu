#pragma once
// COPY THIS FILE TO secrets.h ON EACH DEVICE; NEVER COMMIT secrets.h.
#define OG_PROVISIONED 0  // change to 1 ONLY after every placeholder below is replaced
// For the TWO-NODE PILOT both devices must share the same random 32+ char mesh key.
// Production architecture requires per-device provisioning / hardware key storage.
#define OG_MESH_KEY "REPLACE_WITH_SAME_32PLUS_RANDOM_CHARACTER_ALERT_KEY"
// Separate 32+ byte group transport key for target-addressed controls and OTA.
#define OG_CONTROL_KEY "REPLACE_WITH_SEPARATE_32PLUS_RANDOM_CONTROL_KEY"
// E-fuse-derived ID of the ONE USB-connected command bridge authorised by this node.
#define OG_CONTROLLER_ID "NEST-001122AABBCC"
// Uncompressed P-256 point: 04 followed by X (32 bytes) and Y (32 bytes), hex.
// tools/lora_ota.py keygen generates this; never embed its private half on Nest.
#define OG_OTA_PUBLIC_KEY_HEX "REPLACE_WITH_THE_130_HEX_CHARACTER_P256_PUBLIC_POINT"
#define OG_MAINT_AP_PASSWORD "REPLACE_WITH_UNIQUE_AP_PASSWORD"
#define OG_MAINT_HTTP_PASSWORD "REPLACE_WITH_UNIQUE_HTTP_PASSWORD"
#define OG_OTA_PASSWORD "REPLACE_WITH_UNIQUE_OTA_PASSWORD"
