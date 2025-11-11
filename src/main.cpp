#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <I2S.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <Preferences.h>
// #include <Wire.h>
// #include <Adafruit_GFX.h>
// #include <Adafruit_SSD1306.h>
#include "esp_camera.h"
#include "esp_sleep.h"

// ============================================
// WiFi Credentials - CHANGE THESE!
// ============================================
const char* wifi_ssid = "MastaWifi";      // Change to your WiFi name
const char* wifi_password = "mastashake08";  // Change to your WiFi password

// ============================================
// Recording Mode Configuration
// ============================================
bool recordingMode = false;  // Set to true when WiFi fails
unsigned long frameCount = 0;
unsigned long audioFileCount = 0;

// ============================================
// Motion Detection Configuration
// ============================================
#define MOTION_THRESHOLD 15      // Pixel difference threshold (0-255)
#define MOTION_MIN_PIXELS 1000   // Minimum pixels changed to trigger motion
bool motionDetected = false;
camera_fb_t *lastFrame = NULL;

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
#define LED_BUILTIN 21            // Built-in LED pin for XIAO ESP32S3
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

// Camera pin configuration for XIAO ESP32S3 Sense
camera_config_t camera_config = {
  .pin_pwdn = -1,
  .pin_reset = -1,
  .pin_xclk = 10,
  .pin_sscb_sda = 40,
  .pin_sscb_scl = 39,
  .pin_d7 = 48,
  .pin_d6 = 11,
  .pin_d5 = 12,
  .pin_d4 = 14,
  .pin_d3 = 16,
  .pin_d2 = 18,
  .pin_d1 = 17,
  .pin_d0 = 15,
  .pin_vsync = 38,
  .pin_href = 47,
  .pin_pclk = 13,
  .xclk_freq_hz = 20000000,
  .ledc_timer = LEDC_TIMER_0,
  .ledc_channel = LEDC_CHANNEL_0,
  .pixel_format = PIXFORMAT_JPEG,
  .frame_size = FRAMESIZE_SVGA,  // 800x600
  .jpeg_quality = 12,  // 0-63, lower = higher quality
  .fb_count = 2,
  .grab_mode = CAMERA_GRAB_LATEST
};

// Initialize camera
bool initCamera() {
  esp_err_t err = esp_camera_init(&camera_config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    currentState = STATE_ERROR;
    return false;
  }
  
  // Adjust camera sensor settings
  sensor_t * s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 0);     // -2 to 2
    s->set_contrast(s, 0);       // -2 to 2
    s->set_saturation(s, 0);     // -2 to 2
    s->set_hmirror(s, 0);        // 0 = disable, 1 = enable
    s->set_vflip(s, 0);          // 0 = disable, 1 = enable
  }
  
  Serial.println("Camera initialized successfully");
  return true;
}

// ============================================
// MOTION DETECTION
// ============================================
bool detectMotion(camera_fb_t *currentFrame) {
  if (!lastFrame || !currentFrame) return false;
  
  // Only compare if frames are same size and grayscale/small enough
  if (lastFrame->len != currentFrame->len) {
    return false;
  }
  
  uint32_t changedPixels = 0;
  uint32_t totalPixels = currentFrame->width * currentFrame->height;
  
  // Sample every 4th pixel for performance
  for (uint32_t i = 0; i < currentFrame->len; i += 4) {
    int diff = abs(currentFrame->buf[i] - lastFrame->buf[i]);
    if (diff > MOTION_THRESHOLD) {
      changedPixels++;
    }
  }
  
  // Check if enough pixels changed
  bool motion = changedPixels > (MOTION_MIN_PIXELS / 4);
  
  if (motion) {
    Serial.printf("Motion detected! %u pixels changed\n", changedPixels * 4);
  }
  
  return motion;
}

void updateLastFrame(camera_fb_t *frame) {
  if (lastFrame) {
    free(lastFrame->buf);
    free(lastFrame);
  }
  
  // Allocate and copy frame for motion detection
  lastFrame = (camera_fb_t*)malloc(sizeof(camera_fb_t));
  if (lastFrame) {
    memcpy(lastFrame, frame, sizeof(camera_fb_t));
    lastFrame->buf = (uint8_t*)malloc(frame->len);
    if (lastFrame->buf) {
      memcpy(lastFrame->buf, frame->buf, frame->len);
    }
  }
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

// Initialize PDM microphone (using I2S library per Seeed example)
bool initMicrophone() {
  // Configure pins: bck=-1, ws=42 (CLK), data_in=41 (DATA), data_out=-1, mck=-1
  I2S.setAllPins(-1, PDM_CLK_PIN, PDM_DATA_PIN, -1, -1);
  
  // Begin in PDM mono mode with 16kHz sample rate and 16-bit samples
  if (!I2S.begin(PDM_MONO_MODE, SAMPLE_RATE, SAMPLE_BITS)) {
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
  return true;
}

// Save JPEG frame to SD card with timestamp
bool saveFrameToSD(camera_fb_t *fb) {
  if (!fb) return false;
  
  // Use timestamp in filename if available
  char filename[64];
  if (timeInitialized) {
    String timestamp = getTimestamp();
    sprintf(filename, "/video/%s_frame_%06lu.jpg", timestamp.c_str(), frameCount++);
  } else {
    sprintf(filename, "/video/frame_%06lu.jpg", frameCount++);
  }
  
  File file = SD.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    currentState = STATE_ERROR;
    return false;
  }
  
  size_t written = file.write(fb->buf, fb->len);
  file.close();
  
  if (written != fb->len) {
    Serial.printf("Write error: wrote %d of %d bytes\n", written, fb->len);
    return false;
  }
  
  if (frameCount % 30 == 0) {  // Log every 30 frames
    Serial.printf("Saved frame %lu (%d bytes)\n", frameCount, fb->len);
  }
  
  return true;
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
    return;
  }
  
  // Write the header to the WAV file
  uint8_t wav_header[WAV_HEADER_SIZE];
  generate_wav_header(wav_header, record_size, SAMPLE_RATE);
  file.write(wav_header, WAV_HEADER_SIZE);
  
  // PSRAM malloc for recording
  rec_buffer = (uint8_t *)ps_malloc(record_size);
  if (rec_buffer == NULL) {
    Serial.printf("malloc failed!\n");
    file.close();
    return;
  }
  Serial.printf("Buffer: %d bytes\n", ESP.getPsramSize() - ESP.getFreePsram());
  
  // Start recording - read from I2S
  sample_size = I2S.read(rec_buffer, record_size);
  
  if (sample_size == 0) {
    Serial.printf("Record Failed!\n");
  } else {
    Serial.printf("Record %d bytes\n", sample_size);
  }
  
  // Increase volume
  for (uint32_t i = 0; i < sample_size; i += SAMPLE_BITS/8) {
    (*(uint16_t *)(rec_buffer+i)) <<= VOLUME_GAIN;
  }
  
  // Write data to the WAV file
  Serial.printf("Writing to the file ...\n");
  if (file.write(rec_buffer, record_size) != record_size)
    Serial.printf("Write file Failed!\n");
  
  free(rec_buffer);
  file.close();
  Serial.printf("Recording saved: %s\n", filename);
}

// Recording task for SD card mode with motion detection and auto-recording
void recordingTask(void *parameter) {
  Serial.println("Starting continuous SD card recording...");
  Serial.println("Recording audio files automatically every 10 seconds");
  
  unsigned long lastFrameTime = 0;
  unsigned long lastMotionCheck = 0;
  const unsigned long frameInterval = 100; // Capture frame every 100ms (10 FPS)
  const unsigned long motionCheckInterval = 500; // Check motion every 500ms
  bool isRecording = false;
  unsigned long recordingStartTime = 0;
  const unsigned long maxRecordingTime = 60000; // Stop recording after 60s of no motion
  
  currentState = STATE_RECORDING;
  
  while (recordingMode) {
    // Check battery status periodically
    checkBatteryStatus();
    if (batteryLow) {
      Serial.println("Battery low - stopping recording");
      break;
    }
    
    // Run cleanup periodically
    if (millis() - lastCleanupTime > CLEANUP_INTERVAL) {
      cleanupOldFiles();
      lastCleanupTime = millis();
    }
    
    // Capture frame for motion detection
    if (millis() - lastMotionCheck >= motionCheckInterval) {
      camera_fb_t *fb = esp_camera_fb_get();
      if (fb) {
        // Detect motion
        if (detectMotion(fb)) {
          motionDetected = true;
          isRecording = true;
          recordingStartTime = millis();
          Serial.println("📹 Motion detected - recording started");
        }
        
        // Update last frame for next comparison
        updateLastFrame(fb);
        
        // Save frame if recording
        if (isRecording) {
          saveFrameToSD(fb);
          lastActivityTime = millis();
        }
        
        esp_camera_fb_return(fb);
        lastMotionCheck = millis();
      }
    }
    
    // Stop recording if no motion for maxRecordingTime
    if (isRecording && (millis() - recordingStartTime > maxRecordingTime)) {
      isRecording = false;
      motionDetected = false;
      Serial.println("⏸️  No motion - recording paused");
    }
    
    // Auto-record audio continuously (10 second clips)
    Serial.println("🎙️  Recording audio clip...");
    record_wav();
    lastActivityTime = millis();
    
    // Small delay between recordings
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  
  Serial.println("Recording stopped");
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
  const size_t bufferSize = 512;
  uint8_t audioBuffer[bufferSize];
  
  while (true) {
    // Read audio data from I2S/PDM microphone
    size_t bytesRead = I2S.read(audioBuffer, bufferSize);
    
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
  Serial.println("XIAO ESP32S3 Camera & Audio Streamer v2.0");
  Serial.println("Features: Motion Detection | Timestamps | File Cleanup");
  Serial.println("          Power Management | OTA Updates | Status LED");
  Serial.println("========================================\n");
  
  // Initialize status LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  
  // Initialize activity tracking
  lastActivityTime = millis();
  
  // Initialize camera
  Serial.println("Initializing camera...");
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
  
  // Connect to WiFi
  Serial.println("\n========================================");
  Serial.println("Connecting to WiFi...");
  Serial.printf("SSID: %s\n", wifi_ssid);
  Serial.println("========================================");
  
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
    Serial.println("========================================");
    Serial.println("Switching to SD CARD RECORDING MODE");
    Serial.println("========================================");
    
    // Initialize SD card
    if (!initSDCard()) {
      Serial.println("❌ SD Card initialization failed!");
      Serial.println("Cannot stream or record. System halted.");
      while (1) { delay(1000); }
    }
    
    // Enable recording mode
    recordingMode = true;
    
    Serial.println("\n✓ Recording to SD Card");
    Serial.println("========================================");
    Serial.println("Video: /video/frame_XXXXXX.jpg");
    Serial.println("Audio: /audio/audio_XXXXXX.wav");
    Serial.println("========================================");
    Serial.println("\nRecording started! Press RESET to stop.");
    
    // Start recording task on Core 0
    xTaskCreatePinnedToCore(
      recordingTask,   // Task function
      "SDRecording",   // Task name
      8192,            // Stack size (larger for SD operations)
      NULL,            // Parameters
      1,               // Priority
      NULL,            // Task handle
      0                // Core 0
    );
    
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
      json += "\"motionDetected\":" + String(motionDetected ? "true" : "false") + ",";
      json += "\"batteryVoltage\":" + String(getBatteryVoltage(), 2) + ",";
      json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
      json += "\"timestamp\":\"" + getTimestamp() + "\"";
      json += "}";
      request->send(200, "application/json", json);
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
  }
}

void loop() {
  // Update status LED
  updateStatusLED();
  
  // Handle OTA updates
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }
  
  // Check battery status
  static unsigned long lastBatteryCheck = 0;
  if (millis() - lastBatteryCheck > 60000) {  // Check every minute
    checkBatteryStatus();
    lastBatteryCheck = millis();
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
      Serial.printf("Recording Status:\n");
      Serial.printf("  Video frames: %lu\n", frameCount);
      Serial.printf("  Audio files: %lu\n", audioFileCount);
      Serial.printf("  Motion detected: %s\n", motionDetected ? "YES" : "NO");
      Serial.printf("  SD Free: %lluMB / %lluMB\n", freeSpace, totalSpace);
      Serial.printf("  Battery: %.2fV\n", getBatteryVoltage());
      Serial.printf("  Uptime: %lu seconds\n", millis() / 1000);
      if (timeInitialized) {
        Serial.printf("  Time: %s\n", getTimestamp().c_str());
      }
      Serial.println("========================================");
      lastStatus = millis();
    }
  }
  
  delay(100);
}