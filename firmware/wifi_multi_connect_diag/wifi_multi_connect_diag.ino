/*
 * WiFi Multi-AP Connect — DIAGNOSTIC BUILD
 * ESP32-C3 SuperMini / ESP32-C3 Dev Board
 *
 * Board: ESP32C3 Dev Module
 * Tools > USB CDC On Boot > Enabled
 * Baud:  115200
 *
 * This build prints the raw WiFi disconnect reason code, which is the only
 * thing that distinguishes "wrong password" from "AP refused us" from
 * "radio never transmitted".
 */

#include <WiFi.h>

struct Credential {
  const char* ssid;
  const char* password;
};

const Credential CREDENTIALS[] = {
  { "dipak34",              "qwer1234"   },
  { "TP-Link_Second_Floor", "1234567890" },
};

const int CRED_COUNT = sizeof(CREDENTIALS) / sizeof(CREDENTIALS[0]);

#define CONNECT_TIMEOUT_MS   15000
#define RETRY_CYCLE_DELAY_MS 5000

// TX power ladder, strongest first. reason=2 (AUTH_EXPIRE) on a board with a
// poorly matched antenna, or on a marginal USB supply, usually clears once
// the radio stops transmitting at full tilt.
const wifi_power_t TX_LADDER[] = {
  WIFI_POWER_19_5dBm,
  WIFI_POWER_15dBm,
  WIFI_POWER_11dBm,
  WIFI_POWER_8_5dBm,
  WIFI_POWER_5dBm,
  WIFI_POWER_2dBm,
};
const int TX_LADDER_LEN = sizeof(TX_LADDER) / sizeof(TX_LADDER[0]);

// Last disconnect reason seen, captured by the event handler.
volatile uint8_t g_lastReason = 0;
volatile bool    g_gotReason  = false;

// Decode the esp_wifi reason codes that actually matter in practice.
const char* reasonToStr(uint8_t r) {
  switch (r) {
    case 1:   return "UNSPECIFIED";
    case 2:   return "AUTH_EXPIRE";
    case 4:   return "ASSOC_EXPIRE";
    case 6:   return "NOT_AUTHED";
    case 7:   return "NOT_ASSOCED";
    case 8:   return "ASSOC_LEAVE";
    case 15:  return "4WAY_HANDSHAKE_TIMEOUT  <-- almost always WRONG PASSWORD";
    case 200: return "BEACON_TIMEOUT";
    case 201: return "NO_AP_FOUND  <-- SSID/channel/band mismatch";
    case 202: return "AUTH_FAIL  <-- WRONG PASSWORD";
    case 203: return "ASSOC_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT";
    case 205: return "CONNECTION_FAIL";
    default:  return "(see esp_wifi_types.h)";
  }
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("\n  [event] STA_CONNECTED (association OK)");
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      uint8_t reason = info.wifi_sta_disconnected.reason;
      g_lastReason = reason;
      g_gotReason  = true;
      Serial.printf("\n  [event] STA_DISCONNECTED  reason=%u  %s\n",
                    reason, reasonToStr(reason));
      break;
    }

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("\n  [event] GOT_IP: %s\n",
                    WiFi.localIP().toString().c_str());
      break;

    default:
      break;
  }
}

bool tryConnect(const Credential& cred) {
  Serial.printf("\nConnecting to \"%s\" (pass len %u) ...\n",
                cred.ssid, (unsigned)strlen(cred.password));

  g_gotReason = false;

  // NOTE: signature is disconnect(bool wifioff, bool eraseap).
  // Keep the radio ON, erase the stored AP record.
  WiFi.disconnect(false, true);
  delay(500);

  WiFi.begin(cred.ssid, cred.password);

  uint32_t start = millis();
  while (millis() - start < CONNECT_TIMEOUT_MS) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\n  Connected in %lu ms\n", millis() - start);
      Serial.printf("  IP      : %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("  RSSI    : %d dBm\n", WiFi.RSSI());
      Serial.printf("  Channel : %d\n", WiFi.channel());
      Serial.printf("  BSSID   : %s\n", WiFi.BSSIDstr().c_str());
      return true;
    }

    // Bail early on an unambiguous auth failure — no point waiting.
    if (g_gotReason && (g_lastReason == 202 || g_lastReason == 15)) {
      Serial.println("  Giving up on this AP (auth failure).");
      return false;
    }

    Serial.print('.');
    delay(400);
  }

  Serial.printf("\n  Timed out after %d ms", CONNECT_TIMEOUT_MS);
  if (g_gotReason) Serial.printf(" (last reason=%u)", g_lastReason);
  else             Serial.print(" (NO disconnect event ever fired)");
  Serial.println();
  return false;
}

bool connectToAnyAP() {
  for (int i = 0; i < CRED_COUNT; i++) {
    Serial.printf("\n[%d/%d]", i + 1, CRED_COUNT);
    if (tryConnect(CREDENTIALS[i])) return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\nESP32-C3 multi-AP connect (diagnostic)");
  Serial.printf("Arduino core: %s\n", ESP.getSdkVersion());

  WiFi.onEvent(onWiFiEvent);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // modem sleep can stall the handshake
  WiFi.setAutoReconnect(false);
  delay(200);

  // Walk down the TX ladder. The power level that first succeeds is the
  // answer: note it and hard-code it in your production sketch.
  bool up = false;
  for (int p = 0; p < TX_LADDER_LEN && !up; p++) {
    WiFi.setTxPower(TX_LADDER[p]);
    delay(100);
    Serial.printf("\n===== TX power step %d/%d  (getTxPower=%d) =====\n",
                  p + 1, TX_LADDER_LEN, WiFi.getTxPower());
    up = connectToAnyAP();
    if (!up) delay(RETRY_CYCLE_DELAY_MS);
  }

  if (!up) {
    Serial.println("\nEvery TX power level failed on every AP.");
    Serial.println("Next: test against a phone hotspot to isolate the AP.");
    return;
  }

  Serial.printf("\nReady. Working TX power = %d (0.25 dBm units)\n",
                WiFi.getTxPower());
}

void loop() {
  delay(1000);
}
