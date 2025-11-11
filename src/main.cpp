#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
// #include <Wire.h>
// #include <Adafruit_GFX.h>
// #include <Adafruit_SSD1306.h>
#include "esp_camera.h"
#include "driver/i2s.h"

// ============================================
// WiFi Credentials - CHANGE THESE!
// ============================================
const char* wifi_ssid = "MastaWifi";      // Change to your WiFi name
const char* wifi_password = "mastashake08";  // Change to your WiFi password

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

// Audio configuration for PDM microphone
#define I2S_PORT I2S_NUM_0
#define PDM_DATA_PIN 42
#define PDM_CLK_PIN 41
#define SAMPLE_RATE 16000
#define BUFFER_SIZE 512

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

// Initialize PDM microphone
bool initMicrophone() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_PIN_NO_CHANGE,
    .ws_io_num = PDM_CLK_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = PDM_DATA_PIN
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("I2S driver install failed: %d\n", err);
    return false;
  }

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("I2S pin config failed: %d\n", err);
    return false;
  }

  Serial.println("Microphone initialized successfully");
  return true;
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
  int16_t audioBuffer[BUFFER_SIZE];
  size_t bytesRead = 0;
  
  while (true) {
    // Read audio data from I2S/PDM microphone
    esp_err_t result = i2s_read(I2S_PORT, audioBuffer, sizeof(audioBuffer), &bytesRead, portMAX_DELAY);
    
    if (result == ESP_OK && bytesRead > 0 && ws.count() > 0) {
      // Send audio data to all connected WebSocket clients
      ws.binaryAll((uint8_t*)audioBuffer, bytesRead);
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
  Serial.println("XIAO ESP32S3 Camera & Audio Streamer");
  Serial.println("========================================\n");
  
  // Initialize camera
  Serial.println("Initializing camera...");
  if (!initCamera()) {
    Serial.println("❌ Camera initialization failed!");
    Serial.println("System halted.");
    while (1) { delay(1000); }
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
    Serial.println("\n❌ WiFi connection failed!");
    Serial.println("Please check your WiFi credentials in the code.");
    Serial.println("System halted.");
    while (1) { delay(1000); }
  }
  
  IPAddress IP = WiFi.localIP();
  Serial.println("\n✓ WiFi Connected!");
  Serial.println("========================================");
  Serial.printf("IP Address: http://%s\n", IP.toString().c_str());
  Serial.printf("Stream URL: http://%s/stream\n", IP.toString().c_str());
  Serial.println("========================================\n");
  
  // Setup WebSocket for audio
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  
  // Setup web server routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", html);
  });
  
  server.on("/stream", HTTP_GET, handleStream);
  
  // Start server
  server.begin();
  Serial.println("\n✓ Web server started");
  
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
}

void loop() {
  // Nothing needed here - server runs asynchronously
  delay(1000);
}