#include "WiFi.h"
#include "WiFiProv.h"
#include "nvs_flash.h"
#include "esp_camera.h"
#include "board_config.h"
#include <ESPmDNS.h>

// ============================================================
// Sketch overview (self-explanation)
// 1) Boot and initialize serial + NVS
// 2) Check if Wi-Fi credentials already exist in NVS
// 3) If not provisioned: start BLE provisioning and wait for app
// 4) If provisioned: initialize camera, connect Wi-Fi, start mDNS + web server
// 5) Always monitor BOOT long-press to factory-reset provisioning data
// ============================================================

// ============================================================
// Limits for Wi-Fi credential buffers
// IEEE 802.11 allows up to:
//   - SSID: 32 characters
//   - WPA/WPA2 password: 64 characters
// Extra byte is reserved for '\0' terminator.
// ============================================================
#define WIFI_SSID_MAX_LEN 33   // 32 + '\0'
#define WIFI_PASS_MAX_LEN 65   // 64 + '\0'

// ============================================================
// Provisioning configuration
// ============================================================

// Proof-of-possession string used by the mobile provisioning app.
// This acts like a shared secret during provisioning.
static const char *kProvisioningPop = "nht12345";

// BLE service name visible in the ESP BLE Provisioning app.
static const char *kProvisioningBleServiceName = "CAN_123";

// Password for SoftAP provisioning mode only.
// Not used here because provisioning mode is BLE.
static const char *kProvisioningSoftApPassword = NULL;

// If true, existing provisioning data is erased when provisioning starts.
// Useful during development/testing. In production, this is often false.
static const bool kForceResetProvisioningDataOnStart = true;
// NOTE: Keep this 'true' for development convenience.
// In production, set to false to preserve valid stored credentials.

// Optional custom BLE service UUID.
// This helps identify the provisioning service.
static uint8_t kProvisioningBleServiceUuid[16] = {
  0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
  0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02
};

// ============================================================
// Factory-reset button configuration
// ============================================================

// BOOT button pin on most ESP32 camera boards.
// Holding this button for a few seconds clears stored provisioning.
static const int kFactoryResetButtonPin = 0; // GPIO 0 is usually the BOOT button

// Duration required to trigger factory reset.
static const unsigned long kFactoryResetLongPressMs = 3000;

// Debounce window for BOOT button sampling.
static const unsigned long kFactoryResetDebounceMs = 100;

// Timestamp of when the button was first pressed.
unsigned long gButtonPressStartMs = 0;

// Arm long-press detection only after button is seen released once.
// This avoids false factory-reset triggers right after boot/Wi-Fi connect.
bool gFactoryResetButtonArmed = false;

// Forward declarations for camera web server functions.
// These are usually implemented in another source file.
void startCameraServer();
void setupLedFlash();

// ------------------------------------------------------------
// Event callback: Wi-Fi + Provisioning events
// NOTE:
// - This callback runs in a separate FreeRTOS task context.
// - Avoid long blocking operations here.
// - Use it mainly for logging or small state updates.
// ------------------------------------------------------------
void handleWiFiAndProvisioningEvent(arduino_event_t *event) {
  // Centralized event logger for both provisioning and STA connection lifecycle.
  switch (event->event_id) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      // Wi-Fi station connected successfully and received an IP address.
      Serial.print("[WiFi] Connected. IP: ");
      Serial.println(IPAddress(event->event_info.got_ip.ip_info.ip.addr));
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      // Station disconnected from the AP.
      // ESP32 Wi-Fi stack may reconnect automatically depending on settings.
      Serial.println("[WiFi] Disconnected. Reconnecting...");
      break;

    case ARDUINO_EVENT_PROV_START:
      // Provisioning has started and device is ready to receive credentials.
      Serial.println("[Prov] Provisioning started.");
      Serial.println("[Prov] Send Wi-Fi credentials from smartphone app.");
      break;

    case ARDUINO_EVENT_PROV_CRED_RECV:
      // Credentials were received from the provisioning app.
      // Password is printed here for debugging only.
      Serial.println("[Prov] Credentials received:");
      Serial.print("  SSID: ");
      Serial.println((const char *)event->event_info.prov_cred_recv.ssid);
      Serial.print("  Password: ");
      Serial.println((const char *)event->event_info.prov_cred_recv.password);
      break;

    case ARDUINO_EVENT_PROV_CRED_FAIL:
      // Provisioning failed due to invalid Wi-Fi information
      // or because the target AP could not be found.
      Serial.println("[Prov] Provisioning failed.");
      if (event->event_info.prov_fail_reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR) {
        Serial.println("[Prov] Reason: Incorrect Wi-Fi password.");
      } else {
        Serial.println("[Prov] Reason: Access point not found.");
        Serial.println("[Prov] Tip: erase NVS before beginProvision().");
      }
      break;

    case ARDUINO_EVENT_PROV_CRED_SUCCESS:
      // Wi-Fi credentials were accepted and saved.
      Serial.println("[Prov] Provisioning successful.");
      Serial.println("[Prov] Hold BOOT for 3s to factory reset provisioning.");
      break;

    case ARDUINO_EVENT_PROV_END:
      // Provisioning workflow has finished.
      // Typical next step is restart so normal station flow can start cleanly.
      Serial.println("[Prov] Provisioning ended. Restarting ...");
      digitalWrite(LED_GPIO_NUM, LOW);
      rebootDevice();
      break;

    default:
      break;
  }
}

// ------------------------------------------------------------
// Camera initialization
// ------------------------------------------------------------
void initializeCamera() {
  // Keep all camera defaults in one place for easier tuning/debugging.
  // Camera driver configuration structure.
  // Pin values come from board_config.h for the selected camera board.
  camera_config_t cameraConfig;
  cameraConfig.ledc_channel = LEDC_CHANNEL_0;
  cameraConfig.ledc_timer = LEDC_TIMER_0;
  cameraConfig.pin_d0 = Y2_GPIO_NUM;
  cameraConfig.pin_d1 = Y3_GPIO_NUM;
  cameraConfig.pin_d2 = Y4_GPIO_NUM;
  cameraConfig.pin_d3 = Y5_GPIO_NUM;
  cameraConfig.pin_d4 = Y6_GPIO_NUM;
  cameraConfig.pin_d5 = Y7_GPIO_NUM;
  cameraConfig.pin_d6 = Y8_GPIO_NUM;
  cameraConfig.pin_d7 = Y9_GPIO_NUM;
  cameraConfig.pin_xclk = XCLK_GPIO_NUM;
  cameraConfig.pin_pclk = PCLK_GPIO_NUM;
  cameraConfig.pin_vsync = VSYNC_GPIO_NUM;
  cameraConfig.pin_href = HREF_GPIO_NUM;
  cameraConfig.pin_sccb_sda = SIOD_GPIO_NUM;
  cameraConfig.pin_sccb_scl = SIOC_GPIO_NUM;
  cameraConfig.pin_pwdn = PWDN_GPIO_NUM;
  cameraConfig.pin_reset = RESET_GPIO_NUM;
  cameraConfig.xclk_freq_hz = 20000000;
  cameraConfig.frame_size = FRAMESIZE_UXGA;
  cameraConfig.pixel_format = PIXFORMAT_JPEG; // JPEG is required for web streaming
  cameraConfig.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  cameraConfig.fb_location = CAMERA_FB_IN_PSRAM;
  cameraConfig.jpeg_quality = 12; // lower value = better quality
  cameraConfig.fb_count = 1;

  // If PSRAM exists, the camera can use larger frame buffers and better quality.
  // This improves streaming performance and allows more memory-heavy settings.
  if (cameraConfig.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      cameraConfig.jpeg_quality = 10;
      cameraConfig.fb_count = 2;
      cameraConfig.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Without PSRAM, reduce frame size and use DRAM to avoid memory issues.
      // This trades quality for stability on memory-limited boards.
      cameraConfig.frame_size = FRAMESIZE_SVGA;
      cameraConfig.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Raw formats use more memory, so choose a small resolution.
    cameraConfig.frame_size = FRAMESIZE_240X240;
  #if CONFIG_IDF_TARGET_ESP32S3
    cameraConfig.fb_count = 2;
  #endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  // Extra pin setup required for ESP-EYE board.
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // Initialize the camera driver with the selected configuration.
  esp_err_t initStatus = esp_camera_init(&cameraConfig);
  if (initStatus != ESP_OK) {
    // Early return keeps later sensor calls safe when camera init fails.
    Serial.printf("[Camera] Init failed. Error: 0x%x\n", initStatus);
    return;
  }

  // Access sensor settings for image tuning.
  sensor_t *sensor = esp_camera_sensor_get();

  // Apply sensor-specific tuning for OV3660.
  if (sensor->id.PID == OV3660_PID) {
    sensor->set_vflip(sensor, 1);
    sensor->set_brightness(sensor, 1);
    sensor->set_saturation(sensor, -2);
  }

  // Start with smaller frames to improve startup responsiveness.
  if (cameraConfig.pixel_format == PIXFORMAT_JPEG) {
    sensor->set_framesize(sensor, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  // Correct orientation for specific M5Stack camera boards.
  sensor->set_vflip(sensor, 1);
  sensor->set_hmirror(sensor, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  // Correct image orientation for ESP32S3-EYE.
  sensor->set_vflip(sensor, 1);
#endif

#if defined(LED_GPIO_NUM)
  // Initialize optional LED flash if available on the board.
  setupLedFlash();
#endif
}

void rebootDevice() {
  // Small LED blink gives user visual feedback before restart.
  Serial.println("[Reboot] Restarting device...");
  #if defined(LED_GPIO_NUM)
    // Turn on ESP32 CAM LED
    pinMode(LED_GPIO_NUM, OUTPUT);
    digitalWrite(LED_GPIO_NUM, HIGH);
    delay(100);
    digitalWrite(LED_GPIO_NUM, LOW);
  #endif
  ESP.restart();
}

// ------------------------------------------------------------
// Provisioning reset helpers
// ------------------------------------------------------------
void factoryResetProvisioning() {
  // Stop active provisioning service, erase NVS, then restart the chip.
  // This removes saved Wi-Fi credentials and provisioning information.
  // Equivalent to returning device to "not provisioned" state.
  Serial.println("[Reset] Erasing provisioning data...");
  WiFiProv.endProvision();
  nvs_flash_erase();
  Serial.println("[Reset] Done. Restarting...");
  delay(3000);
  rebootDevice();
}

void handleFactoryResetButton() {
  // Button is active LOW because it uses INPUT_PULLUP.
  const unsigned long nowMs = millis();
  const int rawState = digitalRead(kFactoryResetButtonPin);

  // Simple debounce + stable-state tracking.
  static int lastRawState = HIGH;
  static int debouncedState = HIGH;
  static unsigned long lastChangeMs = 0;
  static bool resetTriggeredForThisHold = false;

  if (rawState != lastRawState) {
    // Raw transition observed; start debounce timing window.
    lastRawState = rawState;
    lastChangeMs = nowMs;
  }

  if (nowMs - lastChangeMs >= kFactoryResetDebounceMs) {
    debouncedState = rawState;
  }

  if (!gFactoryResetButtonArmed) {
    // Arm only after the button is observed released once after boot.
    if (debouncedState == HIGH) {
      gFactoryResetButtonArmed = true;
    }
    gButtonPressStartMs = 0;
    resetTriggeredForThisHold = false;
    return;
  }

  if (debouncedState == LOW) {
    // First moment the debounced button is detected as pressed.
    if (gButtonPressStartMs == 0) {
      gButtonPressStartMs = nowMs;
      resetTriggeredForThisHold = false;
    } else if (!resetTriggeredForThisHold && (nowMs - gButtonPressStartMs >= kFactoryResetLongPressMs)) {
      if(digitalRead(kFactoryResetButtonPin) == LOW) {
        // Re-check raw pin before reset to reduce false positives.
        // Button has been held long enough to trigger factory reset.
        resetTriggeredForThisHold = true;
        Serial.println("[Button] Long press detected. Factory reset...");
        factoryResetProvisioning();
      } else {
        gButtonPressStartMs = 0;
      }
    }
  } else {
    // Button released before timeout, so clear press tracking.
    gButtonPressStartMs = 0;
    resetTriggeredForThisHold = false;
  }
}

// ------------------------------------------------------------
// Read stored Wi-Fi credentials from NVS
// Namespace/key used by ESP Wi-Fi stack:
//   namespace: "nvs.net80211"
//   keys:      "sta.ssid", "sta.pswd"
//
// Notes:
// - SSID/password are read as blobs from NVS.
// - Output buffers are always null-terminated if data is read.
// - Returns true if SSID exists and was read successfully.
// ------------------------------------------------------------
static bool loadWiFiCredentialsFromNvs(
  char *outSsid, size_t outSsidSize,
  char *outPass, size_t outPassSize
) {
  // Helper kept for diagnostics/manual flows.
  // Current sketch mostly relies on WiFi.begin() auto-loading credentials.
  if (!outSsid || outSsidSize == 0 || !outPass || outPassSize == 0) {
    Serial.println("[NVS] Invalid buffer parameters.");
    return false;
  }

  // Start with empty strings.
  outSsid[0] = '\0';
  outPass[0] = '\0';

  // Open Wi-Fi namespace in read-only mode.
  nvs_handle_t nvsHandle;
  if (nvs_open("nvs.net80211", NVS_READONLY, &nvsHandle) != ESP_OK) {
    Serial.println("[NVS] Failed to open namespace 'nvs.net80211'.");
    return false;
  }
  Serial.println("[NVS] Opened namespace 'nvs.net80211'.");

  // Query stored SSID length first.
  size_t ssidLen = 0;
  esp_err_t status = nvs_get_blob(nvsHandle, "sta.ssid", nullptr, &ssidLen);
  if (status != ESP_OK || ssidLen == 0) {
    Serial.printf("[NVS] SSID not found or empty. status=0x%x, len=%d\n", status, ssidLen);
    nvs_close(nvsHandle);
    return false;
  }
  Serial.printf("[NVS] SSID blob found. Stored length: %d bytes.\n", ssidLen);

  // Read SSID into caller buffer, truncating if buffer is smaller than stored value.
  size_t ssidReadLen = (ssidLen < outSsidSize) ? ssidLen : (outSsidSize - 1);
  if (ssidReadLen < ssidLen) {
    Serial.printf("[NVS] SSID truncated: stored=%d, read=%d.\n", ssidLen, ssidReadLen);
  }
  status = nvs_get_blob(nvsHandle, "sta.ssid", outSsid, &ssidReadLen);
  if (status != ESP_OK) {
    Serial.printf("[NVS] Failed to read SSID. status=0x%x\n", status);
    nvs_close(nvsHandle);
    return false;
  }
  outSsid[ssidReadLen] = '\0';
  Serial.printf("[NVS] SSID read successfully. Value: '%s'\n", outSsid);

  // Password may be empty for open networks.
  size_t passLen = 0;
  esp_err_t passStatus = nvs_get_blob(nvsHandle, "sta.pswd", nullptr, &passLen);
  if (passStatus == ESP_OK && passLen > 0) {
    Serial.printf("[NVS] Password blob found. Stored length: %d bytes.\n", passLen);
    size_t passReadLen = (passLen < outPassSize) ? passLen : (outPassSize - 1);
    if (passReadLen < passLen) {
      Serial.printf("[NVS] Password truncated: stored=%d, read=%d.\n", passLen, passReadLen);
    }
    if (nvs_get_blob(nvsHandle, "sta.pswd", outPass, &passReadLen) == ESP_OK) {
      outPass[passReadLen] = '\0';
      Serial.println("[NVS] Password read successfully.");
    } else {
      Serial.println("[NVS] Failed to read password blob.");
    }
  } else {
    Serial.printf("[NVS] No password found (open network or missing). status=0x%x\n", passStatus);
  }

  nvs_close(nvsHandle);
  Serial.println("[NVS] Namespace closed.");
  return true;
}

// ------------------------------------------------------------
// Check whether station credentials already exist in NVS
//
// This is a lightweight provisioned-state check.
// If "sta.ssid" exists and has nonzero size, device is considered provisioned.
// ------------------------------------------------------------
static bool isDeviceProvisioned() {
  // Minimal check: if SSID exists in Wi-Fi namespace, treat as provisioned.
  // This avoids starting BLE provisioning every boot.
  nvs_handle_t nvsHandle;
  if (nvs_open("nvs.net80211", NVS_READONLY, &nvsHandle) != ESP_OK) return false;

  size_t ssidLen = 0;
  esp_err_t status = nvs_get_blob(nvsHandle, "sta.ssid", NULL, &ssidLen);
  Serial.printf("[NVS] isDeviceProvisioned: sta.ssid status=0x%x, ssidLen=%d\n", status, ssidLen);
  if (status == ESP_OK && ssidLen > 0) {
    uint8_t ssidBuf[ssidLen + 1];
    memset(ssidBuf, 0, sizeof(ssidBuf));
    esp_err_t readStatus = nvs_get_blob(nvsHandle, "sta.ssid", ssidBuf, &ssidLen);
    if (readStatus == ESP_OK) {
      Serial.printf("[NVS] sta.ssid value: %s (len=%d)\n", (char*)ssidBuf, ssidLen);
    } else {
      Serial.printf("[NVS] Failed to read sta.ssid blob. status=0x%x\n", readStatus);
    }
  } else {
    Serial.printf("[NVS] sta.ssid not found or empty. status=0x%x\n", status);
  }
  nvs_close(nvsHandle);

  return (status == ESP_OK && ssidLen > 0);
}

// ------------------------------------------------------------
// Arduino setup()
// Runs once after boot/reset.
// ------------------------------------------------------------
void setup() {
  // Start serial monitor for debug logs.
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  Serial.flush();

  // BOOT button uses internal pull-up resistor.
  pinMode(kFactoryResetButtonPin, INPUT_PULLUP);

  // Avoid false long-press detection at startup; arm after first release.
  gFactoryResetButtonArmed = false;
  gButtonPressStartMs = 0;

  // Register a single callback to receive both Wi-Fi and provisioning events.
  // Useful for tracing startup/provisioning flow without scattering logs.
  WiFi.onEvent(handleWiFiAndProvisioningEvent);

  Serial.println("[Init] Waiting setup...");
  const unsigned long pollIntervalMs = 200;
  const int startupPollRetries = 30;
  for (int retry = 0; retry < startupPollRetries; ++retry) {
    handleFactoryResetButton();
    delay(pollIntervalMs);
  }

  // Initialize NVS flash storage.
  // Required for both provisioning library and saved Wi-Fi credentials.
  nvs_flash_init();

  // If device has not been provisioned yet, start BLE provisioning mode.
  if (!isDeviceProvisioned()) {
    Serial.println("[Init] Device not provisioned. Starting BLE provisioning...");

    WiFiProv.beginProvision(
      NETWORK_PROV_SCHEME_BLE,                 // BLE transport
      NETWORK_PROV_SCHEME_HANDLER_FREE_BLE,    // free BLE memory automatically after use
      NETWORK_PROV_SECURITY_1,                 // secure provisioning mode
      kProvisioningPop,                        // proof-of-possession
      kProvisioningBleServiceName,             // BLE service name
      kProvisioningSoftApPassword,             // unused in BLE mode
      kProvisioningBleServiceUuid,             // optional custom service UUID
      kForceResetProvisioningDataOnStart       // optionally clear old provisioning data
    );

    // Print a QR code payload to serial for the provisioning app.
    WiFiProv.printQR(kProvisioningBleServiceName, kProvisioningPop, "ble");

    #if defined(LED_GPIO_NUM)
      // Low-brightness LED = provisioning state indicator.
      analogWrite(LED_GPIO_NUM, 1); // dim LED to indicate provisioning mode
    #endif

    // Return here because provisioning mode is now active.
    return;
  }

  // Already provisioned path:
  // 1. Initialize camera
  // 2. Read stored Wi-Fi info for logging
  // 3. Connect to Wi-Fi
  // 4. Start mDNS
  // 5. Start camera web server
  initializeCamera();

  Serial.println("[Init] Provisioned. Connecting to saved Wi-Fi...");
  Serial.println("[Hint] Hold BOOT for 3s to reset provisioning.");

  // Put Wi-Fi into station mode and connect using saved credentials from NVS.
  WiFi.mode(WIFI_STA);
  // WiFi.begin() without args asks ESP-IDF to use credentials stored in NVS.
  WiFi.begin();
  
  WiFi.setSleep(false); // disable modem sleep for better responsiveness

  // Retry Wi-Fi connection up to 30 times.
  Serial.print("[WiFi] Connecting");
  int retries = 30;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    retries--;
    if (retries <= 0) {
      // Keep setup non-blocking forever: fail fast and allow possible watchdog-safe recovery.
      Serial.println();
      Serial.println("[WiFi] Connection timed out.");
      return;
    }
  }

  Serial.println();

  // Re-arm reset detection after Wi-Fi connect only when button is released once.
  gFactoryResetButtonArmed = false;
  gButtonPressStartMs = 0;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Failed to connect after 30 retries. Please consider holding BOOT for 3s to reset provisioning.");
    return;
  }

  Serial.println("[WiFi] Connected");

  // Start mDNS so the device can be reached by hostname on local network.
  if (!MDNS.begin("myeye")) {
    // mDNS is optional; if it fails, sketch currently aborts server start.
    // Change to warning-only if you want IP-only access fallback.
    Serial.println("[mDNS] Failed to start responder.");
    return;
  }

  if (!MDNS.addService("http", "tcp", 80)) {
    Serial.println("[mDNS] Failed to advertise HTTP service.");
    return;
  }

  // Start the camera web server after networking is ready.
  Serial.println("[CameraServer] Starting...");
  startCameraServer();
  Serial.print("[CameraServer] Started. Access at: http://");
  Serial.println(WiFi.localIP());
  Serial.println("[CameraServer] mDNS URL: http://myeye.local");

}

// ------------------------------------------------------------
// Arduino loop()
// Runs continuously after setup() finishes.
// ------------------------------------------------------------
void loop() {

  // Small delay to reduce CPU usage and debounce simple button handling.
  delay(200);
}
