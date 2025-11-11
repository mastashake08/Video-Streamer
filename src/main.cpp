#include <Arduino.h>
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
#include "USB.h"
#include "USBMSC.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
// #include <Wire.h>
// #include <Adafruit_GFX.h>
// #include <Adafruit_SSD1306.h>
#include "esp_camera.h"
#include "esp_sleep.h"

// I2S instance for PDM microphone
I2SClass I2S;

// USB Mass Storage instance
USBMSC msc;

// USB MSC state
bool usbMscEnabled = false;
bool usbMscMounted = false;

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
bool recordingMode = false;  // Set to true when WiFi fails or BLE command
bool bleRecordingActive = false;  // BLE-controlled recording state
unsigned long frameCount = 0;
unsigned long audioFileCount = 0;

// Recording mode flags
bool audioOnlyMode = false;   // Record only audio
bool videoOnlyMode = false;   // Record only video (motion-triggered)
bool bothMode = true;         // Record both audio and video (default)

// File listing flags
volatile bool listVideoRequested = false;
volatile bool listAudioRequested = false;
volatile bool listAllRequested = false;

// SD Card mutex for thread-safe access
SemaphoreHandle_t sdMutex = NULL;

// ============================================
// Motion Detection Configuration (DISABLED)
// ============================================
// Motion detection temporarily disabled - using continuous recording
// #define MOTION_THRESHOLD 15      // Pixel difference threshold (0-255)
// #define MOTION_MIN_PIXELS 1000   // Minimum pixels changed to trigger motion
// bool motionDetected = false;
// camera_fb_t *lastFrame = NULL;

// ============================================
// Time & NTP Configuration
// ============================================
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;      // Adjust for your timezone
const int daylightOffset_sec = 0;  // Adjust for daylight saving
struct tm timeinfo;
bool timeInitialized = false;

// ============================================
// File Management Configuration
// ============================================
#define SD_MIN_FREE_SPACE_MB 100  // Minimum free space before cleanup
#define MAX_FILE_AGE_HOURS 24     // Delete files older than this
unsigned long lastCleanupTime = 0;
#define CLEANUP_INTERVAL 3600000  // Run cleanup every hour

// ============================================
// Power Management Configuration
// ============================================
#define BATTERY_PIN 1             // ADC pin for battery monitoring
#define LOW_BATTERY_VOLTAGE 3.3   // Low battery threshold
#define IDLE_SLEEP_TIMEOUT 300000 // Sleep after 5 min idle (ms)
unsigned long lastActivityTime = 0;
bool batteryLow = false;

// ============================================
// Status LED Configuration
// ============================================
// LED_BUILTIN is already defined by the board variant (pin 21)
#define LED_BLINK_RECORDING 200   // Fast blink when recording
#define LED_BLINK_WIFI 1000       // Slow blink when WiFi connected
#define LED_BLINK_ERROR 100       // Very fast blink on error
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
#define SD_CS_PIN 21     // Chip Select pin

// // OLED Display configuration (DISABLED - defective display)
// #define SCREEN_WIDTH 128
// #define SCREEN_HEIGHT 64
// #define OLED_RESET -1
// #define SCREEN_ADDRESS 0x3C
// #define SDA_PIN 5
// #define SCL_PIN 6
// Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Web server on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/audio");

// Audio configuration for PDM microphone (per Seeed documentation)
#define PDM_DATA_PIN 41  // GPIO 41: PDM Microphone DATA
#define PDM_CLK_PIN 42   // GPIO 42: PDM Microphone CLK
#define SAMPLE_RATE 16000U
#define SAMPLE_BITS 16
#define WAV_HEADER_SIZE 44
#define VOLUME_GAIN 2
#define RECORD_TIME 10  // seconds per file
#define WAV_FILE_NAME "recording"

// ============================================
// USB MASS STORAGE CALLBACKS
// ============================================
// Virtual disk buffer for USB MSC (PSRAM-backed RAM disk for file transfer)
// Disk size is chosen at runtime based on available PSRAM. Sector size is fixed at 512.
static uint32_t disk_sector_count = 0; // set when RAM disk is allocated
static const uint32_t disk_sector_size = 512;
static uint8_t* msc_disk = nullptr;

// Read callback - reads 512-byte sectors from virtual disk
static int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
  if (!usbMscMounted || !msc_disk || disk_sector_count == 0) {
    return 0;
  }

  // Check bounds
  if (lba >= disk_sector_count) {
    return 0;
  }

  // Copy from virtual disk to buffer
  uint8_t* addr = msc_disk + (uint32_t)lba * disk_sector_size + offset;
  memcpy(buffer, addr, bufsize);

  return (int32_t)bufsize;
}

// Write callback - writes 512-byte sectors to virtual disk
static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
  if (!usbMscMounted || !msc_disk || disk_sector_count == 0) {
    return 0;
  }

  // Check bounds
  if (lba >= disk_sector_count) {
    return 0;
  }

  // Copy from buffer to virtual disk
  uint8_t* addr = msc_disk + (uint32_t)lba * disk_sector_size + offset;
  memcpy(addr, buffer, bufsize);

  return (int32_t)bufsize;
}

// Start/Stop callback - called when host mounts/unmounts the device
static bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
  Serial.printf("USB MSC: %s\n", start ? "Mounted" : "Unmounted");
  usbMscMounted = start;
  
  if (start) {
    // Stop recording when USB MSC is mounted to prevent file system conflicts
    if (recordingMode) {
      Serial.println("⚠️  Stopping recording - USB MSC mounted");
      bleRecordingActive = false;
      recordingMode = false;
    }
  }
  
  return true;
}

// Initialize USB Mass Storage
bool initUSBMSC() {
  if (usbMscEnabled) {
    Serial.println("USB MSC already enabled");
    return true;
  }
  
  Serial.println("Initializing USB Mass Storage...");
  Serial.println("========================================");
  
  // Report PSRAM status before allocation
  if (psramFound()) {
    uint32_t psramTotal = ESP.getPsramSize();
    uint32_t psramFree = ESP.getFreePsram();
    uint32_t psramUsed = psramTotal - psramFree;
    Serial.printf("PSRAM Status BEFORE allocation:\n");
    Serial.printf("  Total: %u KB (%u bytes)\n", psramTotal / 1024, psramTotal);
    Serial.printf("  Used:  %u KB (%u bytes)\n", psramUsed / 1024, psramUsed);
    Serial.printf("  Free:  %u KB (%u bytes)\n", psramFree / 1024, psramFree);
    Serial.println("----------------------------------------");
  } else {
    Serial.println("❌ PSRAM not found - USB MSC requires PSRAM!");
    return false;
  }
  
  // Allocate RAM disk in PSRAM (try progressively smaller sizes)
  if (!msc_disk) {
    const uint32_t candidates[] = {
      16UL * 1024UL * 1024UL, // 16 MB
      8UL  * 1024UL * 1024UL, // 8 MB
      4UL  * 1024UL * 1024UL, // 4 MB
      2UL  * 1024UL * 1024UL, // 2 MB
      1UL  * 1024UL * 1024UL, // 1 MB
      512UL * 1024UL,         // 512 KB
      256UL * 1024UL          // 256 KB (minimum)
    };
    const size_t nc = sizeof(candidates) / sizeof(candidates[0]);

    Serial.println("Attempting progressive RAM disk allocation...");
    for (size_t i = 0; i < nc; ++i) {
      uint32_t bytes = candidates[i];
      uint32_t sectors = bytes / disk_sector_size;
      Serial.printf("[%d/%d] Trying %u KB...", (int)(i+1), (int)nc, bytes / 1024UL);
      
      msc_disk = (uint8_t*)ps_malloc(bytes);
      if (msc_disk) {
        disk_sector_count = sectors;
        memset(msc_disk, 0, bytes);
        Serial.printf(" ✓ SUCCESS\n");
        Serial.println("----------------------------------------");
        Serial.printf("✓ RAM disk allocated: %u KB\n", bytes / 1024UL);
        Serial.printf("  Sectors: %u x %u bytes\n", disk_sector_count, disk_sector_size);
        
        // Report PSRAM status after allocation
        uint32_t psramFreeAfter = ESP.getFreePsram();
        uint32_t psramUsedAfter = ESP.getPsramSize() - psramFreeAfter;
        Serial.println("----------------------------------------");
        Serial.printf("PSRAM Status AFTER allocation:\n");
        Serial.printf("  Used:  %u KB (%u bytes)\n", psramUsedAfter / 1024, psramUsedAfter);
        Serial.printf("  Free:  %u KB (%u bytes)\n", psramFreeAfter / 1024, psramFreeAfter);
        Serial.printf("  Delta: +%u KB allocated\n", bytes / 1024UL);
        
        // Send status via BLE if connected
        if (deviceConnected && pStatusCharacteristic) {
          String status = "USB:Enabled|RAMDisk:" + String(bytes / 1024) + "KB";
          pStatusCharacteristic->setValue(status.c_str());
          pStatusCharacteristic->notify();
        }
        
        break;
      } else {
        Serial.printf(" ❌ FAILED\n");
      }
    }

    if (!msc_disk) {
      Serial.println("========================================");
      Serial.println("❌ Failed to allocate any RAM disk size");
      Serial.println("Not enough PSRAM available!");
      Serial.println("----------------------------------------");
      Serial.println("💡 Recommendation: Use Web File Browser instead");
      Serial.println("   Access files via WiFi at http://<IP>/files");
      Serial.println("   No PSRAM required, unlimited file size support");
      Serial.println("========================================");
      
      // Send failure notification via BLE
      if (deviceConnected && pStatusCharacteristic) {
        pStatusCharacteristic->setValue("USB:Failed|UseBrowser");
        pStatusCharacteristic->notify();
      }
      
      return false;
    }
  }
  
  // Configure MSC device
  msc.vendorID("Seeed");
  msc.productID("XIAO ESP32S3");
  msc.productRevision("1.0");
  msc.onRead(onRead);
  msc.onWrite(onWrite);
  msc.onStartStop(onStartStop);
  msc.mediaPresent(true);
  
  if (!msc.begin(disk_sector_count, disk_sector_size)) {
    Serial.println("❌ Failed to start USB MSC");
    return false;
  }

  USB.begin();

  usbMscEnabled = true;
  Serial.println("✓ USB Mass Storage enabled");
  Serial.printf("  %u KB RAM disk exposed as USB drive\n", (disk_sector_count * disk_sector_size) / 1024U);
  Serial.println("  ⚠️  This is a virtual disk for file transfers");
  Serial.println("  ⚠️  Format as FAT32 first, then copy files to/from SD card manually");
  Serial.println("  Safely eject the drive before sending DISABLE_USB command");
  
  return true;
}

// Disable USB Mass Storage
void disableUSBMSC() {
  if (!usbMscEnabled) {
    return;
  }
  
  Serial.println("Disabling USB Mass Storage...");
  
  // Wait for unmount
  if (usbMscMounted) {
    Serial.println("⚠️  Please eject the USB drive from your computer first!");
    Serial.println("Waiting for unmount...");
    
    unsigned long timeout = millis() + 30000; // 30 second timeout
    while (usbMscMounted && millis() < timeout) {
      delay(100);
    }
    
    if (usbMscMounted) {
      Serial.println("⚠️  Timeout - force disabling USB MSC");
    }
  }
  
  msc.end();
  // Note: USB.end() not available - USB peripheral stays active
  
  // Free RAM disk (optional - keep it allocated for next time)
  // if (msc_disk) {
  //   free(msc_disk);
  //   msc_disk = nullptr;
  // }
  
  usbMscEnabled = false;
  usbMscMounted = false;
  
  Serial.println("✓ USB Mass Storage disabled");
  Serial.println("  Recording can now be resumed");
  
  // Small delay
  delay(500);
}

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
  config.pixel_format = PIXFORMAT_JPEG; // for streaming
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  
  // If PSRAM IC present, init with UXGA resolution and higher JPEG quality
  // for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
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
    // Initial sensors are flipped vertically and colors are a bit saturated
    if (s->id.PID == OV3660_PID) {
      s->set_vflip(s, 1);        // flip it back
      s->set_brightness(s, 1);   // up the brightness just a bit
      s->set_saturation(s, -2);  // lower the saturation
    }
    // Drop down frame size for higher initial frame rate
    if (config.pixel_format == PIXFORMAT_JPEG) {
      s->set_framesize(s, FRAMESIZE_QVGA);
    }
  }
  
  Serial.println("Camera initialized successfully");
  return true;
}

// ============================================
// MOTION DETECTION (DISABLED)
// ============================================
// Motion detection functions temporarily removed - using continuous recording
/*
bool detectMotion(camera_fb_t *currentFrame) {
  // Motion detection code removed
}

void updateLastFrame(camera_fb_t *frame) {
  // Motion detection code removed
}
*/

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
    return String(millis()); // Fallback to millis
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
  
  // Delete oldest files from video directory
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
  
  // Update free space
  freeSpace = (SD.totalBytes() - SD.usedBytes()) / (1024 * 1024);
  Serial.printf("Free space after cleanup: %lluMB\n", freeSpace);
}

// ============================================
// POWER MANAGEMENT
// ============================================
float getBatteryVoltage() {
  // Read battery voltage from ADC
  int adcValue = analogRead(BATTERY_PIN);
  // Convert ADC to voltage (adjust based on your voltage divider)
  float voltage = (adcValue / 4095.0) * 3.3 * 2.0; // Assuming 2:1 divider
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
  
  // Close all files
  SD.end();
  
  // Configure wakeup
  esp_sleep_enable_timer_wakeup(sleepTimeSeconds * 1000000ULL);
  
  // Enter deep sleep
  esp_deep_sleep_start();
}

void checkIdleTimeout() {
  if (currentState == STATE_STREAMING || currentState == STATE_RECORDING) {
    lastActivityTime = millis();
    return;
  }
  
  if (millis() - lastActivityTime > IDLE_SLEEP_TIMEOUT) {
    Serial.println("Idle timeout - entering sleep mode");
    enterDeepSleep(60); // Sleep for 1 minute
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

// ============================================
// FORWARD DECLARATIONS
// ============================================
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
  
  // List video files
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
  
  // List audio files
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
  ArduinoOTA.setPassword("admin"); // Change this!
  
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
  Serial.println("  Use Arduino IDE or platformio to upload firmware wirelessly");
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
    // Restart advertising
    BLEDevice::startAdvertising();
  }
};

class ControlCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    
    if (value.length() > 0) {
      String command = String(value.c_str());
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
          
          // Start recording task
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
        Serial.println("📋 Video file list requested...");
      }
      else if (command == "LIST_AUDIO") {
        listAudioRequested = true;
        Serial.println("📋 Audio file list requested...");
      }
      else if (command == "LIST_ALL") {
        listAllRequested = true;
        Serial.println("📋 All files list requested...");
      }
      else if (command == "AUDIO_ONLY") {
        audioOnlyMode = true;
        videoOnlyMode = false;
        bothMode = false;
        Serial.println("🎙️  Mode: AUDIO ONLY");
        
        if (pStatusCharacteristic) {
          pStatusCharacteristic->setValue("Mode:AudioOnly");
          pStatusCharacteristic->notify();
        }
      }
      else if (command == "VIDEO_ONLY") {
        audioOnlyMode = false;
        videoOnlyMode = true;
        bothMode = false;
        Serial.println("📹 Mode: VIDEO ONLY");
        
        if (pStatusCharacteristic) {
          pStatusCharacteristic->setValue("Mode:VideoOnly");
          pStatusCharacteristic->notify();
        }
      }
      else if (command == "BOTH") {
        audioOnlyMode = false;
        videoOnlyMode = false;
        bothMode = true;
        Serial.println("🎥 Mode: AUDIO + VIDEO");
        
        if (pStatusCharacteristic) {
          pStatusCharacteristic->setValue("Mode:Both");
          pStatusCharacteristic->notify();
        }
      }
      else if (command == "ENABLE_USB") {
        if (!usbMscEnabled) {
          if (initUSBMSC()) {
            if (pStatusCharacteristic) {
              pStatusCharacteristic->setValue("USB:Enabled");
              pStatusCharacteristic->notify();
            }
            Serial.println("💾 USB Mass Storage ENABLED");
            Serial.println("   Connect USB cable to access SD card files");
          } else {
            if (pStatusCharacteristic) {
              pStatusCharacteristic->setValue("USB:Failed");
              pStatusCharacteristic->notify();
            }
          }
        } else {
          Serial.println("⚠️  USB MSC already enabled");
        }
      }
      else if (command == "DISABLE_USB") {
        if (usbMscEnabled) {
          disableUSBMSC();
          if (pStatusCharacteristic) {
            pStatusCharacteristic->setValue("USB:Disabled");
            pStatusCharacteristic->notify();
          }
          Serial.println("💾 USB Mass Storage DISABLED");
        } else {
          Serial.println("⚠️  USB MSC not enabled");
        }
      }
    }
  }
};

class WiFiCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    
    if (value.length() > 0) {
      String command = String(value.c_str());
      command.trim();
      Serial.println("BLE WiFi Command: " + command);
      
      if (command == "ENABLE_WIFI") {
        wifiRequested = true;
        Serial.println("📡 WiFi mode requested via BLE");
        
        if (pStatusCharacteristic) {
          pStatusCharacteristic->setValue("WiFi:Connecting...");
          pStatusCharacteristic->notify();
        }
        
        // Will be handled in loop()
      }
    }
  }
};

// Initialize BLE Server
bool initBLE() {
  Serial.println("Initializing BLE...");
  
  BLEDevice::init("ESP32-CAM-BLE");
  
  // Create BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  
  // Create BLE Service
  BLEService *pService = pServer->createService(BLE_SERVICE_UUID);
  
  // Control Characteristic (Write)
  pControlCharacteristic = pService->createCharacteristic(
    BLE_CONTROL_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pControlCharacteristic->setCallbacks(new ControlCallbacks());
  
  // Status Characteristic (Read + Notify)
  pStatusCharacteristic = pService->createCharacteristic(
    BLE_STATUS_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pStatusCharacteristic->addDescriptor(new BLE2902());
  pStatusCharacteristic->setValue("Ready");
  
  // WiFi Control Characteristic (Write)
  pWiFiCharacteristic = pService->createCharacteristic(
    BLE_WIFI_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pWiFiCharacteristic->setCallbacks(new WiFiCallbacks());
  
  // Start the service
  pService->start();
  
  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  
  Serial.println("✓ BLE Server started");
  Serial.println("  Device Name: ESP32-CAM-BLE");
  Serial.println("  Commands: START, STOP, STATUS");
  Serial.println("  WiFi: Send ENABLE_WIFI to WiFi characteristic");
  
  return true;
}

// Initialize PDM microphone (using ESP_I2S 3.0 library)
bool initMicrophone() {
  // Setup PDM pins: clock=42, data=41
  I2S.setPinsPdmRx(PDM_CLK_PIN, PDM_DATA_PIN);
  
  // Start I2S in PDM RX mode at 16 kHz with 16-bit samples, mono
  if (!I2S.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("Failed to initialize I2S!");
    return false;
  }

  Serial.println("✓ Microphone initialized (16kHz 16-bit PDM)");
  return true;
}

// Initialize SD Card (using SPI mode per Seeed example)
bool initSDCard() {
  Serial.println("Initializing SD Card...");
  
  // Initialize SD card in SPI mode with CS pin 21
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
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
  
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  uint64_t usedSpace = SD.usedBytes() / (1024 * 1024);
  uint64_t totalSpace = SD.totalBytes() / (1024 * 1024);
  uint64_t freeSpace = totalSpace - usedSpace;
  
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
  Serial.printf("Total Space: %lluMB\n", totalSpace);
  Serial.printf("Used Space: %lluMB\n", usedSpace);
  Serial.printf("Free Space: %lluMB\n", freeSpace);
  
  // Create directories for recording
  if (!SD.exists("/video")) {
    if (SD.mkdir("/video")) {
      Serial.println("Created /video directory");
    } else {
      Serial.println("Failed to create /video directory");
    }
  }
  if (!SD.exists("/audio")) {
    if (SD.mkdir("/audio")) {
      Serial.println("Created /audio directory");
    } else {
      Serial.println("Failed to create /audio directory");
    }
  }
  
  Serial.println("✓ SD Card initialized");
  
  // Create SD card mutex for thread-safe access
  sdMutex = xSemaphoreCreateMutex();
  if (sdMutex == NULL) {
    Serial.println("⚠️  Failed to create SD mutex");
  } else {
    Serial.println("✓ SD mutex created");
  }
  
  return true;
}

// Save JPEG frame to SD card with timestamp
bool saveFrameToSD(camera_fb_t *fb) {
  if (!fb) {
    Serial.println("❌ saveFrameToSD: No frame buffer provided");
    return false;
  }
  
  // Acquire SD mutex
  if (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    // Use timestamp in filename if available
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
    
    // Release SD mutex
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

// Generate WAV file header (per Seeed example)
void generate_wav_header(uint8_t *wav_header, uint32_t wav_size, uint32_t sample_rate) {
  // See this for reference: http://soundfile.sapp.org/doc/WaveFormat/
  uint32_t file_size = wav_size + WAV_HEADER_SIZE - 8;
  uint32_t byte_rate = sample_rate * SAMPLE_BITS / 8;
  const uint8_t set_wav_header[] = {
    'R', 'I', 'F', 'F', // ChunkID
    file_size, file_size >> 8, file_size >> 16, file_size >> 24, // ChunkSize
    'W', 'A', 'V', 'E', // Format
    'f', 'm', 't', ' ', // Subchunk1ID
    0x10, 0x00, 0x00, 0x00, // Subchunk1Size (16 for PCM)
    0x01, 0x00, // AudioFormat (1 for PCM)
    0x01, 0x00, // NumChannels (1 channel)
    sample_rate, sample_rate >> 8, sample_rate >> 16, sample_rate >> 24, // SampleRate
    byte_rate, byte_rate >> 8, byte_rate >> 16, byte_rate >> 24, // ByteRate
    0x02, 0x00, // BlockAlign
    0x10, 0x00, // BitsPerSample (16 bits)
    'd', 'a', 't', 'a', // Subchunk2ID
    wav_size, wav_size >> 8, wav_size >> 16, wav_size >> 24, // Subchunk2Size
  };
  memcpy(wav_header, set_wav_header, sizeof(set_wav_header));
}

// Record WAV file (based on Seeed example)
void record_wav() {
  uint32_t sample_size = 0;
  uint32_t record_size = (SAMPLE_RATE * SAMPLE_BITS / 8) * RECORD_TIME;
  uint8_t *rec_buffer = NULL;
  
  Serial.printf("Ready to start recording %d seconds...\n", RECORD_TIME);
  
  // PSRAM malloc for recording
  rec_buffer = (uint8_t *)ps_malloc(record_size);
  if (rec_buffer == NULL) {
    Serial.printf("malloc failed!\n");
    return;
  }
  Serial.printf("Buffer: %d bytes\n", ESP.getPsramSize() - ESP.getFreePsram());
  
  // Start recording - read from I2S (do this BEFORE acquiring SD mutex to avoid blocking)
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
  
  // Increase volume
  for (uint32_t i = 0; i < sample_size; i += SAMPLE_BITS/8) {
    (*(uint16_t *)(rec_buffer+i)) <<= VOLUME_GAIN;
  }
  
  // Now acquire SD mutex for writing
  if (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
    // Use timestamp in filename if available
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
    
    // Write the header to the WAV file
    uint8_t wav_header[WAV_HEADER_SIZE];
    generate_wav_header(wav_header, record_size, SAMPLE_RATE);
    file.write(wav_header, WAV_HEADER_SIZE);
    
    // Write data to the WAV file
    Serial.printf("Writing to the file ...\n");
    size_t written = file.write(rec_buffer, record_size);
    file.close();
    
    // Release SD mutex
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

// Recording task for SD card mode with continuous recording
void recordingTask(void *parameter) {
  // Check if USB MSC is active
  if (usbMscEnabled) {
    Serial.println("❌ Cannot start recording - USB Mass Storage is active");
    Serial.println("   Send DISABLE_USB command first");
    recordingMode = false;
    bleRecordingActive = false;
    vTaskDelete(NULL);
    return;
  }
  
  Serial.println("Starting 10-second interval recording...");
  Serial.println("========================================");
  
  // Display recording mode
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
  const unsigned long clipInterval = 10000; // Record clips every 10 seconds
  
  currentState = STATE_RECORDING;
  
  while (recordingMode && !usbMscEnabled) {
    unsigned long currentTime = millis();
    
    // Check battery status periodically
    static unsigned long lastBatteryCheck = 0;
    if (currentTime - lastBatteryCheck > 60000) {
      checkBatteryStatus();
      lastBatteryCheck = currentTime;
      if (batteryLow) {
        Serial.println("Battery low - stopping recording");
        break;
      }
    }
    
    // Run cleanup periodically
    if (currentTime - lastCleanupTime > CLEANUP_INTERVAL) {
      cleanupOldFiles();
      lastCleanupTime = currentTime;
    }
    
    // VIDEO RECORDING (only if not audio-only mode) - 10-second clips
    if (!audioOnlyMode && (currentTime - lastVideoTime >= clipInterval)) {
      Serial.println("📹 Recording 10-second video clip...");
      unsigned long clipStart = millis();
      unsigned long clipFrameCount = 0;
      
      // Capture frames for 10 seconds
      while (millis() - clipStart < 10000 && recordingMode) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
          Serial.println("❌ Error getting framebuffer!");
          vTaskDelay(pdMS_TO_TICKS(100)); // Wait a bit before retry
        } else {
          bool saved = saveFrameToSD(fb);
          esp_camera_fb_return(fb);
          
          if (saved) {
            clipFrameCount++;
          } else {
            Serial.println("⚠️  Frame save failed, continuing...");
          }
        }
        vTaskDelay(1); // Minimal delay for watchdog
      }
      
      Serial.printf("✓ Video clip complete: %lu frames captured\n", clipFrameCount);
      lastActivityTime = currentTime;
      lastVideoTime = currentTime;
    }
    
    // AUDIO RECORDING (only if not video-only mode)
    if (!videoOnlyMode && (currentTime - lastAudioTime >= clipInterval)) {
      Serial.println("🎙️  Recording audio clip...");
      record_wav();
      lastActivityTime = currentTime;
      lastAudioTime = currentTime;
    }
    
    // Sleep between clips
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

// // ============================================
// // DISPLAY FUNCTIONS (DISABLED - defective display)
// // ============================================
// bool initDisplay() {
//   Wire.begin(SDA_PIN, SCL_PIN);
//   if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
//     Serial.println("SSD1306 allocation failed");
//     return false;
//   }
//   display.clearDisplay();
//   display.setTextSize(1);
//   display.setTextColor(SSD1306_WHITE);
//   display.setCursor(0, 0);
//   display.println("ESP32-CAM");
//   display.println("Booting...");
//   display.display();
//   Serial.println("Display initialized successfully");
//   return true;
// }
// 
// void updateDisplay(const char* line1, const char* line2 = "", const char* line3 = "", const char* line4 = "") {
//   display.clearDisplay();
//   display.setTextSize(1);
//   display.setTextColor(SSD1306_WHITE);
//   display.setCursor(0, 0);
//   display.println(line1);
//   if (strlen(line2) > 0) {
//     display.setCursor(0, 16);
//     display.println(line2);
//   }
//   if (strlen(line3) > 0) {
//     display.setCursor(0, 32);
//     display.println(line3);
//   }
//   if (strlen(line4) > 0) {
//     display.setCursor(0, 48);
//     display.println(line4);
//   }
//   display.display();
// }
// 
// void displayWiFiInfo(const char* ssid, IPAddress ip) {
//   char ipStr[16];
//   sprintf(ipStr, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
//   updateDisplay("WiFi Connected", ssid, ipStr, "Ready to stream!");
// }

// WebSocket event handler
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected\n", client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
  }
}

// Audio streaming task
void audioTask(void *parameter) {
  const size_t bufferSize = 256; // 256 samples = 512 bytes
  uint8_t audioBuffer[bufferSize * 2];
  
  while (true) {
    // Read audio data from I2S/PDM microphone
    size_t bytesRead = 0;
    for (size_t i = 0; i < bufferSize && bytesRead < bufferSize * 2; i++) {
      int sample = I2S.read();
      if (sample != -1 && sample != 0 && sample != 1) {
        audioBuffer[bytesRead++] = sample & 0xFF;
        audioBuffer[bytesRead++] = (sample >> 8) & 0xFF;
      }
    }
    
    if (bytesRead > 0 && ws.count() > 0) {
      // Send audio data to all connected WebSocket clients
      ws.binaryAll(audioBuffer, bytesRead);
    }
    
    vTaskDelay(1); // Small delay to prevent watchdog
  }
}

// MJPEG streaming handler
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
        // Write boundary and headers
        len = snprintf((char *)buffer, maxLen,
          "--frame\r\n"
          "Content-Type: image/jpeg\r\n"
          "Content-Length: %u\r\n\r\n",
          fb->len);
      }
      
      // Copy JPEG data
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

// Web interface with video and audio
const char* html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32-CAM Stream</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body {
      font-family: Arial, sans-serif;
      text-align: center;
      background-color: #1a1a1a;
      color: white;
      margin: 0;
      padding: 20px;
    }
    h1 {
      margin-bottom: 20px;
    }
    img {
      max-width: 100%;
      height: auto;
      border: 2px solid #444;
      border-radius: 8px;
    }
    .controls {
      margin: 20px 0;
    }
    button {
      padding: 10px 20px;
      font-size: 16px;
      background-color: #4CAF50;
      color: white;
      border: none;
      border-radius: 5px;
      cursor: pointer;
      margin: 0 5px;
    }
    button:hover {
      background-color: #45a049;
    }
    button:disabled {
      background-color: #666;
      cursor: not-allowed;
    }
    .status {
      display: inline-block;
      width: 10px;
      height: 10px;
      border-radius: 50%;
      background-color: #666;
      margin-left: 10px;
    }
    .status.active {
      background-color: #4CAF50;
    }
    .info {
      margin-top: 20px;
      font-size: 14px;
      color: #aaa;
    }
  </style>
</head>
<body>
  <h1>📹 ESP32-CAM Video & Audio Stream</h1>
  <img src="/stream" alt="Video Stream">
  
  <div class="controls">
    <button id="audioBtn" onclick="toggleAudio()">🔊 Enable Audio</button>
    <span>Audio: <span class="status" id="audioStatus"></span></span>
  </div>
  
  <div class="info">
    <p>XIAO ESP32S3 Sense - MJPEG Video + PDM Audio</p>
    <p>Resolution: 800x600 (SVGA) | Audio: 16kHz 16-bit PCM</p>
  </div>

  <script>
    let audioContext;
    let websocket;
    let audioQueue = [];
    let isPlaying = false;

    function toggleAudio() {
      const btn = document.getElementById('audioBtn');
      const status = document.getElementById('audioStatus');
      
      if (!audioContext) {
        startAudio();
        btn.textContent = '🔇 Disable Audio';
        status.classList.add('active');
      } else {
        stopAudio();
        btn.textContent = '🔊 Enable Audio';
        status.classList.remove('active');
      }
    }

    function startAudio() {
      audioContext = new (window.AudioContext || window.webkitAudioContext)({
        sampleRate: 16000
      });
      
      websocket = new WebSocket('ws://' + location.hostname + '/audio');
      websocket.binaryType = 'arraybuffer';
      
      websocket.onopen = () => {
        console.log('Audio WebSocket connected');
      };
      
      websocket.onmessage = (event) => {
        const audioData = new Int16Array(event.data);
        playAudio(audioData);
      };
      
      websocket.onerror = (error) => {
        console.error('WebSocket error:', error);
      };
      
      websocket.onclose = () => {
        console.log('Audio WebSocket disconnected');
      };
    }

    function stopAudio() {
      if (websocket) {
        websocket.close();
        websocket = null;
      }
      if (audioContext) {
        audioContext.close();
        audioContext = null;
      }
      audioQueue = [];
      isPlaying = false;
    }

    function playAudio(int16Data) {
      // Convert Int16 to Float32 for Web Audio API
      const float32Data = new Float32Array(int16Data.length);
      for (let i = 0; i < int16Data.length; i++) {
        float32Data[i] = int16Data[i] / 32768.0;
      }
      
      const audioBuffer = audioContext.createBuffer(1, float32Data.length, 16000);
      audioBuffer.getChannelData(0).set(float32Data);
      
      const source = audioContext.createBufferSource();
      source.buffer = audioBuffer;
      source.connect(audioContext.destination);
      source.start(audioContext.currentTime);
    }
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n========================================");
  Serial.println("XIAO ESP32S3 Camera & Audio Streamer v3.0");
  Serial.println("Features: BLE Control | Continuous Recording | Timestamps");
  Serial.println("          File Cleanup | Power Management | OTA Updates");
  Serial.println("========================================\n");
  
  // Check PSRAM availability first
  Serial.println("Checking PSRAM...");
  if (psramFound()) {
    Serial.printf("✓ PSRAM found: %d bytes total, %d bytes free\n", 
                  ESP.getPsramSize(), ESP.getFreePsram());
  } else {
    Serial.println("❌ PSRAM not found!");
    Serial.println("Camera initialization will fail without PSRAM!");
    Serial.println("Check platformio.ini board_build.arduino.memory_type setting");
    while (1) { delay(1000); }
  }
  
  // Initialize status LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  
  // Initialize activity tracking
  lastActivityTime = millis();
  
  // Initialize camera
  Serial.println("\nInitializing camera...");
  if (!initCamera()) {
    Serial.println("❌ Camera initialization failed!");
    currentState = STATE_ERROR;
    Serial.println("System halted.");
    while (1) { 
      updateStatusLED();
      delay(100);
    }
  }
  Serial.println("✓ Camera initialized");
  
  // Initialize microphone
  Serial.println("\nInitializing microphone...");
  if (!initMicrophone()) {
    Serial.println("⚠️  Microphone initialization failed!");
    Serial.println("Continuing without audio...");
  } else {
    Serial.println("✓ Microphone initialized");
  }
  
  // Initialize SD card (needed for both BLE and WiFi modes)
  Serial.println("\nInitializing SD Card...");
  if (!initSDCard()) {
    Serial.println("❌ SD Card initialization failed!");
    Serial.println("System halted - SD card required for recording.");
    while (1) { 
      updateStatusLED();
      delay(1000);
    }
  }
  
  // Initialize BLE first (primary control method)
  Serial.println("\n========================================");
  Serial.println("Starting in BLE CONTROL MODE");
  Serial.println("========================================");
  
  if (!initBLE()) {
    Serial.println("⚠️  BLE initialization failed!");
  }
  
  Serial.println("\n📱 BLE Control Ready!");
  Serial.println("========================================");
  Serial.println("Connect via BLE app (nRF Connect, LightBlue, etc.)");
  Serial.println("Device: ESP32-CAM-BLE");
  Serial.println("\nCommands (write to Control characteristic):");
  Serial.println("  START       - Start recording to SD card");
  Serial.println("  STOP        - Stop recording");
  Serial.println("  STATUS      - Get recording status");
  Serial.println("  LIST_VIDEO  - List all video files");
  Serial.println("  LIST_AUDIO  - List all audio files");
  Serial.println("  LIST_ALL    - List all recorded files");
  Serial.println("\nRecording Modes:");
  Serial.println("  AUDIO_ONLY  - Record only audio (no video)");
  Serial.println("  VIDEO_ONLY  - Record only video (continuous)");
  Serial.println("  BOTH        - Record both audio + video (default)");
  Serial.println("\nUSB Mass Storage:");
  Serial.println("  ENABLE_USB  - Enable USB drive mode (access SD card)");
  Serial.println("  DISABLE_USB - Disable USB drive mode");
  Serial.println("\nWiFi Mode (write to WiFi characteristic):");
  Serial.println("  ENABLE_WIFI - Switch to WiFi streaming mode");
  Serial.println("========================================\n");
  
  currentState = STATE_INIT;
}

void startWiFiMode() {
  Serial.println("\n========================================");
  Serial.println("Switching to WiFi Mode...");
  Serial.println("========================================");
  
  // Disable BLE to free resources
  if (bleEnabled) {
    BLEDevice::deinit(true);
    bleEnabled = false;
    Serial.println("✓ BLE disabled");
  }
  
  // Connect to WiFi
  Serial.println("Connecting to WiFi...");
  Serial.printf("SSID: %s\n", wifi_ssid);
  
  currentState = STATE_WIFI_CONNECTING;
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    updateStatusLED();
    attempts++;
  }
  Serial.println();
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n❌ WiFi connection failed!");
    Serial.println("Returning to BLE mode...");
    
    // Restart BLE
    initBLE();
    bleEnabled = true;
    return;
  } else {
    // WiFi connected - start streaming mode
    currentState = STATE_WIFI_CONNECTED;
    IPAddress IP = WiFi.localIP();
    Serial.println("\n✓ WiFi Connected!");
    Serial.println("========================================");
    Serial.printf("IP Address: http://%s\n", IP.toString().c_str());
    Serial.printf("Stream URL: http://%s/stream\n", IP.toString().c_str());
    Serial.printf("OTA Updates: http://%s:3232\n", IP.toString().c_str());
    Serial.println("========================================\n");
    
    // Initialize NTP time
    initTime();
    
    // Initialize OTA updates
    initOTA();
    
    // Setup WebSocket for audio
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    
    // Setup web server routes
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      request->send(200, "text/html", html);
    });
    
    server.on("/stream", HTTP_GET, handleStream);
    
    // Add status endpoint
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
      String json = "{";
      json += "\"uptime\":" + String(millis() / 1000) + ",";
      json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
      json += "\"sdFree\":" + String((SD.totalBytes() - SD.usedBytes()) / (1024 * 1024)) + ",";
      json += "\"sdTotal\":" + String(SD.totalBytes() / (1024 * 1024)) + ",";
      json += "\"frames\":" + String(frameCount) + ",";
      json += "\"audioFiles\":" + String(audioFileCount) + ",";
      json += "\"batteryVoltage\":" + String(getBatteryVoltage(), 2) + ",";
      json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
      json += "\"timestamp\":\"" + getTimestamp() + "\"";
      json += "}";
      request->send(200, "application/json", json);
    });
    
    // File browser API endpoints - list files in directory
    server.on("/api/files/list", HTTP_GET, [](AsyncWebServerRequest *request) {
      String path = "/";
      if (request->hasParam("path")) {
        path = request->getParam("path")->value();
      }
      
      if (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        File dir = SD.open(path);
        if (!dir || !dir.isDirectory()) {
          xSemaphoreGive(sdMutex);
          request->send(404, "application/json", "{\"error\":\"Directory not found\"}");
          return;
        }
        
        String json = "{\"path\":\"" + path + "\",\"files\":[";
        bool first = true;
        File file = dir.openNextFile();
        while (file) {
          if (!first) json += ",";
          first = false;
          json += "{\"name\":\"" + String(file.name()) + "\",";
          json += "\"size\":" + String(file.size()) + ",";
          json += "\"isDir\":" + String(file.isDirectory() ? "true" : "false") + "}";
          file = dir.openNextFile();
        }
        json += "]}";
        dir.close();
        xSemaphoreGive(sdMutex);
        
        request->send(200, "application/json", json);
      } else {
        request->send(503, "application/json", "{\"error\":\"SD card busy\"}");
      }
    });
    
    // Download file endpoint
    server.on("/api/files/download", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (!request->hasParam("path")) {
        request->send(400, "application/json", "{\"error\":\"Missing path parameter\"}");
        return;
      }
      
      String filePath = request->getParam("path")->value();
      
      if (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        File file = SD.open(filePath);
        if (!file || file.isDirectory()) {
          xSemaphoreGive(sdMutex);
          request->send(404, "application/json", "{\"error\":\"File not found\"}");
          return;
        }
        
        // Stream file to client
        AsyncWebServerResponse *response = request->beginResponse(
          "application/octet-stream",
          file.size(),
          [file](uint8_t *buffer, size_t maxLen, size_t index) mutable -> size_t {
            return file.read(buffer, maxLen);
          }
        );
        
        // Extract filename for download
        String filename = filePath;
        int lastSlash = filename.lastIndexOf('/');
        if (lastSlash >= 0) {
          filename = filename.substring(lastSlash + 1);
        }
        response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
        
        request->send(response);
        file.close();
        xSemaphoreGive(sdMutex);
      } else {
        request->send(503, "application/json", "{\"error\":\"SD card busy\"}");
      }
    });
    
    // Delete file endpoint
    server.on("/api/files/delete", HTTP_DELETE, [](AsyncWebServerRequest *request) {
      if (!request->hasParam("path")) {
        request->send(400, "application/json", "{\"error\":\"Missing path parameter\"}");
        return;
      }
      
      String filePath = request->getParam("path")->value();
      
      if (sdMutex && xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (SD.remove(filePath)) {
          xSemaphoreGive(sdMutex);
          request->send(200, "application/json", "{\"success\":true}");
        } else {
          xSemaphoreGive(sdMutex);
          request->send(500, "application/json", "{\"error\":\"Failed to delete file\"}");
        }
      } else {
        request->send(503, "application/json", "{\"error\":\"SD card busy\"}");
      }
    });
    
    // File browser web UI
    server.on("/files", HTTP_GET, [](AsyncWebServerRequest *request) {
      const char* fileBrowserHtml = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32-CAM File Browser</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body {
      font-family: Arial, sans-serif;
      background-color: #1a1a1a;
      color: white;
      margin: 0;
      padding: 20px;
    }
    h1 { margin-bottom: 10px; }
    .path { color: #888; font-size: 14px; margin-bottom: 20px; }
    .file-list {
      background-color: #2a2a2a;
      border-radius: 8px;
      padding: 10px;
    }
    .file-item {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 10px;
      border-bottom: 1px solid #444;
      cursor: pointer;
    }
    .file-item:hover { background-color: #333; }
    .file-name { flex-grow: 1; }
    .file-size { color: #888; margin-right: 10px; font-size: 14px; }
    .folder { color: #4CAF50; }
    .file { color: #2196F3; }
    button {
      padding: 8px 16px;
      margin: 5px;
      background-color: #4CAF50;
      color: white;
      border: none;
      border-radius: 4px;
      cursor: pointer;
    }
    button.delete { background-color: #f44336; }
    button:hover { opacity: 0.8; }
    .loading { text-align: center; padding: 20px; color: #888; }
    .error { color: #f44336; padding: 20px; }
    .actions { margin-top: 10px; }
  </style>
</head>
<body>
  <h1>📁 File Browser</h1>
  <div class="path" id="currentPath">/</div>
  <button onclick="location.href='/'">🏠 Home</button>
  <button onclick="refreshFiles()">🔄 Refresh</button>
  <div class="file-list" id="fileList">
    <div class="loading">Loading...</div>
  </div>
  
  <script>
    let currentPath = '/';
    
    async function loadFiles(path = '/') {
      currentPath = path;
      document.getElementById('currentPath').textContent = 'Path: ' + path;
      document.getElementById('fileList').innerHTML = '<div class="loading">Loading...</div>';
      
      try {
        const response = await fetch('/api/files/list?path=' + encodeURIComponent(path));
        const data = await response.json();
        
        if (data.error) {
          document.getElementById('fileList').innerHTML = '<div class="error">Error: ' + data.error + '</div>';
          return;
        }
        
        let html = '';
        
        // Add parent directory link if not at root
        if (path !== '/') {
          const parentPath = path.substring(0, path.lastIndexOf('/')) || '/';
          html += '<div class="file-item folder" onclick="loadFiles(\'' + parentPath + '\')">';
          html += '<span class="file-name">📁 ..</span>';
          html += '</div>';
        }
        
        // Sort: directories first, then files
        data.files.sort((a, b) => {
          if (a.isDir && !b.isDir) return -1;
          if (!a.isDir && b.isDir) return 1;
          return a.name.localeCompare(b.name);
        });
        
        data.files.forEach(file => {
          const fullPath = path === '/' ? '/' + file.name : path + '/' + file.name;
          const icon = file.isDir ? '📁' : '📄';
          const cssClass = file.isDir ? 'folder' : 'file';
          const sizeText = file.isDir ? '' : formatSize(file.size);
          
          html += '<div class="file-item ' + cssClass + '">';
          if (file.isDir) {
            html += '<span class="file-name" onclick="loadFiles(\'' + fullPath + '\')">' + icon + ' ' + file.name + '</span>';
          } else {
            html += '<span class="file-name">' + icon + ' ' + file.name + '</span>';
            html += '<span class="file-size">' + sizeText + '</span>';
            html += '<button onclick="downloadFile(\'' + fullPath + '\')">⬇️ Download</button>';
            html += '<button class="delete" onclick="deleteFile(\'' + fullPath + '\')">🗑️ Delete</button>';
          }
          html += '</div>';
        });
        
        if (data.files.length === 0) {
          html = '<div class="loading">Empty directory</div>';
        }
        
        document.getElementById('fileList').innerHTML = html;
      } catch (error) {
        document.getElementById('fileList').innerHTML = '<div class="error">Error loading files: ' + error.message + '</div>';
      }
    }
    
    function formatSize(bytes) {
      if (bytes < 1024) return bytes + ' B';
      if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
      return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
    }
    
    function downloadFile(path) {
      window.location.href = '/api/files/download?path=' + encodeURIComponent(path);
    }
    
    async function deleteFile(path) {
      if (!confirm('Delete ' + path + '?')) return;
      
      try {
        const response = await fetch('/api/files/delete?path=' + encodeURIComponent(path), {
          method: 'DELETE'
        });
        const data = await response.json();
        
        if (data.success) {
          alert('File deleted successfully');
          refreshFiles();
        } else {
          alert('Error: ' + (data.error || 'Failed to delete'));
        }
      } catch (error) {
        alert('Error: ' + error.message);
      }
    }
    
    function refreshFiles() {
      loadFiles(currentPath);
    }
    
    // Load root directory on page load
    loadFiles('/');
  </script>
</body>
</html>
)rawliteral";
      request->send(200, "text/html", fileBrowserHtml);
    });
    
    // Start server
    server.begin();
    Serial.println("\n✓ Web server started");
    
    currentState = STATE_STREAMING;
    
    // Start audio streaming task on Core 0 (Core 1 handles WiFi/camera)
    xTaskCreatePinnedToCore(
      audioTask,       // Task function
      "AudioStream",   // Task name
      4096,            // Stack size
      NULL,            // Parameters
      1,               // Priority
      NULL,            // Task handle
      0                // Core 0
    );
    
    Serial.println("✓ Audio streaming task started");
    Serial.println("\nReady to stream video & audio!");
    Serial.println("Click 'Enable Audio' button in browser to start audio");
    Serial.println("\n📊 Status API: http://" + IP.toString() + "/api/status");
    Serial.println("📁 File Browser: http://" + IP.toString() + "/files");
  }
}

void loop() {
  // Update status LED
  updateStatusLED();
  
  // Handle file listing requests (process in main loop to avoid stack overflow in BLE task)
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
  
  // Handle WiFi mode request from BLE
  if (wifiRequested && bleEnabled) {
    wifiRequested = false;
    startWiFiMode();
  }
  
  // Handle OTA updates (only in WiFi mode)
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }
  
  // Update BLE status notifications
  if (bleEnabled && deviceConnected && pStatusCharacteristic) {
    static unsigned long lastBLEUpdate = 0;
    if (millis() - lastBLEUpdate > 5000) {  // Every 5 seconds
      String status = "Frames:" + String(frameCount) + 
                     "|Audio:" + String(audioFileCount) + 
                     "|Recording:" + String(bleRecordingActive ? "ON" : "OFF");
      pStatusCharacteristic->setValue(status.c_str());
      pStatusCharacteristic->notify();
      lastBLEUpdate = millis();
    }
  }
  
  // Check battery status
  static unsigned long lastBatteryCheck = 0;
  if (millis() - lastBatteryCheck > 60000) {  // Check every minute
    checkBatteryStatus();
    lastBatteryCheck = millis();
  }
  
  // Stop recording if USB MSC becomes active
  if (usbMscEnabled && recordingMode) {
    Serial.println("⚠️  USB MSC enabled - stopping recording");
    bleRecordingActive = false;
    recordingMode = false;
  }
  
  // Check idle timeout for power saving
  checkIdleTimeout();
  
  // In recording mode, show status periodically
  if (recordingMode) {
    static unsigned long lastStatus = 0;
    if (millis() - lastStatus > 30000) {  // Every 30 seconds
      uint64_t totalSpace = SD.totalBytes() / (1024 * 1024);
      uint64_t usedSpace = SD.usedBytes() / (1024 * 1024);
      uint64_t freeSpace = totalSpace - usedSpace;
      
      Serial.println("========================================");
      Serial.printf("Recording Status (%s):\n", bleEnabled ? "BLE" : "WiFi");
      Serial.printf("  Video frames: %lu\n", frameCount);
      Serial.printf("  Audio files: %lu\n", audioFileCount);
      Serial.printf("  SD Free: %lluMB / %lluMB\n", freeSpace, totalSpace);
      Serial.printf("  Battery: %.2fV\n", getBatteryVoltage());
      Serial.printf("  Uptime: %lu seconds\n", millis() / 1000);
      if (bleEnabled && deviceConnected) {
        Serial.printf("  BLE: Connected\n");
      }
      if (timeInitialized) {
        Serial.printf("  Time: %s\n", getTimestamp().c_str());
      }
      Serial.println("========================================");
      lastStatus = millis();
    }
  }
  
  delay(100);
}