#include "ota_manager.h"
#include "config.h"
#include "globals.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <mbedtls/base64.h>
#include <mbedtls/ecp.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/md.h>
#include <mbedtls/bignum.h>
#include <cstring>
#include <cstdlib>
#include <atomic>

namespace {
constexpr const char *IMAGE_FILE = "/ogota.part";
constexpr const char *META_FILE = "/ogota.meta";
constexpr size_t CHUNK_MAX = 96;
struct Session {
    String source, tid, sha, signature, canonical;
    uint32_t build=0, size=0;
    bool begin=false, verified=false, flashed=false;
} s;
uint32_t rebootAt=0;
// Maintenance HTTP and LoRa live in independent FreeRTOS tasks; never allow two
// firmware writers or metadata transactions to race one another.
std::atomic_flag otaWriteBusy=ATOMIC_FLAG_INIT;
struct ScopedOtaWriter {
    bool acquired=false;
    ScopedOtaWriter() : acquired(!otaWriteBusy.test_and_set(std::memory_order_acquire)) {}
    ~ScopedOtaWriter() { if(acquired) otaWriteBusy.clear(std::memory_order_release); }
};

bool digits(const String &v) {
    if (v.isEmpty() || v.length()>10) return false;
    for(size_t i=0;i<v.length();++i) if(v[i]<'0'||v[i]>'9') return false;
    return true;
}
bool number(const String &v,uint32_t &value) {
    if(!digits(v)) return false;
    unsigned long long n=strtoull(v.c_str(),nullptr,10);
    if(n>0xFFFFFFFFULL) return false;
    value=static_cast<uint32_t>(n); return true;
}
bool hexString(const String &v,size_t length) {
    if(v.length()!=length) return false;
    for(size_t i=0;i<length;++i) if(!isxdigit(static_cast<unsigned char>(v[i]))) return false;
    return true;
}
bool hexToBytes(const String &src,uint8_t *dst,size_t count) {
    if(!hexString(src,count*2)) return false;
    for(size_t i=0;i<count;++i) {
       char pair[3]={src[i*2],src[i*2+1],0}; dst[i]=strtoul(pair,nullptr,16);
    }
    return true;
}
String shaFile() {
    File file=LittleFS.open(IMAGE_FILE,"r");
    if(!file) return "";
    mbedtls_md_context_t ctx; mbedtls_md_init(&ctx);
    const mbedtls_md_info_t *info=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if(!info || mbedtls_md_setup(&ctx,info,0)!=0 || mbedtls_md_starts(&ctx)!=0) {
       file.close();mbedtls_md_free(&ctx);return "";
    }
    uint8_t buf[512];
    while(file.available()) {
        size_t n=file.read(buf,sizeof(buf));
        if(n==0 || mbedtls_md_update(&ctx,buf,n)!=0) {file.close();mbedtls_md_free(&ctx);return "";}
    }
    uint8_t sum[32]={}; int ok=mbedtls_md_finish(&ctx,sum);
    mbedtls_md_free(&ctx);file.close();
    if(ok!=0) return "";
    char str[65]={};for(int i=0;i<32;++i) snprintf(str+i*2,3,"%02x",sum[i]);
    return String(str);
}
// ECDSA P-256 raw signature (r||s, exactly 64 bytes), SHA256 over canonical
// manifest. Only the public point is provisioned to the device; signing private
// key remains OFF device, never in this project/source ZIP.
bool verifySignature(const String &canonical,const String &signature) {
    if(signature.length()!=88) return false;
    uint8_t sig[80]={};size_t got=0;
    if(mbedtls_base64_decode(sig,sizeof(sig),&got,
        reinterpret_cast<const unsigned char*>(signature.c_str()),signature.length())!=0 || got!=64) return false;
    uint8_t pub[65];
    if(!hexToBytes(String(OG_OTA_PUBLIC_KEY_HEX),pub,65) || pub[0]!=0x04) return false;
    uint8_t hash[32];
    const auto *info=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if(!info || mbedtls_md(info,reinterpret_cast<const unsigned char*>(canonical.c_str()),
                             canonical.length(),hash)!=0) return false;
    mbedtls_ecp_group grp;mbedtls_ecp_point Q;mbedtls_mpi r,t;
    mbedtls_ecp_group_init(&grp);mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&r);mbedtls_mpi_init(&t);
    bool ok=mbedtls_ecp_group_load(&grp,MBEDTLS_ECP_DP_SECP256R1)==0 &&
        mbedtls_ecp_point_read_binary(&grp,&Q,pub,sizeof(pub))==0 &&
        mbedtls_mpi_read_binary(&r,sig,32)==0 &&
        mbedtls_mpi_read_binary(&t,sig+32,32)==0 &&
        mbedtls_ecdsa_verify(&grp,hash,sizeof(hash),&Q,&r,&t)==0;
    mbedtls_mpi_free(&r);mbedtls_mpi_free(&t);
    mbedtls_ecp_point_free(&Q);mbedtls_ecp_group_free(&grp);
    return ok;
}
uint32_t highestBootedBuild() {
    Preferences p;if(!p.begin("ogfw",true)) return OG_FIRMWARE_BUILD;
    uint32_t result=p.getUInt("highest",OG_FIRMWARE_BUILD);p.end();
    return result>OG_FIRMWARE_BUILD ? result : OG_FIRMWARE_BUILD;
}
void reply(String &status,uint32_t &offset,const char *sval,uint32_t off=0) {status=sval;offset=off;}
uint32_t currentSize() {
    File f=LittleFS.open(IMAGE_FILE,"r");if(!f) return 0;
    uint32_t n=f.size();f.close();return n;
}
bool safeToFlash() {
    // No firmware write during active/fire-watch or recent fire events. Also
    // reject low batteries; solar power loss during Update may strand a node.
    if(gCurrentFireLevel>=FIRE_WATCH) return false;
    if(gLastFireWatchMs && uint32_t(millis()-gLastFireWatchMs)<OG_OTA_QUIET_MS) return false;
    return gBatteryPct>=OG_OTA_MIN_BATTERY_PCT;
}
}
namespace OtaManager {
bool handle(const String f[8],String &status,uint32_t &offset) {
    status="INVALID";offset=0;
    ScopedOtaWriter lock;
    if(!lock.acquired) {status="OTA_BUSY";return true;}
    if(f[0]!="OGU1" || f[2]!=String(OG_CONTROLLER_ID) || f[3]!=myTowerID ||
       !hexString(f[4],8)) return false;
    const String &kind=f[1];
    if(kind=="Q") {
       if(!s.verified || f[4]!=s.tid) {
           status="CURRENT_"+String(OG_FIRMWARE_BUILD);return true;
       }
       reply(status,offset,s.flashed?"FLASHED":"NEXT",currentSize());return true;
    }
    if(kind=="B") {
       const int comma=f[6].indexOf(',');uint32_t build=0,size=0;
       if(comma<0 || !number(f[5],build) || !number(f[6].substring(0,comma),size) ||
          !hexString(f[6].substring(comma+1),64) || size==0 ||
          size>OG_OTA_MAX_IMAGE_BYTES || build<=highestBootedBuild()) {
          reply(status,offset,"BAD_MANIFEST");return true;
       }
       const String candidateSha=f[6].substring(comma+1);
       if(s.flashed && s.tid==f[4] && s.build==build && s.size==size &&
          s.sha.equalsIgnoreCase(candidateSha)) {
          reply(status,offset,"ALREADY_FLASHED",s.size);return true;
       }
       s=Session();s.source=f[2];s.tid=f[4];s.build=build;s.size=size;
       s.sha=f[6].substring(comma+1);s.sha.toLowerCase();
       s.canonical="OGOTA1|"+myTowerID+"|"+s.tid+"|"+String(build)+"|"+String(size)+"|"+s.sha;
       s.begin=true;reply(status,offset,"NEED_SIG");return true;
    }
    if(kind=="S") {
       if(!s.begin || s.tid!=f[4] || f[5]!="0" || !verifySignature(s.canonical,f[6])) {
          reply(status,offset,"BAD_SIGNATURE");return true;
       }
       s.signature=f[6];
       const String meta=s.canonical+"\n"+s.signature+"\n";
       File old=LittleFS.open(META_FILE,"r");
       const String previous=old?old.readString():"";if(old) old.close();
       if(previous!=meta) {
          // Old incomplete OTA only; NEVER erase telemetry, fire events or logs.
          LittleFS.remove(IMAGE_FILE);
          LittleFS.remove(META_FILE);
          if(LittleFS.totalBytes()-LittleFS.usedBytes() < s.size+OG_OTA_FREE_RESERVE_BYTES) {
             reply(status,offset,"NO_SPACE");return true;
          }
          File created=LittleFS.open(META_FILE,"w");
          if(!created || created.print(meta)!=meta.length()) {
             if(created) created.close();reply(status,offset,"FLASH_ERROR");return true;
          }
          created.flush();created.close();
       }
       if(currentSize()>s.size) {reply(status,offset,"BAD_STAGING");return true;}
       s.verified=true;reply(status,offset,"READY",currentSize());return true;
    }
    if(!s.verified || s.tid!=f[4]) {reply(status,offset,"NOT_READY");return true;}
    if(kind=="D") {
        uint32_t chunkOffset=0;
        if(!number(f[5],chunkOffset) || f[6].length()>128 || f[6].isEmpty()) {
            reply(status,offset,"BAD_CHUNK",currentSize());return true;
        }
        uint8_t bytes[CHUNK_MAX+8]={};size_t n=0;
        if(mbedtls_base64_decode(bytes,sizeof(bytes),&n,
              reinterpret_cast<const unsigned char*>(f[6].c_str()),f[6].length())!=0 ||
           n==0 || n>CHUNK_MAX || chunkOffset+n>s.size) {
            reply(status,offset,"BAD_CHUNK",currentSize());return true;
        }
        uint32_t expected=currentSize();
        if(chunkOffset<expected) {
            // Duplicate of an earlier chunk is accepted ONLY if identical.
            if(chunkOffset+n>expected) {reply(status,offset,"OFFSET",expected);return true;}
            File check=LittleFS.open(IMAGE_FILE,"r");
            if(!check || !check.seek(chunkOffset)) {
                if(check)check.close();reply(status,offset,"FLASH_ERROR",expected);return true;
            }
            bool matches=true;for(size_t i=0;i<n;++i) if(check.read()!=bytes[i]) matches=false;
            check.close();reply(status,offset,matches?"NEXT":"BAD_REPLAY",expected);return true;
        }
        if(chunkOffset!=expected) {reply(status,offset,"OFFSET",expected);return true;}
        File out=LittleFS.open(IMAGE_FILE,"a");
        if(!out || out.write(bytes,n)!=n) {
           if(out)out.close();reply(status,offset,"FLASH_ERROR",expected);return true;
        }
        out.flush();out.close();
        reply(status,offset,"NEXT",currentSize());return true;
    }
    if(kind=="E") {
       if(s.flashed) {reply(status,offset,"FLASHED",s.size);return true;}
       if(currentSize()!=s.size) {reply(status,offset,"INCOMPLETE",currentSize());return true;}
       if(!safeToFlash()) {reply(status,offset,"UNSAFE_POWER_OR_ALARM",currentSize());return true;}
       if(shaFile()!=s.sha) {reply(status,offset,"BAD_HASH",currentSize());return true;}
       // Signature already verified over target + transfer ID + build + size + SHA256.
       File in=LittleFS.open(IMAGE_FILE,"r");
       if(!in || !Update.begin(s.size,U_FLASH)) {
           if(in)in.close();reply(status,offset,"UPDATE_BEGIN_FAIL");return true;
       }
       size_t written=Update.writeStream(in);in.close();
       if(written!=s.size || !Update.end(false)) {
           Update.abort();reply(status,offset,"UPDATE_WRITE_FAIL");return true;
       }
       s.flashed=true;reply(status,offset,"FLASHED",s.size);return true;
    }
    if(kind=="X") {
        if(!s.flashed) {reply(status,offset,"NOT_FLASHED",currentSize());return true;}
        reply(status,offset,"REBOOTING",s.size);
        rebootAt=millis()+10000UL;return true;
    }
    return false;
}
void tick() {
    if(rebootAt && static_cast<int32_t>(millis()-rebootAt)>=0) ESP.restart();
}
void confirmBootIfHealthy() {
    static bool confirmed=false;
    if(confirmed) return;
    confirmed=true;
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    // Called after flash/logging/radio/sensor init. If these fail the bootloader
    // can revert to its former OTA partition instead of confirming a broken app.
    esp_ota_mark_app_valid_cancel_rollback();
#endif
    Preferences p;
    if(p.begin("ogfw",false)) {
        if(p.getUInt("highest",0)<OG_FIRMWARE_BUILD) p.putUInt("highest",OG_FIRMWARE_BUILD);
        p.end();
    }
    // Do not retain an obsolete 1MiB transfer on the same flash as black-box
    // fire records after successful boot into the new signed build.
    File meta=LittleFS.open(META_FILE,"r");
    const String canonical=meta?meta.readStringUntil('\n'):"";
    if(meta)meta.close();
    int lastSep=canonical.lastIndexOf('|');
    int before=lastSep>0?canonical.lastIndexOf('|',lastSep-1):-1;
    int earlier=before>0?canonical.lastIndexOf('|',before-1):-1;
    if(earlier>=0 && before>earlier) {
        const uint32_t stagedBuild=strtoul(canonical.substring(earlier+1,before).c_str(),nullptr,10);
        if(stagedBuild && stagedBuild<=OG_FIRMWARE_BUILD) {
            LittleFS.remove(IMAGE_FILE);LittleFS.remove(META_FILE);
        }
    }
}
}
