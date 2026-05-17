#include "WiFi.h"
#include "WiFiProv.h"
#include "nvs_flash.h"

// ---------------------------
// Device provisioning settings
// ---------------------------

// Proof of possession (PIN shown in phone app during provisioning)
static const char *kProofOfPossession = "nht12345";

// Device name shown in provisioning app (BLE service name)
static const char *kProvisioningServiceName = "CAN_123";

// SoftAP password (used only for SoftAP scheme). NULL = no password.
static const char *kSoftApPassword = NULL;

// true: erase old Wi-Fi provisioning data before starting new provisioning
static const bool kResetProvisionedData = true;

// Optional custom UUID for BLE provisioning service
static uint8_t kBleServiceUuid[16] = {
  0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
  0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02
};

// Reset button pin
static const int kResetButtonPin = 0; // GPIO0 (BOOT button on most ESP32 boards)
static const unsigned long kLongPressTime = 3000; // 3 seconds for factory reset
unsigned long buttonPressTime = 0;

// NOTE: This callback runs in a separate FreeRTOS task.
void onWiFiProvisioningEvent(arduino_event_t *event) {
  switch (event->event_id) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("[WiFi] Connected. IP: ");
      Serial.println(IPAddress(event->event_info.got_ip.ip_info.ip.addr));
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("[WiFi] Disconnected. Reconnecting...");
      break;

    case ARDUINO_EVENT_PROV_START:
      Serial.println("[Prov] Provisioning started.");
      Serial.println("[Prov] Send Wi-Fi credentials from smartphone app.");
      break;

    case ARDUINO_EVENT_PROV_CRED_RECV:
      Serial.println("[Prov] Credentials received:");
      Serial.print("  SSID: ");
      Serial.println((const char *)event->event_info.prov_cred_recv.ssid);
      Serial.print("  Password: ");
      Serial.println((const char *)event->event_info.prov_cred_recv.password);
      break;

    case ARDUINO_EVENT_PROV_CRED_FAIL:
      Serial.println("[Prov] Provisioning failed.");
      if (event->event_info.prov_fail_reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR) {
        Serial.println("[Prov] Reason: Incorrect Wi-Fi password.");
      } else {
        Serial.println("[Prov] Reason: Access point not found.");
        Serial.println("[Prov] Tip: erase NVS (nvs_flash_erase()) before beginProvision().");
      }
      break;

    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
      Serial.println("[Prov] Provisioning successful.");
      Serial.println("If you want to reset provisioning, press BOOT button for 3s.");
      break;

    case ARDUINO_EVENT_PROV_END:
      Serial.println("[Prov] Provisioning ended.");
      break;

    default:
      break;
  }
}

void resetProvisioning() {
  Serial.println("[Reset] Erasing provisioning data...");
  WiFiProv.endProvision();
  nvs_flash_erase(); // Erase NVS partition
  Serial.println("[Reset] Provisioning data erased. Restarting...");
  delay(3000);
  ESP.restart();
}

void checkResetButton() {
  if (digitalRead(kResetButtonPin) == LOW) {
    if (buttonPressTime == 0) {
      buttonPressTime = millis();
      Serial.println("[Button] Press detected...");
    } else if (millis() - buttonPressTime >= kLongPressTime) {
      Serial.println("[Button] Long press detected. Resetting provisioning...");
      resetProvisioning();
    }
  } else {
    buttonPressTime = 0;
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); } // Wait for Serial to be ready
  Serial.flush();                 // Flush serial buffer

  pinMode(kResetButtonPin, INPUT_PULLUP);

  // Register event callback for both Wi-Fi and provisioning events
  WiFi.onEvent(onWiFiProvisioningEvent);

  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }

  // Check if Wi-Fi credentials are stored in NVS
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open("nvs.net80211", NVS_READONLY, &nvs_handle);
  bool provisioned = false;

  if (err == ESP_OK) {
    size_t ssid_len = 0;
    err = nvs_get_blob(nvs_handle, "sta.ssid", NULL, &ssid_len);
    provisioned = (err == ESP_OK && ssid_len > 0);
    nvs_close(nvs_handle);
  }

  if (!provisioned) {
    Serial.println("[Init] Device not provisioned. Starting provisioning...");

    // Start provisioning:
    // - Scheme: BLE
    // - Security: POP-based security level 1
    WiFiProv.beginProvision(
      NETWORK_PROV_SCHEME_BLE,
      NETWORK_PROV_SCHEME_HANDLER_FREE_BLE,
      NETWORK_PROV_SECURITY_1,
      kProofOfPossession,
      kProvisioningServiceName,
      kSoftApPassword,
      kBleServiceUuid,
      kResetProvisionedData
    );


    // Print QR payload for phone app provisioning
    WiFiProv.printQR(kProvisioningServiceName, kProofOfPossession, "ble");
  } else {
    Serial.println("[Init] Device already provisioned. Connecting to saved network...");
    Serial.println("If you want to reset provisioning, press BOOT button for 3s.");
    WiFi.mode(WIFI_STA);
    WiFi.begin();
  }
}

void loop() {
  checkResetButton();
  delay(200);
}
