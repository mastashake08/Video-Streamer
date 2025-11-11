/*
 * XIAO ESP32S3 Camera & Audio Streamer v3.0
 * 
 * For Arduino IDE:
 * - Board: XIAO_ESP32S3 (Seeed Studio XIAO ESP32S3)
 * - PSRAM: OPI PSRAM
 * - Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)
 * 
 * Required Libraries (install via Library Manager):
 * - ESP32 (by Espressif) - Install via Board Manager
 * - ESP32-Camera (by Espressif)
 * - ESPAsyncWebServer (by me-no-dev)
 * - AsyncTCP (by me-no-dev)
 * - ESP_I2S 3.0+ (by Phil Schatzmann)
 * - ArduinoOTA (included with ESP32)
 * 
 * Features:
 * - BLE Control for recording
 * - 10-second video/audio clips
 * - WiFi streaming mode
 * - SD card storage with auto-cleanup
 * - PSRAM support for camera buffers
 */

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <ESP_I2S.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "esp_camera.h"
#include "esp_sleep.h"

// I2S instance for PDM microphone (ESP_I2S 3.0)
I2SClass I2S;

// Camera sensor PID definitions
#define OV3660_PID 0x3660

// ============================================
// WiFi Credentials - CHANGE THESE!
// ============================================
const char* wifi_ssid = "MastaWifi";      // Change to your WiFi name
const char* wifi_password = "mastashake08";  // Change to your WiFi password

// ============================================
// BLE Configuration
// ============================================
#define BLE_SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CONTROL_CHAR_UUID   "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_STATUS_CHAR_UUID    "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e"
#define BLE_WIFI_CHAR_UUID      "d8de624e-140f-4a23-8b85-726f9d55da18"

BLEServer* pServer = nullptr;
BLECharacteristic* pControlCharacteristic = nullptr;
BLECharacteristic* pStatusCharacteristic = nullptr;
BLECharacteristic* pWiFiCharacteristic = nullptr;
bool deviceConnected = false;
bool bleEnabled = true;
bool wifiRequested = false;

// ============================================
// Recording Mode Configuration
// ============================================
bool recordingMode = false;
bool bleRecordingActive = false;
unsigned long frameCount = 0;
unsigned long audioFileCount = 0;

// Recording mode flags
bool audioOnlyMode = false;
bool videoOnlyMode = false;
bool bothMode = true;

// File listing flags
volatile bool listVideoRequested = false;
volatile bool listAudioRequested = false;
volatile bool listAllRequested = false;

// SD Card mutex for thread-safe access
SemaphoreHandle_t sdMutex = NULL;

// ============================================
// Time & NTP Configuration
// ============================================
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 0;
struct tm timeinfo;
bool timeInitialized = false;

// ============================================
// File Management Configuration
// ============================================
#define SD_MIN_FREE_SPACE_MB 100
#define MAX_FILE_AGE_HOURS 24
unsigned long lastCleanupTime = 0;
#define CLEANUP_INTERVAL 3600000

// ============================================
// Power Management Configuration
// ============================================
#define BATTERY_PIN 1
#define LOW_BATTERY_VOLTAGE 3.3
#define IDLE_SLEEP_TIMEOUT 300000
unsigned long lastActivityTime = 0;
bool batteryLow = false;

// ============================================
// Status LED Configuration
// ============================================
#define LED_BLINK_RECORDING 200
#define LED_BLINK_WIFI 1000
#define LED_BLINK_ERROR 100
unsigned long lastLedBlink = 0;
bool ledState = false;

enum SystemState {
  STATE_INIT,
  STATE_WIFI_CONNECTING,
  STATE_WIFI_CONNECTED,
  STATE_RECORDING,
  STATE_STREAMING,
  STATE_ERROR,
  STATE_LOW_BATTERY
};
SystemState currentState = STATE_INIT;

// SD Card configuration for XIAO ESP32S3 (SPI mode)
#define SD_CS_PIN 21

// Web server on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/audio");

// Audio configuration for PDM microphone
#define PDM_DATA_PIN 41
#define PDM_CLK_PIN 42
#define SAMPLE_RATE 16000U
#define SAMPLE_BITS 16
#define WAV_HEADER_SIZE 44
#define VOLUME_GAIN 2
#define RECORD_TIME 10
#define WAV_FILE_NAME "recording"

// Initialize camera - following official XIAO ESP32S3 example pattern
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 15;
  config.pin_d1 = 17;
  config.pin_d2 = 18;
  config.pin_d3 = 16;
  config.pin_d4 = 14;
  config.pin_d5 = 12;
  config.pin_d6 = 11;
  config.pin_d7 = 48;
  config.pin_xclk = 10;
  config.pin_pclk = 13;
  config.pin_vsync = 38;
  config.pin_href = 47;
  config.pin_sccb_sda = 40;
  config.pin_sccb_scl = 39;
  config.pin_pwdn = -1;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  
  // If PSRAM IC present, init with UXGA resolution and higher JPEG quality
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  }

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    currentState = STATE_ERROR;
    return false;
  }
  
  // Adjust camera sensor settings
  sensor_t * s = esp_camera_sensor_get();
  if (s) {
    if (s->id.PID == OV3660_PID) {
      s->set_vflip(s, 1);
      s->set_brightness(s, 1);
      s->set_saturation(s, -2);
    }
    if (config.pixel_format == PIXFORMAT_JPEG) {
      s->set_framesize(s, FRAMESIZE_QVGA);
    }
  }
  
  Serial.println("Camera initialized successfully");
  return true;
}

// ============================================
// TIME & TIMESTAMP FUNCTIONS
// ============================================
bool initTime() {
  Serial.println("Initializing time from NTP...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  int retries = 0;
  while (!getLocalTime(&timeinfo) && retries < 10) {
    Serial.print(".");
    delay(1000);
    retries++;
  }
  Serial.println();
  
  if (retries >= 10) {
    Serial.println("Failed to obtain time");
    return false;
  }
  
  Serial.println(&timeinfo, "Time initialized: %A, %B %d %Y %H:%M:%S");
  timeInitialized = true;
  return true;
}

String getTimestamp() {
  if (!timeInitialized || !getLocalTime(&timeinfo)) {
    return String(millis());
  }
  
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &timeinfo);
  return String(timestamp);
}

String getDateString() {
  if (!timeInitialized || !getLocalTime(&timeinfo)) {
    return "unknown";
  }
  
  char datestr[16];
  strftime(datestr, sizeof(datestr), "%Y-%m-%d", &timeinfo);
  return String(datestr);
}

// ============================================
// FILE ROTATION & CLEANUP
// ============================================
void cleanupOldFiles() {
  Serial.println("Running file cleanup...");
  
  uint64_t totalSpace = SD.totalBytes() / (1024 * 1024);
  uint64_t usedSpace = SD.usedBytes() / (1024 * 1024);
  uint64_t freeSpace = totalSpace - usedSpace;
  
  Serial.printf("SD Card: %lluMB free / %lluMB total\n", freeSpace, totalSpace);
  
  if (freeSpace > SD_MIN_FREE_SPACE_MB) {
    Serial.println("Sufficient free space available");
    return;
  }
  
  Serial.println("Low disk space! Deleting old files...");
  
  File videoDir = SD.open("/video");
  if (videoDir && videoDir.isDirectory()) {
    File file = videoDir.openNextFile();
    int deletedCount = 0;
    
    while (file && freeSpace < SD_MIN_FREE_SPACE_MB) {
      if (!file.isDirectory()) {
        String filename = String("/video/") + file.name();
        size_t fileSize = file.size();
        file.close();
        
        if (SD.remove(filename.c_str())) {
          deletedCount++;
          freeSpace += (fileSize / (1024 * 1024));
          Serial.printf("Deleted: %s\n", filename.c_str());
        }
      }
      file = videoDir.openNextFile();
    }
    
    Serial.printf("Cleanup complete: %d files deleted\n", deletedCount);
  }
  videoDir.close();
  
  freeSpace = (SD.totalBytes() - SD.usedBytes()) / (1024 * 1024);
  Serial.printf("Free space after cleanup: %lluMB\n", freeSpace);
}

// ============================================
// POWER MANAGEMENT
// ============================================
float getBatteryVoltage() {
  int adcValue = analogRead(BATTERY_PIN);
  float voltage = (adcValue / 4095.0) * 3.3 * 2.0;
  return voltage;
}

void checkBatteryStatus() {
  float voltage = getBatteryVoltage();
  
  if (voltage < LOW_BATTERY_VOLTAGE && voltage > 0.5) {
    if (!batteryLow) {
      batteryLow = true;
      currentState = STATE_LOW_BATTERY;
      Serial.printf("⚠️  LOW BATTERY: %.2fV\n", voltage);
    }
  } else {
    batteryLow = false;
  }
}

void enterDeepSleep(uint64_t sleepTimeSeconds) {
  Serial.printf("Entering deep sleep for %llu seconds\n", sleepTimeSeconds);
  SD.end();
  esp_sleep_enable_timer_wakeup(sleepTimeSeconds * 1000000ULL);
  esp_deep_sleep_start();
}

void checkIdleTimeout() {
  if (currentState == STATE_STREAMING || currentState == STATE_RECORDING) {
    lastActivityTime = millis();
    return;
  }
  
  if (millis() - lastActivityTime > IDLE_SLEEP_TIMEOUT) {
    Serial.println("Idle timeout - entering sleep mode");
    enterDeepSleep(60);
  }
}

// ============================================
// STATUS LED CONTROL
// ============================================
void updateStatusLED() {
  unsigned long currentMillis = millis();
  unsigned int blinkInterval;
  
  switch (currentState) {
    case STATE_RECORDING:
      blinkInterval = LED_BLINK_RECORDING;
      break;
    case STATE_WIFI_CONNECTING:
      blinkInterval = LED_BLINK_WIFI;
      break;
    case STATE_WIFI_CONNECTED:
    case STATE_STREAMING:
      blinkInterval = LED_BLINK_WIFI;
      break;
    case STATE_ERROR:
    case STATE_LOW_BATTERY:
      blinkInterval = LED_BLINK_ERROR;
      break;
    default:
      digitalWrite(LED_BUILTIN, LOW);
      return;
  }
  
  if (currentMillis - lastLedBlink >= blinkInterval) {
    lastLedBlink = currentMillis;
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
  }
}

// Forward declarations
void recordingTask(void *parameter);

// ============================================
// FILE LISTING FUNCTIONS
// ============================================
void listVideoFiles() {
  Serial.println("\n========================================");
  Serial.println("VIDEO FILES:");
  Serial.println("========================================");
  File videoDir = SD.open("/video");
  if (videoDir && videoDir.isDirectory()) {
    int count = 0;
    File file = videoDir.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        count++;
        Serial.printf("%d. %s (%u bytes)\n", count, file.name(), file.size());
      }
      file = videoDir.openNextFile();
    }
    if (count == 0) {
      Serial.println("No video files found.");
    } else {
      Serial.printf("\nTotal: %d video files\n", count);
    }
    videoDir.close();
  } else {
    Serial.println("Failed to open /video directory");
  }
  Serial.println("========================================\n");
}

void listAudioFiles() {
  Serial.println("\n========================================");
  Serial.println("AUDIO FILES:");
  Serial.println("========================================");
  File rootDir = SD.open("/");
  if (rootDir && rootDir.isDirectory()) {
    int count = 0;
    File file = rootDir.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        String filename = String(file.name());
        if (filename.endsWith(".wav")) {
          count++;
          Serial.printf("%d. %s (%u bytes)\n", count, file.name(), file.size());
        }
      }
      file = rootDir.openNextFile();
    }
    if (count == 0) {
      Serial.println("No audio files found.");
    } else {
      Serial.printf("\nTotal: %d audio files\n", count);
    }
    rootDir.close();
  } else {
    Serial.println("Failed to open root directory");
  }
  Serial.println("========================================\n");
}

void listAllFiles() {
  Serial.println("\n========================================");
  Serial.println("ALL RECORDED FILES:");
  Serial.println("========================================");
  
  Serial.println("\nVIDEO FILES (/video):");
  File videoDir = SD.open("/video");
  if (videoDir && videoDir.isDirectory()) {
    int count = 0;
    File file = videoDir.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        count++;
        Serial.printf("  %d. %s (%u bytes)\n", count, file.name(), file.size());
      }
      file = videoDir.openNextFile();
    }
    if (count == 0) {
      Serial.println("  No video files found.");
    } else {
      Serial.printf("  Subtotal: %d video files\n", count);
    }
    videoDir.close();
  }
  
  Serial.println("\nAUDIO FILES (/):");
  File rootDir = SD.open("/");
  if (rootDir && rootDir.isDirectory()) {
    int count = 0;
    File file = rootDir.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        String filename = String(file.name());
        if (filename.endsWith(".wav")) {
          count++;
          Serial.printf("  %d. %s (%u bytes)\n", count, file.name(), file.size());
        }
      }
      file = rootDir.openNextFile();
    }
    if (count == 0) {
      Serial.println("  No audio files found.");
    } else {
      Serial.printf("  Subtotal: %d audio files\n", count);
    }
    rootDir.close();
  }
  
  Serial.println("========================================\n");
}

// ============================================
// OTA UPDATE FUNCTIONS
// ============================================
void initOTA() {
  ArduinoOTA.setHostname("esp32-cam-streamer");
  ArduinoOTA.setPassword("admin");
  
  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("Start OTA updating " + type);
    currentState = STATE_INIT;
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA Update complete!");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
    currentState = STATE_ERROR;
  });
  
  ArduinoOTA.begin();
  Serial.println("✓ OTA updates enabled");
}

// ============================================
// BLE CALLBACKS & FUNCTIONS
// ============================================
class ServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("BLE Client Connected");
    if (pStatusCharacteristic) {
      String status = "Connected|Recording:" + String(bleRecordingActive ? "ON" : "OFF");
      pStatusCharacteristic->setValue(status.c_str());
      pStatusCharacteristic->notify();
    }
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("BLE Client Disconnected");
    BLEDevice::startAdvertising();
  }
};

class ControlCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = pCharacteristic->getValue().c_str();
    
    if (value.length() > 0) {
      String command = value;
      command.trim();
      Serial.println("BLE Command: " + command);
      
      if (command == "START") {
        if (!bleRecordingActive) {
          bleRecordingActive = true;
          recordingMode = true;
          Serial.println("📹 BLE: Recording STARTED");
          
          if (pStatusCharacteristic) {
            pStatusCharacteristic->setValue("Recording:ON");
            pStatusCharacteristic->notify();
          }
          
          xTaskCreatePinnedToCore(
            recordingTask,
            "SDRecording",
            8192,
            NULL,
            1,
            NULL,
            0
          );
        } else {
          Serial.println("⚠️  Recording already active");
        }
      }
      else if (command == "STOP") {
        bleRecordingActive = false;
        recordingMode = false;
        Serial.println("⏹️  BLE: Recording STOPPED");
        
        if (pStatusCharacteristic) {
          pStatusCharacteristic->setValue("Recording:OFF");
          pStatusCharacteristic->notify();
        }
      }
      else if (command == "STATUS") {
        String status = "Frames:" + String(frameCount) + "|Audio:" + String(audioFileCount);
        if (pStatusCharacteristic) {
          pStatusCharacteristic->setValue(status.c_str());
          pStatusCharacteristic->notify();
        }
      }
      else if (command == "LIST_VIDEO") {
        listVideoRequested = true;
      }
      else if (command == "LIST_AUDIO") {
        listAudioRequested = true;
      }
      else if (command == "LIST_ALL") {
        listAllRequested = true;
      }
      else if (command == "AUDIO_ONLY") {
        audioOnlyMode = true;
        videoOnlyMode = false;
        bothMode = false;
        Serial.println("🎙️  Mode: AUDIO ONLY");
      }
      else if (command == "VIDEO_ONLY") {
        audioOnlyMode = false;
        videoOnlyMode = true;
        bothMode = false;
        Serial.println("📹 Mode: VIDEO ONLY");
      }
      else if (command == "BOTH") {
        audioOnlyMode = false;
        videoOnlyMode = false;
        bothMode = true;
        Serial.println("🎥 Mode: AUDIO + VIDEO");
      }
    }
  }
};

class WiFiCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = pCharacteristic->getValue().c_str();
    
    if (value.length() > 0) {
      String command = value;
      command.trim();
      
      if (command == "ENABLE_WIFI") {
        wifiRequested = true;
        Serial.println("📡 WiFi mode requested via BLE");
      }
    }
  }
};

bool initBLE() {
  Serial.println("Initializing BLE...");
  
  BLEDevice::init("ESP32-CAM-BLE");
  
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  
  BLEService *pService = pServer->createService(BLE_SERVICE_UUID);
  
  pControlCharacteristic = pService->createCharacteristic(
    BLE_CONTROL_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pControlCharacteristic->setCallbacks(new ControlCallbacks());
  
  pStatusCharacteristic = pService->createCharacteristic(
    BLE_STATUS_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pStatusCharacteristic->addDescriptor(new BLE2902());
  pStatusCharacteristic->setValue("Ready");
  
  pWiFiCharacteristic = pService->createCharacteristic(
    BLE_WIFI_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pWiFiCharacteristic->setCallbacks(new WiFiCallbacks());
  
  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  
  Serial.println("✓ BLE Server started");
  return true;
}

bool initMicrophone() {
  // Setup PDM pins: clock=42, data=41 (ESP_I2S 3.0)
  I2S.setPinsPdmRx(PDM_CLK_PIN, PDM_DATA_PIN);
  
  // Start I2S in PDM RX mode at 16 kHz with 16-bit samples, mono
  if (!I2S.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("Failed to initialize I2S!");
    return false;
  }

  Serial.println("✓ Microphone initialized (16kHz 16-bit PDM)");
  return true;
}

bool initSDCard() {
  Serial.println("Initializing SD Card...");
  
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD Card Mount Failed!");
    return false;
  }
  
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD Card attached");
    return false;
  }
  
  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) Serial.println("MMC");
  else if (cardType == CARD_SD) Serial.println("SDSC");
  else if (cardType == CARD_SDHC) Serial.println("SDHC");
  else Serial.println("UNKNOWN");
  
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  uint64_t usedSpace = SD.usedBytes() / (1024 * 1024);
  uint64_t totalSpace = SD.totalBytes() / (1024 * 1024);
  uint64_t freeSpace = totalSpace - usedSpace;
  
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
  Serial.printf("Total Space: %lluMB\n", totalSpace);
  Serial.printf("Used Space: %lluMB\n", usedSpace);
  Serial.printf("Free Space: %lluMB\n", freeSpace);
  
  if (!SD.exists("/video")) {
    if (SD.mkdir("/video")) {
      Serial.println("Created /video directory");
    }
  }
  if (!SD.exists("/audio")) {
    if (SD.mkdir("/audio")) {
      Serial.println("Created /audio directory");
    }
  }
  
  Serial.println("✓ SD Card initialized");
  
  sdMutex = xSemaphoreCreateMutex();
  if (sdMutex == NULL) {
    Serial.println("⚠️  Failed to create SD mutex");
  } else {
    Serial.println("✓ SD mutex created");
  }
  
  return true;
}

bool saveFrameToSD(camera_fb_t *fb) {
  if (!fb) {
    Serial.println("❌ saveFrameToSD: No frame buffer provided");
    return false;
  }
  
  if (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    char filename[64];
    if (timeInitialized) {
      String timestamp = getTimestamp();
      sprintf(filename, "/video/%s_frame_%06lu.jpg", timestamp.c_str(), frameCount);
    } else {
      sprintf(filename, "/video/frame_%06lu.jpg", frameCount);
    }
    
    Serial.printf("📹 Saving frame %lu to: %s (%d bytes)...\n", frameCount, filename, fb->len);
    
    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
      Serial.printf("❌ Failed to open file for writing: %s\n", filename);
      currentState = STATE_ERROR;
      xSemaphoreGive(sdMutex);
      return false;
    }
    
    size_t written = file.write(fb->buf, fb->len);
    file.close();
    
    xSemaphoreGive(sdMutex);
    
    if (written != fb->len) {
      Serial.printf("❌ Write error: wrote %d of %d bytes\n", written, fb->len);
      return false;
    }
    
    Serial.printf("✓ Frame %lu saved successfully\n", frameCount);
    frameCount++;
    
    return true;
  } else {
    Serial.println("⚠️  Failed to acquire SD mutex for video");
    return false;
  }
}

void generate_wav_header(uint8_t *wav_header, uint32_t wav_size, uint32_t sample_rate) {
  uint32_t file_size = wav_size + WAV_HEADER_SIZE - 8;
  uint32_t byte_rate = sample_rate * SAMPLE_BITS / 8;
  const uint8_t set_wav_header[] = {
    'R', 'I', 'F', 'F',
    file_size, file_size >> 8, file_size >> 16, file_size >> 24,
    'W', 'A', 'V', 'E',
    'f', 'm', 't', ' ',
    0x10, 0x00, 0x00, 0x00,
    0x01, 0x00,
    0x01, 0x00,
    sample_rate, sample_rate >> 8, sample_rate >> 16, sample_rate >> 24,
    byte_rate, byte_rate >> 8, byte_rate >> 16, byte_rate >> 24,
    0x02, 0x00,
    0x10, 0x00,
    'd', 'a', 't', 'a',
    wav_size, wav_size >> 8, wav_size >> 16, wav_size >> 24,
  };
  memcpy(wav_header, set_wav_header, sizeof(set_wav_header));
}

void record_wav() {
  uint32_t sample_size = 0;
  uint32_t record_size = (SAMPLE_RATE * SAMPLE_BITS / 8) * RECORD_TIME;
  uint8_t *rec_buffer = NULL;
  
  Serial.printf("Ready to start recording %d seconds...\n", RECORD_TIME);
  
  rec_buffer = (uint8_t *)ps_malloc(record_size);
  if (rec_buffer == NULL) {
    Serial.printf("malloc failed!\n");
    return;
  }
  Serial.printf("Buffer: %d bytes\n", ESP.getPsramSize() - ESP.getFreePsram());
  
  // Read samples from I2S one at a time (ESP_I2S 3.0)
  uint32_t bytes_read = 0;
  while (bytes_read < record_size) {
    int sample = I2S.read();
    if (sample != -1 && sample != 0 && sample != 1) {
      // Store sample as 16-bit value
      rec_buffer[bytes_read++] = sample & 0xFF;
      rec_buffer[bytes_read++] = (sample >> 8) & 0xFF;
    }
  }
  sample_size = bytes_read;
  
  if (sample_size == 0) {
    Serial.printf("Record Failed!\n");
    free(rec_buffer);
    return;
  } else {
    Serial.printf("Record %d bytes\n", sample_size);
  }
  
  for (uint32_t i = 0; i < sample_size; i += SAMPLE_BITS/8) {
    (*(uint16_t *)(rec_buffer+i)) <<= VOLUME_GAIN;
  }
  
  if (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
    char filename[64];
    if (timeInitialized) {
      String timestamp = getTimestamp();
      sprintf(filename, "/%s_%s_%06lu.wav", WAV_FILE_NAME, timestamp.c_str(), audioFileCount++);
    } else {
      sprintf(filename, "/%s_%06lu.wav", WAV_FILE_NAME, audioFileCount++);
    }
    
    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open file for writing");
      xSemaphoreGive(sdMutex);
      free(rec_buffer);
      return;
    }
    
    uint8_t wav_header[WAV_HEADER_SIZE];
    generate_wav_header(wav_header, record_size, SAMPLE_RATE);
    file.write(wav_header, WAV_HEADER_SIZE);
    
    Serial.printf("Writing to the file ...\n");
    size_t written = file.write(rec_buffer, record_size);
    file.close();
    
    xSemaphoreGive(sdMutex);
    
    if (written != record_size) {
      Serial.printf("Write file Failed! Wrote %d of %d bytes\n", written, record_size);
    } else {
      Serial.printf("Recording saved: %s\n", filename);
    }
  } else {
    Serial.println("⚠️  Failed to acquire SD mutex for audio");
  }
  
  free(rec_buffer);
}

void recordingTask(void *parameter) {
  Serial.println("Starting 10-second interval recording...");
  Serial.println("========================================");
  
  if (audioOnlyMode) {
    Serial.println("Mode: AUDIO ONLY - Recording audio files every 10 seconds");
  } else if (videoOnlyMode) {
    Serial.println("Mode: VIDEO ONLY - Recording 10-second video clips");
  } else {
    Serial.println("Mode: AUDIO + VIDEO - Recording 10-second clips of both");
  }
  Serial.println("========================================\n");
  
  unsigned long lastAudioTime = 0;
  unsigned long lastVideoTime = 0;
  const unsigned long clipInterval = 10000;
  
  currentState = STATE_RECORDING;
  
  while (recordingMode) {
    unsigned long currentTime = millis();
    
    static unsigned long lastBatteryCheck = 0;
    if (currentTime - lastBatteryCheck > 60000) {
      checkBatteryStatus();
      lastBatteryCheck = currentTime;
      if (batteryLow) {
        Serial.println("Battery low - stopping recording");
        break;
      }
    }
    
    if (currentTime - lastCleanupTime > CLEANUP_INTERVAL) {
      cleanupOldFiles();
      lastCleanupTime = currentTime;
    }
    
    if (!audioOnlyMode && (currentTime - lastVideoTime >= clipInterval)) {
      Serial.println("📹 Recording 10-second video clip...");
      unsigned long clipStart = millis();
      unsigned long clipFrameCount = 0;
      
      while (millis() - clipStart < 10000 && recordingMode) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
          Serial.println("❌ Error getting framebuffer!");
          vTaskDelay(pdMS_TO_TICKS(100));
        } else {
          bool saved = saveFrameToSD(fb);
          esp_camera_fb_return(fb);
          
          if (saved) {
            clipFrameCount++;
          } else {
            Serial.println("⚠️  Frame save failed, continuing...");
          }
        }
        vTaskDelay(1);
      }
      
      Serial.printf("✓ Video clip complete: %lu frames captured\n", clipFrameCount);
      lastActivityTime = currentTime;
      lastVideoTime = currentTime;
    }
    
    if (!videoOnlyMode && (currentTime - lastAudioTime >= clipInterval)) {
      Serial.println("🎙️  Recording audio clip...");
      record_wav();
      lastActivityTime = currentTime;
      lastAudioTime = currentTime;
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  
  Serial.println("========================================");
  Serial.println("Recording stopped");
  Serial.printf("Total frames captured: %lu\n", frameCount);
  Serial.printf("Total audio files: %lu\n", audioFileCount);
  Serial.println("========================================");
  currentState = STATE_INIT;
  vTaskDelete(NULL);
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected\n", client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
  }
}

void audioTask(void *parameter) {
  const size_t bufferSize = 256; // 256 samples = 512 bytes
  uint8_t audioBuffer[bufferSize * 2];
  
  while (true) {
    // Read audio samples one at a time (ESP_I2S 3.0)
    size_t bytesRead = 0;
    for (size_t i = 0; i < bufferSize && bytesRead < bufferSize * 2; i++) {
      int sample = I2S.read();
      if (sample != -1 && sample != 0 && sample != 1) {
        audioBuffer[bytesRead++] = sample & 0xFF;
        audioBuffer[bytesRead++] = (sample >> 8) & 0xFF;
      }
    }
    
    if (bytesRead > 0 && ws.count() > 0) {
      ws.binaryAll(audioBuffer, bytesRead);
    }
    
    vTaskDelay(1);
  }
}

void handleStream(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginChunkedResponse(
    "multipart/x-mixed-replace; boundary=frame",
    [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
      camera_fb_t *fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("Camera capture failed");
        return 0;
      }
      
      size_t len = 0;
      if (index == 0) {
        len = snprintf((char *)buffer, maxLen,
          "--frame\r\n"
          "Content-Type: image/jpeg\r\n"
          "Content-Length: %u\r\n\r\n",
          fb->len);
      }
      
      size_t remaining = fb->len - (index - len);
      if (remaining > 0) {
        size_t copyLen = (remaining > maxLen) ? maxLen : remaining;
        memcpy(buffer, fb->buf + (index - len), copyLen);
        len = copyLen;
      }
      
      esp_camera_fb_return(fb);
      return len;
    }
  );
  
  request->send(response);
}

const char* html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32-CAM Stream</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial; text-align: center; background: #1a1a1a; color: white; padding: 20px; }
    h1 { margin-bottom: 20px; }
    img { max-width: 100%; border: 2px solid #444; border-radius: 8px; }
    .controls { margin: 20px 0; }
    button { padding: 10px 20px; font-size: 16px; background: #4CAF50; color: white; border: none; border-radius: 5px; cursor: pointer; margin: 0 5px; }
    button:hover { background: #45a049; }
    .info { margin-top: 20px; font-size: 14px; color: #aaa; }
  </style>
</head>
<body>
  <h1>📹 ESP32-CAM Video & Audio Stream</h1>
  <img src="/stream" alt="Video Stream">
  <div class="controls">
    <button onclick="location.reload()">🔄 Refresh</button>
  </div>
  <div class="info">
    <p>XIAO ESP32S3 Sense - MJPEG Video + PDM Audio</p>
  </div>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n========================================");
  Serial.println("XIAO ESP32S3 Camera & Audio Streamer v3.0");
  Serial.println("========================================\n");
  
  Serial.println("Checking PSRAM...");
  if (psramFound()) {
    Serial.printf("✓ PSRAM found: %d bytes total, %d bytes free\n", 
                  ESP.getPsramSize(), ESP.getFreePsram());
  } else {
    Serial.println("❌ PSRAM not found!");
    Serial.println("Enable PSRAM in Arduino IDE: Tools → PSRAM → OPI PSRAM");
    while (1) { delay(1000); }
  }
  
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  
  lastActivityTime = millis();
  
  Serial.println("\nInitializing camera...");
  if (!initCamera()) {
    Serial.println("❌ Camera initialization failed!");
    currentState = STATE_ERROR;
    while (1) { 
      updateStatusLED();
      delay(100);
    }
  }
  Serial.println("✓ Camera initialized");
  
  Serial.println("\nInitializing microphone...");
  if (!initMicrophone()) {
    Serial.println("⚠️  Microphone initialization failed!");
  } else {
    Serial.println("✓ Microphone initialized");
  }
  
  Serial.println("\nInitializing SD Card...");
  if (!initSDCard()) {
    Serial.println("❌ SD Card initialization failed!");
    while (1) { 
      updateStatusLED();
      delay(1000);
    }
  }
  
  Serial.println("\n========================================");
  Serial.println("Starting in BLE CONTROL MODE");
  Serial.println("========================================");
  
  if (!initBLE()) {
    Serial.println("⚠️  BLE initialization failed!");
  }
  
  Serial.println("\n📱 BLE Control Ready!");
  Serial.println("========================================");
  Serial.println("Connect via BLE app (nRF Connect, LightBlue)");
  Serial.println("Device: ESP32-CAM-BLE");
  Serial.println("\nCommands: START, STOP, STATUS");
  Serial.println("Modes: AUDIO_ONLY, VIDEO_ONLY, BOTH");
  Serial.println("Lists: LIST_VIDEO, LIST_AUDIO, LIST_ALL");
  Serial.println("========================================\n");
  
  currentState = STATE_INIT;
}

void startWiFiMode() {
  Serial.println("\nSwitching to WiFi Mode...");
  
  if (bleEnabled) {
    BLEDevice::deinit(true);
    bleEnabled = false;
    Serial.println("✓ BLE disabled");
  }
  
  Serial.println("Connecting to WiFi...");
  currentState = STATE_WIFI_CONNECTING;
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi connection failed!");
    initBLE();
    bleEnabled = true;
    return;
  }
  
  currentState = STATE_WIFI_CONNECTED;
  IPAddress IP = WiFi.localIP();
  Serial.println("✓ WiFi Connected!");
  Serial.printf("IP: http://%s\n", IP.toString().c_str());
  
  initTime();
  initOTA();
  
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", html);
  });
  
  server.on("/stream", HTTP_GET, handleStream);
  
  server.begin();
  Serial.println("✓ Web server started");
  
  currentState = STATE_STREAMING;
  
  xTaskCreatePinnedToCore(audioTask, "AudioStream", 4096, NULL, 1, NULL, 0);
}

void loop() {
  updateStatusLED();
  
  if (listVideoRequested) {
    listVideoRequested = false;
    listVideoFiles();
  }
  if (listAudioRequested) {
    listAudioRequested = false;
    listAudioFiles();
  }
  if (listAllRequested) {
    listAllRequested = false;
    listAllFiles();
  }
  
  if (wifiRequested && bleEnabled) {
    wifiRequested = false;
    startWiFiMode();
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }
  
  if (bleEnabled && deviceConnected && pStatusCharacteristic) {
    static unsigned long lastBLEUpdate = 0;
    if (millis() - lastBLEUpdate > 5000) {
      String status = "Frames:" + String(frameCount) + 
                     "|Audio:" + String(audioFileCount) + 
                     "|Recording:" + String(bleRecordingActive ? "ON" : "OFF");
      pStatusCharacteristic->setValue(status.c_str());
      pStatusCharacteristic->notify();
      lastBLEUpdate = millis();
    }
  }
  
  static unsigned long lastBatteryCheck = 0;
  if (millis() - lastBatteryCheck > 60000) {
    checkBatteryStatus();
    lastBatteryCheck = millis();
  }
  
  checkIdleTimeout();
  
  if (recordingMode) {
    static unsigned long lastStatus = 0;
    if (millis() - lastStatus > 30000) {
      uint64_t totalSpace = SD.totalBytes() / (1024 * 1024);
      uint64_t usedSpace = SD.usedBytes() / (1024 * 1024);
      uint64_t freeSpace = totalSpace - usedSpace;
      
      Serial.println("========================================");
      Serial.println("Recording Status:");
      Serial.printf("  Frames: %lu | Audio: %lu\n", frameCount, audioFileCount);
      Serial.printf("  SD Free: %lluMB / %lluMB\n", freeSpace, totalSpace);
      Serial.println("========================================");
      lastStatus = millis();
    }
  }
  
  delay(100);
}
