#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "esp_camera.h"

// WiFi Access Point credentials
const char* ap_ssid = "ESP32-CAM";
const char* ap_password = "12345678";  // Min 8 characters

// Web server on port 80
AsyncWebServer server(80);

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

// Simple web interface
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
    .info {
      margin-top: 20px;
      font-size: 14px;
      color: #aaa;
    }
  </style>
</head>
<body>
  <h1>📹 ESP32-CAM Video Stream</h1>
  <img src="/stream" alt="Video Stream">
  <div class="info">
    <p>XIAO ESP32S3 Sense - MJPEG Stream</p>
    <p>Resolution: 800x600 (SVGA)</p>
  </div>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nXIAO ESP32S3 Camera Streamer");
  Serial.println("=============================");
  
  // Initialize camera
  if (!initCamera()) {
    Serial.println("Camera initialization failed!");
    Serial.println("System halted.");
    while (1) { delay(1000); }
  }
  
  // Create WiFi Access Point
  Serial.println("\nStarting WiFi Access Point...");
  WiFi.softAP(ap_ssid, ap_password);
  IPAddress IP = WiFi.softAPIP();
  
  Serial.println("\n✓ WiFi Access Point Active");
  Serial.printf("  SSID: %s\n", ap_ssid);
  Serial.printf("  Password: %s\n", ap_password);
  Serial.printf("  IP Address: http://%s\n", IP.toString().c_str());
  Serial.println("\nConnect to the WiFi and open the IP in your browser");
  
  // Setup web server routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", html);
  });
  
  server.on("/stream", HTTP_GET, handleStream);
  
  // Start server
  server.begin();
  Serial.println("\n✓ Web server started");
  Serial.println("\nReady to stream!");
}

void loop() {
  // Nothing needed here - server runs asynchronously
  delay(1000);
}