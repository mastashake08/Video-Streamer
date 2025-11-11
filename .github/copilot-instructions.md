# Video Streamer - PlatformIO ESP32 Project

## Project Overview
This is an embedded video streaming project targeting the **Seeed XIAO ESP32S3** development board using the Arduino framework. The project is currently in early stages with boilerplate Arduino code - the video streaming functionality is yet to be implemented.

## Hardware Platform
- **Board**: Seeed Studio XIAO ESP32S3 Sense
  - **Processor**: ESP32-S3R8 (Xtensa LX7 dual-core, 32-bit @ up to 240 MHz)
  - **Wireless**: Complete 2.4GHz Wi-Fi subsystem, BLE 5.0/Bluetooth mesh
  - **Memory**: 8MB PSRAM + 8MB Flash
  - **Camera**: OV2640 (1600x1200) or OV3660 (2048x1536) - both compatible with same code
  - **Sensors**: Digital microphone (PDM), detachable camera module
  - **Storage**: MicroSD card slot (supports up to 32GB FAT)
  - **Size**: 21 x 17.8 x 15mm (with expansion board)
  - **Power**: 5V via Type-C, 4.2V battery support with charging (100mA)
- **Framework**: Arduino (ESP32 Arduino Core)
- **Platform**: Espressif32

### Important Notes
- **Camera Model Transition**: OV2640 has been discontinued; newer boards use OV3660, but all example code remains compatible
- **Power Consumption** (Webcam streaming): ~220mA @ 5V (Type-C) or ~212mA @ 4.2V (Battery)
- **RF Performance**: 100m+ range when using U.FL antenna connector

## Development Workflow

### Build & Upload
```bash
# Build the project
pio run

# Upload to connected board
pio run --target upload

# Build and upload in one command
pio run -t upload

# Monitor serial output
pio device monitor

# Upload and monitor
pio run -t upload && pio device monitor
```

### BLE Configuration Workflow
On first boot or to change WiFi settings:
1. Device starts in **BLE Configuration Mode** for 60 seconds
2. BLE device name: **ESP32-CAM-Config**
3. Connect via BLE app (nRF Connect, LightBlue, etc.)
4. Write to characteristics:
   - **SSID Characteristic**: Set desired WiFi hotspot name
   - **Password Characteristic**: Set password (min 8 characters)
5. Settings are saved to NVS (persistent across reboots)
6. Device automatically exits BLE mode and starts WiFi AP
7. If no BLE connection within 60s, uses saved/default credentials

### Project Structure
- `src/main.cpp` - Main application entry point (setup() and loop())
- `include/` - Header files for the project
- `lib/` - Private project libraries (compiled with the project)
- `test/` - Unit tests directory
- `platformio.ini` - Project configuration (board, platform, dependencies)

## Code Conventions

### Arduino Framework Patterns
- Use `setup()` for one-time initialization (WiFi, camera, serial)
- Use `loop()` for continuous execution (streaming, processing)
- Include `<Arduino.h>` as the primary framework header

### ESP32-Specific Considerations
When implementing video streaming features:
- **Camera Integration**: Use ESP32Camera library for OV2640 initialization
- **WiFi Streaming**: This project streams **MJPEG over HTTP** - use ESP32 web server to serve continuous multipart JPEG frames
- **Network Mode**: Device operates in **Access Point (AP) mode** - creates its own WiFi hotspot for direct connection
- **Memory Management**: ESP32S3 has PSRAM - enable it for frame buffers (`board_build.arduino.memory_type = qio_opi`)
- **Performance**: Use RTOS tasks for camera capture vs. network streaming to avoid blocking

### MJPEG Streaming Architecture
The video stream uses MJPEG (Motion JPEG) over HTTP with multipart content type:
- Camera captures JPEG frames continuously
- HTTP server sends frames with `multipart/x-mixed-replace` boundary
- Standard pattern: `Content-Type: multipart/x-mixed-replace; boundary=frame`
- Each frame preceded by boundary marker and JPEG headers
- Compatible with browsers and most video players without additional codecs

Example streaming handler structure:
```cpp
void handleStream(AsyncWebServerRequest *request) {
  AsyncResponseStream *response = request->beginResponseStream("multipart/x-mixed-replace; boundary=frame");
  // Loop: capture frame, write boundary, write JPEG data
}
```

### Pin Configuration
XIAO ESP32S3 with camera expansion board uses these pins:

**Camera Pins:**
- GPIO 10: XMCLK (Master clock)
- GPIO 11, 12, 14-18, 48: DVP_Y2-Y9 (8-bit parallel data)
- GPIO 13: DVP_PCLK (Pixel clock)
- GPIO 38: DVP_VSYNC (Vertical sync)
- GPIO 47: DVP_HREF (Horizontal reference)
- GPIO 39: Camera SCL (I2C clock for sensor config)
- GPIO 40: Camera SDA (I2C data for sensor config)

**Additional Peripherals:**
- GPIO 41: PDM Microphone DATA
- GPIO 42: PDM Microphone CLK
- GPIO 21: MicroSD SPI CS
- GPIO 7 (D8): MicroSD SPI SCK
- GPIO 8 (D9): MicroSD SPI MISO
- GPIO 9 (D10): MicroSD SPI MOSI

**OLED Display (0.96" I2C SSD1306):**
- GPIO 5 (D4): SDA (I2C Data)
- GPIO 6 (D5): SCL (I2C Clock)
- 3.3V: VCC
- GND: GND

Example camera initialization:
```cpp
#include "esp_camera.h"

camera_config_t config;
config.pin_d0 = 15;      // DVP_Y2
config.pin_d1 = 17;      // DVP_Y3
config.pin_d2 = 18;      // DVP_Y4
config.pin_d3 = 16;      // DVP_Y5
config.pin_d4 = 14;      // DVP_Y6
config.pin_d5 = 12;      // DVP_Y7
config.pin_d6 = 11;      // DVP_Y8
config.pin_d7 = 48;      // DVP_Y9
config.pin_xclk = 10;    // XMCLK
config.pin_pclk = 13;    // DVP_PCLK
config.pin_vsync = 38;   // DVP_VSYNC
config.pin_href = 47;    // DVP_HREF
config.pin_sscb_sda = 40; // Camera SDA
config.pin_sscb_scl = 39; // Camera SCL
config.pin_pwdn = -1;    // Power down (not used)
config.pin_reset = -1;   // Reset (not used)
config.xclk_freq_hz = 20000000;
config.frame_size = FRAMESIZE_SVGA;
config.pixel_format = PIXFORMAT_JPEG;
config.fb_count = 2;
config.grab_mode = CAMERA_GRAB_LATEST;
```

## Adding Dependencies
Add libraries to `platformio.ini` under the environment section:
```ini
[env:seeed_xiao_esp32s3]
lib_deps = 
    espressif/esp32-camera@^2.0.4
    me-no-dev/ESPAsyncWebServer@^1.2.3
```

## Common Tasks

### Serial Debugging
```cpp
Serial.begin(115200);
Serial.println("Debug message");
```

### WiFi Access Point Pattern
```cpp
#include <WiFi.h>
const char* ap_ssid = "ESP32-CAM";
const char* ap_password = "12345678";  // Min 8 characters

void setup() {
  WiFi.softAP(ap_ssid, ap_password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);  // Typically 192.168.4.1
}
```

### BLE Service UUIDs (for custom apps)
- **Service UUID**: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- **SSID Characteristic**: `beb5483e-36e1-4688-b7f5-ea07361b26a8` (Read/Write)
- **Password Characteristic**: `1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e` (Write only)
- **Status Characteristic**: `d8de624e-140f-4a23-8b85-726f9d55da18` (Read/Notify)

### Troubleshooting
- If upload fails: Hold BOOT button while connecting USB, or add `upload_speed = 115200` to platformio.ini
- Camera initialization errors: Verify pin configuration matches your board/expansion
- Out of memory: Enable PSRAM in board configuration
- Slow performance: Use dual-core RTOS tasks for parallel processing
- BLE not visible: Wait for "BLE Server started" message in serial monitor, ensure device is within Bluetooth range
- WiFi credentials not saving: Check NVS partition in platformio.ini (default partition scheme includes NVS)
