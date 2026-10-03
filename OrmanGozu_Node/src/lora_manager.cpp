#include "lora_manager.h"
#include "sensor_manager.h"
#include "storage_manager.h"
#include "mesh_auth.h"
#include "ota_manager.h"
#include <ctype.h>
#include <math.h>

#include <RadioLib.h>
#include <cstring>

namespace {
SX1262 radio = new Module(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_NRST_PIN, LORA_BUSY_PIN);
volatile bool radioIrqFlag = false;
volatile bool radioBusy = false;
bool radioReady = false;
TaskHandle_t loraTaskHandle = nullptr;
volatile uint32_t waitingAckSequence = 0;
volatile uint32_t waitingAckOriginBoot = 0;
String waitingAckSource = "";
volatile bool waitingAckReceived = false;
uint32_t pcControlSequence = 0;
QueueHandle_t otaTxQueue = nullptr;
uint32_t lastOtaTxMs = 0;
uint32_t otaRequiredGapMs = OG_OTA_NODE_TX_GAP_MS;
uint32_t lastOtherTxMs = 0;
String lastControlResultPayload;
String lastControlResultSource;
uint32_t lastControlResultSequence = 0;
struct RelaySeen { uint32_t hash=0, at=0; };
RelaySeen relaySeen[32];
uint8_t relayPointer=0;

struct PendingControl {
    bool active = false;
    uint32_t id = 0;
    uint32_t sequence = 0;
    uint32_t deadline = 0;
    String target;
    String opcode;
};
PendingControl pending;

bool validOpcode(const String &op) {
    return op == "STATUS" || op == "SAMPLE" || op == "FAST" ||
        op == "MANUAL" || op == "PAN" || op == "TILT" ||
        op == "HOME" || op == "AUTO";
}

void emitControl(uint32_t id, const String &target, const String &opcode,
                 const char *stage, const String &status, float maxT = 0,
                 float score = 0, int battery = -1, int pan = 90, int tilt = 90,
                 bool manual = false, int health = -1) {
    Serial.printf("OGCTRL:{\"device_id\":\"%s\",\"command_id\":%lu,\"target_id\":\"%s\","
                  "\"opcode\":\"%s\",\"stage\":\"%s\",\"status\":\"%s\","
                  "\"max_temp\":%.1f,\"score\":%.1f,\"battery_pct\":%d,\"pan\":%d,"
                  "\"tilt\":%d,\"manual\":%s,\"health\":%d}\n",
       myTowerID.c_str(), static_cast<unsigned long>(id), target.c_str(), opcode.c_str(),
       stage, status.c_str(), maxT, score, battery, pan, tilt, manual ? "true" : "false", health);
}

void IRAM_ATTR onDio1() {
    radioIrqFlag = true;
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    if (loraTaskHandle != nullptr) {
        vTaskNotifyGiveFromISR(loraTaskHandle, &higherPriorityTaskWoken);
    }
    if (higherPriorityTaskWoken == pdTRUE) portYIELD_FROM_ISR();
}

// Pilot peer IDs are derived from the ESP efuse MAC; reject signed but malformed
// peer strings before logging them as JSON or executing a wake event.
bool validTowerId(const String &id) {
    if (id.length() != 17 || !id.startsWith("NEST-")) return false;
    for (size_t i = 5; i < id.length(); ++i) {
        const char c = id[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

int pipeCount(const String &message) {
    int count=0;
    for(size_t i=0;i<message.length();++i) if(message[i]=='|') ++count;
    return count;
}

int splitMessage(const String &message, String fields[], int maxFields) {
    int count = 0;
    int start = 0;
    while (count < maxFields) {
        const int sep = message.indexOf('|', start);
        if (sep < 0) {
            fields[count++] = message.substring(start);
            break;
        }
        fields[count++] = message.substring(start, sep);
        start = sep + 1;
    }
    return count;
}

String buildMessage(const char *type, const String &dst, uint32_t originBoot, uint32_t seq,
                    uint8_t fireLevel, uint8_t healthLevel, float score,
                    float maxTemp, uint16_t largestCluster,
                    uint8_t persistence, float gasDrop, int battery) {
    String p;
    p.reserve(220);
    p += "OG4|";
    p += type;
    p += "|" + myTowerID;
    p += "|" + String(myBootCounter);
    p += "|" + dst;
    p += "|" + String(originBoot);
    p += "|" + String(seq);
    p += "|" + String(fireLevel);
    p += "|" + String(healthLevel);
    p += "|" + String(score, 1);
    p += "|" + String(maxTemp, 1);
    p += "|" + String(largestCluster);
    p += "|" + String(persistence);
    p += "|" + String(gasDrop, 1);
    p += "|" + String(battery);
    p += "|0";  // authenticated bounded relay hop counter (v0.7; old OG4 incompatible)
    return p + "|" + MeshAuth::tag(p);
}

String signedControl(const String &body) { return body + "|" + MeshAuth::tag(body); }

bool startContinuousReceive() {
    if (!radioReady) return false;
    radioIrqFlag = false;
    const int16_t state = radio.startReceive();
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("❌ LoRa RX başlatma hatası: %d\n", state);
        return false;
    }
    return true;
}

// Semtech LoRa airtime formula. Approximate with explicit header, CRC enabled,
// SF/BW/CR config. Pilot maintenance traffic is limited to <=~0.9% theoretical
// airtime; check the actual lawful band and other transmission classes too.
uint32_t otaGapMs(size_t bytes) {
    const float sf=static_cast<float>(LORA_SPREADING_FACTOR);
    const float tsym=powf(2.0f,sf)/(LORA_BANDWIDTH_KHZ*1000.0f);
    const float de=(LORA_SPREADING_FACTOR>=11 && LORA_BANDWIDTH_KHZ<=125.0f)?1.0f:0.0f;
    const float units=ceilf((8.0f*bytes-4.0f*sf+28.0f+16.0f)/(4.0f*(sf-2.0f*de)));
    const float symbols=8.0f+fmaxf(units,0.0f)*static_cast<float>(LORA_CODING_RATE);
    const float toaMs=(LORA_PREAMBLE_SYMBOLS+4.25f+symbols)*tsym*1000.0f;
    return static_cast<uint32_t>(fmaxf(OG_OTA_NODE_TX_GAP_MS,ceilf(toaMs*115.0f)));
}

bool transmitOnce(const String &payload) {
    if (!radioReady) return false;
    radioBusy = true;
    xEventGroupSetBits(systemEvents, EVENT_LORA_BUSY);
    radio.standby();
    radioIrqFlag = false;
    // RadioLib 6.6 takes a mutable Arduino String even though it does not need
    // to change the caller's payload.
    String wirePayload(payload);
    const int16_t state = radio.transmit(wirePayload);
    const bool ok = state == RADIOLIB_ERR_NONE;
    if (!ok) Serial.printf("❌ LoRa TX hatası: %d\n", state);
    if(ok && !payload.startsWith("OGU1|")) lastOtherTxMs=millis();
    radioIrqFlag = false;  // TX-done IRQ'sini RX paketi sanma.
    startContinuousReceive();
    radioBusy = false;
    xEventGroupClearBits(systemEvents, EVENT_LORA_BUSY);
    return ok;
}

bool fillTxMessage(LoraTxMessage &msg, const char *type, const String &destination,
                   uint32_t originBoot, uint32_t sequence, bool requireAck, const String &payload) {
    // Never silently truncate a signed LoRa frame (which invalidates the HMAC).
    if (payload.isEmpty() || payload.length() >= sizeof(msg.payload)) {
        Serial.println("[LORA] packet too large/unsigned; discarded");
        return false;
    }
    memset(&msg, 0, sizeof(msg));
    msg.sequence = sequence;
    msg.origin_boot = originBoot;
    msg.require_ack = requireAck;
    payload.toCharArray(msg.payload, sizeof(msg.payload));
    strncpy(msg.type, type, sizeof(msg.type) - 1);
    destination.toCharArray(msg.destination, sizeof(msg.destination));
    return true;
}


// Bounded two-hop application relay. Authentication is verified BEFORE this
// function is called. Relays re-MAC the incremented hop count, use a short
// duplicate cache and are deliberately lower priority than wildfire alarms.
// All admitted pilot nodes share OG_CONTROL_KEY; rotate/re-provision after compromise.
bool relayFrame(const String &body, const String &source, const String &destination,
                const String &protocol) {
    if (!validTowerId(source) ||
        (!validTowerId(destination) && !(protocol=="OG4" && destination=="*")) ||
        source==myTowerID || destination==myTowerID || source==destination) return false;
    const int cut=body.lastIndexOf('|');
    if(cut<0) return false;
    const String hopString=body.substring(cut+1);
    if(hopString.length()!=1 || hopString[0]<'0' || hopString[0]>'2') return false;
    const int hop=hopString[0]-'0';
    if(hop>=OG_MAX_RELAY_HOPS || (destination=="*" && hop>=1)) return false;
    // Hash authenticated content excluding hop, keeping retry possible after
    // a brief duplicate suppression window. No indefinite TTL poisoning.
    const String identity=body.substring(0,cut);
    uint32_t h=2166136261UL;
    for(size_t i=0;i<identity.length();++i) h=(h^uint8_t(identity[i]))*16777619UL;
    for(auto &entry:relaySeen) {
       if(entry.hash==h && uint32_t(millis()-entry.at)<1700UL) return false;
    }
    relaySeen[relayPointer].hash = h;
    relaySeen[relayPointer].at = millis();
    relayPointer=(relayPointer+1)%32;
    String forwarded=identity+"|"+String(hop+1);
    String payload=forwarded+"|"+MeshAuth::tag(forwarded);
    LoraTxMessage tx={};
    if(!fillTxMessage(tx, protocol=="OGU1"?"OTA_RELAY":(protocol=="OG4"?"ALERT_RELAY":"CTRL_RELAY"),
                      destination,0,0,false,payload)) return false;
    QueueHandle_t queue=(protocol=="OGU1")?otaTxQueue:loraTxQueue;
    return queue && xQueueSend(queue,&tx,0)==pdPASS;
}

void sendOtaResponse(const String &dst,const String &tid,uint32_t offset,const String &status) {
    const String body="OGU1|R|"+myTowerID+"|"+dst+"|"+tid+"|"+String(offset)+"|"+status+"|0";
    LoraTxMessage tx={};
    const String payload= signedControl(body);
    if(fillTxMessage(tx,"OTA_R",dst,0,0,false,payload) && otaTxQueue)
       xQueueSend(otaTxQueue,&tx,0);
}

// Authenticated remote OTA is separate from OG4 alarm messages and OGC2 controls.
void processOtaFrame(const String &body,float rssi,float snr) {
    String f[8];
    if(pipeCount(body)!=7 || splitMessage(body,f,8)!=8 || f[0]!="OGU1" ||
       !validTowerId(f[2]) || !validTowerId(f[3]) || f[2]==myTowerID ||
       f[4].length()!=8 || f[7].length()!=1 || f[7][0]<'0' || f[7][0]>'2') return;
    for(size_t i=0;i<f[4].length();++i) if(!isxdigit((unsigned char)f[4][i])) return;
    const String kind=f[1];
    if(kind!="B" && kind!="S" && kind!="D" && kind!="E" &&
       kind!="X" && kind!="Q" && kind!="R") return;
    if(f[3]!=myTowerID) {
       relayFrame(body,f[2],f[3],"OGU1");return;
    }
    if(kind=="R") {
       // The signed response must be addressed to THIS USB-connected controller;
       // it contains no executable side effects and is correlated by PC tid.
       if(f[5].length()>10 || f[6].length()>32) return;
       Serial.printf("OGOTA:{\"device_id\":\"%s\",\"source_device_id\":\"%s\","
                     "\"tid\":\"%s\",\"offset\":%lu,\"status\":\"%s\","
                     "\"rssi\":%.1f,\"snr\":%.1f}\n",
             myTowerID.c_str(),f[2].c_str(),f[4].c_str(),
             static_cast<unsigned long>(strtoul(f[5].c_str(),nullptr,10)),
             f[6].c_str(),rssi,snr);
       return;
    }
    String status;uint32_t offset=0;
    if(!OtaManager::handle(f,status,offset)) return;
    sendOtaResponse(f[2],f[4],offset,status);
    StorageManager::logLoraEvent("RX","OTA_"+kind,f[2],offset,rssi,snr,status);
}

void sendAck(const String &destination, uint32_t originBoot, uint32_t seq, int battery = 0) {
    delay(random(25, 110));
    const String ack = buildMessage("ACK", destination, originBoot, seq, FIRE_NORMAL, HEALTH_OK,
                                    0.0f, 0.0f, 0, 0, 0.0f, battery);
    const bool ok = transmitOnce(ack);
    if (ok) Serial.printf("OGMESH:{\"device_id\":\"%s\",\"peer\":\"%s\",\"direction\":\"TX\",\"type\":\"ACK\",\"boot\":%lu,\"origin_boot\":%lu,\"sequence\":%lu}\n",
       myTowerID.c_str(), destination.c_str(), static_cast<unsigned long>(myBootCounter),
       static_cast<unsigned long>(originBoot), static_cast<unsigned long>(seq));
    StorageManager::logLoraEvent("TX", "ACK", destination, seq, 0, 0, ok ? "ok" : "tx_error");
}

// OGC2 is a separate HMAC-authenticated small control envelope. It is NOT OTA.
// The command is addressed to one Nest; peer sensing remains autonomous.
void processControlFrame(const String &body, float rssi, float snr) {
    String f[17];
    const int pipes=pipeCount(body);
    const int count = splitMessage(body, f, 17);
    if ((f[1]=="CMD" && pipes!=8) || (f[1]=="RESULT" && pipes!=16)) return;
    if (count < 9 || f[0] != "OGC2" || !validTowerId(f[2]) ||
        !validTowerId(f[4]) || f[2] == myTowerID) return;
    if (f[4] != myTowerID) { relayFrame(body,f[2],f[4],"OGC2"); return; }
    if ((f[1]=="CMD" && f[2]!=String(OG_CONTROLLER_ID))) return;
    const String type = f[1];
    const String src = f[2];
    const uint32_t srcBoot = static_cast<uint32_t>(strtoul(f[3].c_str(), nullptr, 10));
    const uint32_t seq = static_cast<uint32_t>(strtoul(f[type == "CMD" ? 5 : 6].c_str(), nullptr, 10));
    if (srcBoot == 0 || seq == 0) return;
    if (type == "CMD") {
        if (count != 9 || f[8].length()!=1 || !validOpcode(f[6])) return;
        char *end = nullptr;
        const long arg = strtol(f[7].c_str(), &end, 10);
        if (end == f[7].c_str() || *end != '\0' || arg < -32768 || arg > 32767) return;
        // Replay state must be persisted before an actuator action is admitted.
        const bool fresh = MeshAuth::freshCommandAndRemember(src, srcBoot, seq);
        // Direct ACK is still useful but the OGC2 RESULT will travel through
        // two-hop relay independently. ACKs are not used as proof of execution.
        sendAck(src, srcBoot, seq);
        if (!fresh) {
            if(lastControlResultSequence==seq && lastControlResultSource==src &&
               lastControlResultPayload.length()) {
                LoraTxMessage retry={};
                if(fillTxMessage(retry,"RESULT",src,srcBoot,seq,false,lastControlResultPayload))
                    xQueueSend(loraTxQueue,&retry,0);
            }
            StorageManager::logLoraEvent("RX", "CTRL_DUP", src, seq, rssi, snr, "ignored");
            return;
        }
        ControlRequest request = {};
        src.toCharArray(request.source, sizeof(request.source));
        request.source_boot = srcBoot;
        request.sequence = seq;
        f[6].toCharArray(request.opcode, sizeof(request.opcode));
        request.argument = static_cast<int16_t>(arg);
        if (!SensorManager::enqueueControlRequest(request)) {
            // Queue saturation does not silently turn into a successful remote action.
            const String result = "OGC2|RESULT|" + myTowerID + "|" + String(myBootCounter) +
                "|" + src + "|" + String(srcBoot) + "|" + String(seq) + "|" + f[6] +
                "|BUSY|0|0|-1|90|90|0|-1|0";
            LoraTxMessage msg = {};
            const String payload = signedControl(result);
            if (fillTxMessage(msg, "RESULT", src, srcBoot, seq, true, payload))
                xQueueSend(loraTxQueue, &msg, 0);
        }
        StorageManager::logLoraEvent("RX", "CTRL_CMD", src, seq, rssi, snr, f[6]);
        return;
    }
    if (type == "RESULT") {
        if (count != 17 || f[16].length()!=1 || static_cast<uint32_t>(strtoul(f[5].c_str(), nullptr, 10)) != myBootCounter ||
            !validOpcode(f[7]) || !pending.active || pending.target != src ||
            pending.sequence != seq || pending.opcode != f[7]) return;
        // RESULT is correlated to one outstanding PC request; never executes a command.
        sendAck(src, myBootCounter, seq);
        const bool manual = f[14] == "1";
        emitControl(pending.id, src, f[7], "RESULT", f[8], f[9].toFloat(),
                    f[10].toFloat(), f[11].toInt(), f[12].toInt(), f[13].toInt(), manual, f[15].toInt());
        StorageManager::logLoraEvent("RX", "CTRL_RESULT", src, seq, rssi, snr, f[8]);
        pending.active = false;
    }
}

PeerEvent makePeerEvent(uint8_t type, const String &source, uint32_t originBoot, uint32_t seq,
                        uint8_t fireLevel, uint8_t healthLevel, float score,
                        float maxTemp, uint16_t cluster, uint8_t persistence,
                        float gasDrop, float rssi, float snr) {
    PeerEvent event = {};
    event.type = type;
    source.toCharArray(event.source, sizeof(event.source));
    event.sequence = seq;
    event.origin_boot = originBoot;
    event.fire_level = fireLevel;
    event.health_level = healthLevel;
    event.fire_score = score;
    event.max_temp = maxTemp;
    event.largest_cluster = cluster;
    event.persistence_count = persistence;
    event.gas_drop_pct = gasDrop;
    event.rssi = rssi;
    event.snr = snr;
    return event;
}

void processIncoming() {
    if (!radioIrqFlag || !radioReady || radioBusy) return;
    radioIrqFlag = false;

    String incoming;
    const int16_t state = radio.readData(incoming);
    const float rssi = radio.getRSSI();
    const float snr = radio.getSNR();

    if (state != RADIOLIB_ERR_NONE) {
        StorageManager::logLoraEvent("RX", "ERROR", "?", 0, rssi, snr, String(state));
        startContinuousReceive();
        return;
    }

    // Authenticate before executing ANY remote command.
    const int lastSeparator = incoming.lastIndexOf('|');
    if (lastSeparator < 0) { startContinuousReceive(); return; }
    const String body = incoming.substring(0, lastSeparator);
    const String mac = incoming.substring(lastSeparator + 1);
    if (!MeshAuth::verifyTag(body, mac)) {
        StorageManager::logLoraEvent("RX", "BAD_MAC", "?", 0, rssi, snr, "drop");
        startContinuousReceive();
        return;
    }
    if (body.startsWith("OGU1|")) {
        processOtaFrame(body,rssi,snr);
        startContinuousReceive();
        return;
    }
    if (body.startsWith("OGC2|")) {
        processControlFrame(body, rssi, snr);
        startContinuousReceive();
        return;
    }
    String f[16];
    const int count = splitMessage(body, f, 16);
    if (count != 16 || pipeCount(body)!=15 || f[0] != "OG4" || f[15].length()!=1 ||
        f[15][0]<'0' || f[15][0]>'2') {
        StorageManager::logLoraEvent("RX", "INVALID", "?", 0, rssi, snr, "bad_fields");
        startContinuousReceive();
        return;
    }
    const String type = f[1];
    const String src = f[2];
    const uint32_t sourceBoot = static_cast<uint32_t>(strtoul(f[3].c_str(), nullptr, 10));
    const String dst = f[4];
    const uint32_t originBoot = static_cast<uint32_t>(strtoul(f[5].c_str(), nullptr, 10));
    const uint32_t seq = static_cast<uint32_t>(strtoul(f[6].c_str(), nullptr, 10));
    const uint8_t fireLevel = static_cast<uint8_t>(f[7].toInt());
    const uint8_t healthLevel = static_cast<uint8_t>(f[8].toInt());
    const float score = f[9].toFloat();
    const float maxTemp = f[10].toFloat();
    const uint16_t largestCluster = static_cast<uint16_t>(f[11].toInt());
    const uint8_t persistence = static_cast<uint8_t>(f[12].toInt());
    const float gasDrop = f[13].toFloat();

    const bool knownType = type == "ALERT" || type == "ACK" ||
                           type == "REPORT" || type == "CONFIRM";
    if (!knownType || !validTowerId(src) ||
        src == myTowerID || (dst != "*" && !validTowerId(dst)) ||
        sourceBoot == 0 || originBoot == 0 || seq == 0) {
        startContinuousReceive();
        return;
    }
    if (dst != "*" && dst != myTowerID) {
        relayFrame(body,src,dst,"OG4");
        startContinuousReceive();
        return;
    }
    // Only a single bounded extra hop of broadcast ALERT to prevent flood storms.
    if (dst=="*" && type=="ALERT") relayFrame(body,src,dst,"OG4");
    // Reports/confirmations must concern an event from the CURRENT boot of this Nest.
    if (type == "REPORT" && originBoot != myBootCounter) {
        startContinuousReceive();
        return;
    }

    // ACK has no executable side effect: correlate with pending event instead of
    // ordering it by the sender's own sequence (it echoes the OTHER device's seq).
    const bool fresh = (type == "ACK") ? true :
        MeshAuth::freshAndRemember(src, sourceBoot, originBoot, seq, type);
    // Repeated authenticated packet is ACKed too; the peer may have missed our first ACK.
    if (type == "ALERT" || type == "REPORT" || type == "CONFIRM") {
        sendAck(src, originBoot, seq);
    }
    if (!fresh) {
        StorageManager::logLoraEvent("RX", "DUP_OR_OLD", src, seq, rssi, snr, type);
        startContinuousReceive();
        return;
    }
    StorageManager::logLoraEvent("RX", type, src, seq, rssi, snr, "verified");
    // The computer can capture these lines over USB without a third LoRa device.
    // device_id identifies the USB-connected observer; source_device_id is the
    // remote Nest whose sensor metrics the authenticated packet describes.
    Serial.printf("OGMESH:{\"device_id\":\"%s\",\"source_device_id\":\"%s\",\"peer\":\"%s\",\"direction\":\"RX\",\"type\":\"%s\",\"boot\":%lu,\"origin_boot\":%lu,\"sequence\":%lu,\"level\":%u,\"health\":%u,\"score\":%.1f,\"max_temp\":%.1f,\"cluster\":%u,\"persistence\":%u,\"gas_drop_pct\":%.1f,\"rssi\":%.1f,\"snr\":%.1f}\n",
            myTowerID.c_str(), src.c_str(), src.c_str(), type.c_str(),
            static_cast<unsigned long>(sourceBoot), static_cast<unsigned long>(originBoot),
            static_cast<unsigned long>(seq), fireLevel, healthLevel, score, maxTemp,
            static_cast<unsigned>(largestCluster), static_cast<unsigned>(persistence),
            gasDrop, rssi, snr);

    if (type == "ACK") {
        const bool sourceMatches = waitingAckSource == "*" || waitingAckSource == src;
        if (waitingAckSequence == seq && waitingAckOriginBoot == originBoot && sourceMatches) {
            waitingAckReceived = true;
        }
    } else if (type == "ALERT") {
        const PeerEvent event = makePeerEvent(
            PEER_EVENT_ALERT, src, originBoot, seq, fireLevel, healthLevel, score,
            maxTemp, largestCluster, persistence, gasDrop, rssi, snr);
        SensorManager::enqueuePeerEvent(event);
    } else if (type == "REPORT") {
        const PeerEvent event = makePeerEvent(
            PEER_EVENT_REPORT, src, originBoot, seq, fireLevel, healthLevel, score,
            maxTemp, largestCluster, persistence, gasDrop, rssi, snr);
        SensorManager::enqueuePeerEvent(event);
    } else if (type == "CONFIRM") {
        const PeerEvent event = makePeerEvent(
            PEER_EVENT_CONFIRMED, src, originBoot, seq, FIRE_NETWORK_CORROBORATED, healthLevel, score,
            maxTemp, largestCluster, persistence, gasDrop, rssi, snr);
        SensorManager::enqueuePeerEvent(event);
    }

    startContinuousReceive();
}

bool sendWithRetries(const LoraTxMessage &msg) {
    waitingAckSequence = msg.require_ack ? msg.sequence : 0;
    waitingAckOriginBoot = msg.require_ack ? msg.origin_boot : 0;
    waitingAckSource = msg.require_ack ? String(msg.destination) : "";
    waitingAckReceived = false;

    for (uint8_t attempt = 0; attempt < LORA_TX_RETRIES; ++attempt) {
        if (attempt > 0) delay(random(LORA_RETRY_BACKOFF_MIN_MS, LORA_RETRY_BACKOFF_MAX_MS));
        const bool sent = transmitOnce(String(msg.payload));
        StorageManager::logLoraEvent("TX", String(msg.type), String(msg.destination), msg.sequence, 0, 0,
                                     sent ? String("attempt=") + String(attempt + 1)
                                          : String("tx_error"));
        if (sent) {
            Serial.printf("OGMESH:{\"device_id\":\"%s\",\"peer\":\"%s\",\"direction\":\"TX\",\"type\":\"%s\",\"boot\":%lu,\"origin_boot\":%lu,\"sequence\":%lu}\n",
                myTowerID.c_str(), msg.destination, msg.type,
                static_cast<unsigned long>(myBootCounter),
                static_cast<unsigned long>(msg.origin_boot),
                static_cast<unsigned long>(msg.sequence));
        }
        if (!sent) continue;
        if (!msg.require_ack) return true;

        const uint32_t deadline = millis() + LORA_ACK_TIMEOUT_MS;
        while (static_cast<int32_t>(deadline - millis()) > 0) {
            if (radioIrqFlag) processIncoming();
            if (waitingAckReceived) {
                waitingAckSequence = 0;
                waitingAckOriginBoot = 0;
                waitingAckSource = "";
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    waitingAckSequence = 0;
    waitingAckOriginBoot = 0;
    waitingAckSource = "";
    StorageManager::logLoraEvent("TX", "NO_ACK", String(msg.destination), msg.sequence, 0, 0,
                                 String(msg.type) + ":retries_exhausted");
    return false;
}
} // namespace

namespace LoraManager {

bool init() {
    // Fail-closed provisioning checks. A shared alert key MUST NOT unlock the
    // higher-privilege control/OTA transport.
    if(strcmp(OG_MESH_KEY,OG_CONTROL_KEY)==0 ||
       strlen(OG_OTA_PUBLIC_KEY_HEX)!=130 || strlen(OG_CONTROLLER_ID)!=17 ||
       !validTowerId(String(OG_CONTROLLER_ID))) {
       Serial.println("[FATAL] Invalid/doubled control keys or signing public key/controller ID");
       return false;
    }
    Serial.println("[CORE 1] LoRa sürekli dinleme başlatılıyor (OG4)...");
    const int16_t state = radio.begin(
        LORA_FREQUENCY_MHZ,
        LORA_BANDWIDTH_KHZ,
        LORA_SPREADING_FACTOR,
        LORA_CODING_RATE,
        LORA_SYNC_WORD,
        LORA_TX_POWER_DBM,
        LORA_PREAMBLE_SYMBOLS,
        LORA_TCXO_VOLTAGE
    );
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("❌ LoRa başlatma hatası: %d\n", state);
        return false;
    }

    radio.setCRC(2);
    otaTxQueue=xQueueCreate(10,sizeof(LoraTxMessage));
    if(!otaTxQueue) return false;
    radio.setDio1Action(onDio1);
    radioReady = true;
    Serial.println("✅ LoRa hazır; sürekli RX Core 1 üzerinde çalışacak.");
    return true;
}

void taskLoop(void *pvParameters) {
    loraTaskHandle = xTaskGetCurrentTaskHandle();
    if (radioReady) startContinuousReceive();

    LoraTxMessage outgoing = {};
    while (true) {
        if (radioIrqFlag) processIncoming();
        OtaManager::tick();
        if (pending.active && static_cast<int32_t>(millis() - pending.deadline) >= 0) {
            emitControl(pending.id, pending.target, pending.opcode, "RESULT", "TIMEOUT");
            pending.active = false;
        }
        PcControlCommand pc = {};
        if (pcControlQueue && xQueueReceive(pcControlQueue, &pc, 0) == pdPASS) {
            const String target(pc.target), op(pc.opcode);
            if (pending.active) {
                emitControl(pc.command_id, target, op, "RESULT", "BUSY");
            } else {
                const uint32_t seq = ++pcControlSequence;
                if (seq == 0) { emitControl(pc.command_id, target, op, "RESULT", "SEQ_EXHAUSTED"); continue; }
                if (target == myTowerID) {
                    ControlRequest local = {};
                    myTowerID.toCharArray(local.source, sizeof(local.source));
                    local.source_boot = myBootCounter;
                    local.sequence = seq;
                    local.command_id = pc.command_id;
                    op.toCharArray(local.opcode, sizeof(local.opcode));
                    local.argument = pc.argument;
                    if (!SensorManager::enqueueControlRequest(local))
                        emitControl(pc.command_id, target, op, "RESULT", "BUSY");
                } else {
                    const String body = "OGC2|CMD|" + myTowerID + "|" + String(myBootCounter) +
                        "|" + target + "|" + String(seq) + "|" + op + "|" + String(pc.argument) + "|0";
                    LoraTxMessage tx = {};
                    const String payload = signedControl(body);
                    if (!fillTxMessage(tx, "CMD", target, myBootCounter, seq, true, payload)) {
                        emitControl(pc.command_id, target, op, "RESULT", "BAD_PAYLOAD");
                    } else {
                        pending.active = true;
                        pending.id = pc.command_id; pending.sequence = seq;
                        pending.target = target; pending.opcode = op;
                        // RX result can arrive while the sendWithRetries() ACK wait is active.
                        pending.deadline = millis() + OG_CONTROL_RESULT_TIMEOUT_MS;
                        // Multi-hop OG4 ACK is not a reliable proof of delivery.
                        // The authenticated OGC2 RESULT is the actual completion proof.
                        tx.require_ack=false;
                        const bool delivered = sendWithRetries(tx);
                        if (!delivered && pending.active) {
                            emitControl(pc.command_id, target, op, "RESULT", "NO_ACK");
                            pending.active = false;
                        } else if (pending.active) {
                            emitControl(pc.command_id, target, op, "SENT", "ACKED");
                        }
                    }
                }
            }
        }

        if (loraTxQueue != nullptr && xQueueReceive(loraTxQueue, &outgoing, pdMS_TO_TICKS(20)) == pdPASS) {
            sendWithRetries(outgoing);
        }

        // OTA intentionally stays below normal OG4 alerts and OGC2 controls.
        // A PC-side legal duty-cycle pacer is ALSO mandatory. This node imposes
        // an additional spacing limit for OTA / relay traffic.
        LoraTxMessage otaOutgoing={};
        if(otaTxQueue && uxQueueMessagesWaiting(loraTxQueue)==0 &&
           (!lastOtaTxMs || uint32_t(millis()-lastOtaTxMs)>=otaRequiredGapMs) &&
           (!lastOtherTxMs || uint32_t(millis()-lastOtherTxMs)>=otaRequiredGapMs) &&
           xQueueReceive(otaTxQueue,&otaOutgoing,0)==pdPASS) {
            delay(random(40,140));
            const uint32_t requiredGap=otaGapMs(strlen(otaOutgoing.payload));
            if(sendWithRetries(otaOutgoing)) {
                lastOtaTxMs=millis();
                otaRequiredGapMs=requiredGap;
            }
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30));
    }
}

bool queueLocalAlert(const SensorDataPacket &p) {
    if (!radioReady || loraTxQueue == nullptr) return false;
    LoraTxMessage msg = {};
    const String payload = buildMessage(
        "ALERT", "*", myBootCounter, p.sequence, p.fire_level, p.health_level,
        p.local_fire_score, p.max_temp, p.largest_hotspot_cluster,
        p.persistence_count, p.gas_drop_pct, p.battery_pct
    );
    if (!fillTxMessage(msg, "ALERT", "*", myBootCounter, p.sequence, true, payload)) return false;
    return xQueueSend(loraTxQueue, &msg, pdMS_TO_TICKS(100)) == pdPASS;
}

bool queuePeerReport(const SensorDataPacket &p, const String &triggerSource, uint32_t triggerBoot, uint32_t triggerSequence) {
    if (!radioReady || loraTxQueue == nullptr) return false;
    LoraTxMessage msg = {};
    const String payload = buildMessage(
        "REPORT", triggerSource, triggerBoot, triggerSequence, p.fire_level, p.health_level,
        p.local_fire_score, p.max_temp, p.largest_hotspot_cluster,
        p.persistence_count, p.gas_drop_pct, p.battery_pct
    );
    if (!fillTxMessage(msg, "REPORT", triggerSource, triggerBoot, triggerSequence, true, payload)) return false;
    return xQueueSend(loraTxQueue, &msg, pdMS_TO_TICKS(100)) == pdPASS;
}

bool queueConfirmation(const SensorDataPacket &p, const String &peer, uint32_t eventSequence) {
    if (!radioReady || loraTxQueue == nullptr) return false;
    LoraTxMessage msg = {};
    const String payload = buildMessage(
        "CONFIRM", peer, myBootCounter, eventSequence, FIRE_NETWORK_CORROBORATED, p.health_level,
        p.local_fire_score, p.max_temp, p.largest_hotspot_cluster,
        p.persistence_count, p.gas_drop_pct, p.battery_pct
    );
    if (!fillTxMessage(msg, "CONFIRM", peer, myBootCounter, eventSequence, true, payload)) return false;
    return xQueueSend(loraTxQueue, &msg, pdMS_TO_TICKS(100)) == pdPASS;
}

bool submitPcCommand(uint32_t commandId, const String &target, const String &opcode, int argument) {
    if (!pcControlQueue || !radioReady || commandId == 0 || !validTowerId(target) ||
        !validOpcode(opcode) || argument < -32768 || argument > 32767) return false;
    if ((opcode == "PAN" || opcode == "TILT") &&
        (argument < OG_SERVO_MIN_DEGREES || argument > OG_SERVO_MAX_DEGREES)) return false;
    if (opcode != "PAN" && opcode != "TILT" && argument != 0) return false;
    PcControlCommand request = {};
    request.command_id = commandId;
    target.toCharArray(request.target, sizeof(request.target));
    opcode.toCharArray(request.opcode, sizeof(request.opcode));
    request.argument = static_cast<int16_t>(argument);
    return xQueueSend(pcControlQueue, &request, 0) == pdPASS;
}

bool queueControlResult(const ControlRequest &r, const char *status,
                        const SensorDataPacket *sample, int pan, int tilt, bool manual) {
    const float maxT = sample ? sample->max_temp : 0.0f;
    const float score = sample ? sample->local_fire_score : 0.0f;
    const int battery = sample ? sample->battery_pct : -1;
    if (r.command_id > 0 && String(r.source) == myTowerID) {
        emitControl(r.command_id, myTowerID, r.opcode, "RESULT", status,
                    maxT, score, battery, pan, tilt, manual, sample ? sample->health_level : -1);
        return true;
    }
    if (!loraTxQueue || !validTowerId(String(r.source))) return false;
    const String body = "OGC2|RESULT|" + myTowerID + "|" + String(myBootCounter) +
        "|" + String(r.source) + "|" + String(r.source_boot) + "|" + String(r.sequence) +
        "|" + String(r.opcode) + "|" + String(status) + "|" + String(maxT,1) +
        "|" + String(score,1) + "|" + String(battery) + "|" + String(pan) +
        "|" + String(tilt) + "|" + (manual ? "1" : "0") +
        "|" + String(sample ? static_cast<int>(sample->health_level) : -1) + "|0";
    LoraTxMessage tx = {};
    const String payload = signedControl(body);
    // Cache a result so retransmitted authenticated CMD can be answered without
    // moving an actuator a second time.
    lastControlResultPayload=payload;
    lastControlResultSource=String(r.source);
    lastControlResultSequence=r.sequence;
    return fillTxMessage(tx, "RESULT", String(r.source), r.source_boot, r.sequence, false, payload) &&
           xQueueSend(loraTxQueue, &tx, 0) == pdPASS;
}
bool submitOtaProxy(const String &body) {
    if(!radioReady || !otaTxQueue || body.length()>211 || pending.active) return false;
    String f[8];
    if(pipeCount(body)!=7 || splitMessage(body,f,8)!=8 || f[0]!="OGU1" || f[2]!=myTowerID ||
       !validTowerId(f[3]) || f[3]==myTowerID || f[4].length()!=8 ||
       f[7]!="0" || f[5].length()>10 || f[6].length()>134) return false;
    const String kind=f[1];
    if(kind!="B" && kind!="S" && kind!="D" && kind!="E" && kind!="X" && kind!="Q") return false;
    for(size_t i=0;i<f[4].length();++i) if(!isxdigit((unsigned char)f[4][i])) return false;
    for(size_t i=0;i<f[5].length();++i) if(!isdigit((unsigned char)f[5][i])) return false;
    for(size_t i=0;i<f[6].length();++i) {
       const char c=f[6][i];
       if(!isalnum((unsigned char)c) && c!='+' && c!='/' && c!='=' && c!=',' && c!='-' && c!='.') return false;
    }
    const String payload=signedControl(body);
    LoraTxMessage tx={};
    return fillTxMessage(tx,"OTA",f[3],0,0,false,payload) &&
           xQueueSend(otaTxQueue,&tx,0)==pdPASS;
}

bool isReady() { return radioReady; }

bool isIdleForLightSleep() {
    if (!radioReady || radioBusy || radioIrqFlag) return false;
    if (loraTxQueue != nullptr && uxQueueMessagesWaiting(loraTxQueue) > 0) return false;
    return digitalRead(LORA_DIO1_PIN) == LOW;
}

void notifyIfIrqLineActive() {
    if (digitalRead(LORA_DIO1_PIN) == HIGH) {
        radioIrqFlag = true;
        if (loraTaskHandle != nullptr) xTaskNotifyGive(loraTaskHandle);
    }
}

} // namespace LoraManager
