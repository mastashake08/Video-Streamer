# ESP32-CAM Video & Audio Streamer

A real-time MJPEG video and audio streaming solution for the Seeed Studio XIAO ESP32S3 Sense development board. Stream live video and audio from your ESP32-CAM to any web browser over WiFi.

![ESP32S3](https://img.shields.io/badge/ESP32-S3-blue)
![PlatformIO](https://img.shields.io/badge/PlatformIO-compatible-orange)
![Arduino](https://img.shields.io/badge/Framework-Arduino-00979D)

## 🌟 Features

- **📹 Live Video Streaming**: MJPEG video stream at 800x600 (SVGA) resolution
- **🎙️ Real-time Audio**: WebSocket-based audio streaming from PDM microphone (16kHz, 16-bit PCM)
- **� SD Card Recording**: Record 10-second video/audio clips to SD card with timestamps
- **📱 BLE Control**: Start/stop recording via Bluetooth Low Energy (no WiFi needed)
- **💾 USB Mass Storage**: Access SD card files via USB (PSRAM-backed virtual disk)
- **� Web File Browser**: Download, view, and delete recorded files over WiFi
- **🌐 Dual Mode**: BLE recording mode OR WiFi streaming mode
- **🔄 Dual-Core Processing**: Optimized for ESP32's dual-core architecture
- **📱 Mobile Friendly**: Works on phones, tablets, and desktops
- **⚡ Low Latency**: Direct I2S to WebSocket audio pipeline

## 🛠️ Hardware Requirements

### Required
- **Seeed Studio XIAO ESP32S3 Sense** (with camera expansion board)
  - ESP32-S3R8 processor (dual-core @ 240MHz)
  - 8MB PSRAM + 8MB Flash
  - OV2640 or OV3660 camera sensor
  - PDM digital microphone
- **MicroSD Card** (up to 32GB FAT32) - for recording
- **USB-C Cable** for programming, power, and USB mass storage
- **WiFi Network** (2.4GHz) - for streaming mode

### Optional
- 0.96" I2C OLED Display (SSD1306) - currently disabled in code
- External antenna for extended range (100m+)
- Battery (4.2V LiPo with JST connector)

## 📋 Software Requirements

- [PlatformIO](https://platformio.org/) (IDE or CLI)
- [Visual Studio Code](https://code.visualstudio.com/) (recommended)

## 🚀 Quick Start

### 1. Clone the Repository

```bash
git clone https://github.com/mastashake08/Video-Streamer.git
cd Video-Streamer
```

### 2. Configure WiFi

Open `src/main.cpp` and update lines 14-15 with your WiFi credentials:

```cpp
const char* wifi_ssid = "YOUR_WIFI_SSID";
const char* wifi_password = "YOUR_WIFI_PASSWORD";
```

### 3. Build and Upload

```bash
# Build the project
pio run

# Upload to ESP32
pio run -t upload

# Monitor serial output
pio device monitor
```

Or use the **PlatformIO IDE** buttons in VS Code.

### 4. Access the Stream

After successful upload, the serial monitor will display:

```
✓ WiFi Connected!
========================================
IP Address: http://192.168.1.123
Stream URL: http://192.168.1.123/stream
========================================
```

Open the IP address in your web browser to view the stream!

## 📺 Usage

The device operates in two modes:

### 🔵 BLE Recording Mode (Default)

The device starts in BLE mode for battery-powered recording without WiFi.

#### BLE Control via Mobile App

Use a BLE app like **nRF Connect** (iOS/Android) or **LightBlue** (iOS):

1. Scan for device: **ESP32-CAM-BLE**
2. Connect to the device
3. Find the service and characteristics:

**BLE Service UUID**: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`

**Characteristics**:
- **Control** (Write): `beb5483e-36e1-4688-b7f5-ea07361b26a8` - Send commands
- **Status** (Read/Notify): `1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e` - Receive status updates
- **WiFi** (Write): `d8de624e-140f-4a23-8b85-726f9d55da18` - WiFi mode control

4. Write commands to **Control Characteristic**:

**Recording Commands:**
- `START` - Start recording 10-second clips
- `STOP` - Stop recording
- `STATUS` - Get recording statistics

**Recording Modes:**
- `AUDIO_ONLY` - Record only audio files
- `VIDEO_ONLY` - Record only video files
- `BOTH` - Record both (default)

**File Management:**
- `LIST_VIDEO` - List all video files
- `LIST_AUDIO` - List all audio files
- `LIST_ALL` - List all recorded files

**USB Mass Storage:**
- `ENABLE_USB` - Enable USB drive mode to access SD card files
- `DISABLE_USB` - Disable USB drive and resume recording

**Switch to WiFi:**
- Write `ENABLE_WIFI` to **WiFi Characteristic** (`d8de624e-140f-4a23-8b85-726f9d55da18`)

### 📡 WiFi Streaming Mode

Switch to WiFi mode via BLE command or set `wifiRequested = true` in code.

#### Web Interface

Navigate to the device's IP address in your browser:

- **Video** starts streaming automatically
- **Audio** requires user interaction - click the "🔊 Enable Audio" button
- **File Browser**: Access recorded files at `http://<IP>/files`
- **Mobile**: Works on iOS Safari and Android Chrome

#### Web Endpoints

- `http://<IP>/` - Main streaming interface
- `http://<IP>/stream` - Raw MJPEG video stream
- `http://<IP>/files` - Web-based file browser
- `http://<IP>/api/status` - Device status (JSON)
- `http://<IP>/api/files/list?path=/` - List files (JSON)
- `http://<IP>/api/files/download?path=/video/file.jpg` - Download file
- `ws://<IP>/audio` - WebSocket audio stream

### 💾 USB Mass Storage Mode

Access SD card files directly from your computer:

1. Send `ENABLE_USB` BLE command (or call via WiFi if implemented)
2. Connect USB-C cable to computer
3. A USB drive appears (PSRAM-backed virtual disk)
4. **Format as FAT32** on first use
5. Copy files to/from the virtual disk
6. **Safely eject** the drive
7. Send `DISABLE_USB` command to resume recording

**Note**: The USB drive is a PSRAM-backed RAM disk (512KB - 16MB depending on available memory). It's a temporary workspace - use the web file browser for direct SD card access.

### 📁 Web File Browser (Recommended)

The web file browser provides direct access to SD card contents without PSRAM limitations:

1. Connect to WiFi mode
2. Navigate to `http://<IP>/files`
3. Browse directories (video, audio, etc.)
4. Download files directly
5. Delete unwanted files
6. No USB cable needed!

## 💡 Usage Examples

### Example 1: Battery-Powered Recording

1. Insert SD card and power on device
2. Connect via BLE (nRF Connect app)
3. Send `START` command → Records 10-second clips continuously
4. Leave device recording for hours
5. Send `STOP` when done
6. Send `LIST_ALL` to see recorded files
7. Connect to WiFi or USB to retrieve files

### Example 2: Live Monitoring with Recording

1. Connect to WiFi first (send `ENABLE_WIFI` via BLE)
2. Open `http://<IP>` in browser to view live stream
3. Use BLE to send `START` command while watching
4. Device records clips while streaming
5. Use file browser to download recordings

### Example 3: Audio-Only Event Recording

1. Send `AUDIO_ONLY` via BLE
2. Send `START` to begin recording
3. Device records 10-second audio clips only (saves power/space)
4. Perfect for audio monitoring, interviews, lectures

### Example 4: Retrieve Files via Web Browser

1. Ensure WiFi mode is active
2. Navigate to `http://<IP>/files`
3. Click through `/video` and root directories
4. Download files individually
5. Delete old files to free space
6. No USB cable or app needed!

## 🔧 Pin Configuration

### Camera Pins (XIAO ESP32S3 Sense)
| Function | GPIO |
|----------|------|
| XMCLK | 10 |
| DVP_Y2-Y9 | 15, 17, 18, 16, 14, 12, 11, 48 |
| DVP_PCLK | 13 |
| DVP_VSYNC | 38 |
| DVP_HREF | 47 |
| Camera SCL (I2C) | 39 |
| Camera SDA (I2C) | 40 |

### Audio Pins
| Function | GPIO |
|----------|------|
| PDM Microphone DATA | 41 |
| PDM Microphone CLK | 42 |

### SD Card Pins (SPI Mode)
| Function | GPIO |
|----------|------|
| CS (Chip Select) | 21 |
| SCK (Clock) | 7 (D8) |
| MISO (Data In) | 8 (D9) |
| MOSI (Data Out) | 9 (D10) |

### Optional Display Pins (Disabled)
| Function | GPIO |
|----------|------|
| OLED SDA (I2C) | 5 (D4) |
| OLED SCL (I2C) | 6 (D5) |

## ⚙️ Configuration

### WiFi Credentials

Edit `src/main.cpp` lines 52-53:

```cpp
const char* wifi_ssid = "YOUR_WIFI_SSID";
const char* wifi_password = "YOUR_WIFI_PASSWORD";
```

### Recording Settings

```cpp
#define RECORD_TIME 10         // Seconds per clip (video/audio)
#define VOLUME_GAIN 2          // Audio amplification factor
```

### Video Settings

```cpp
.frame_size = FRAMESIZE_SVGA,  // Options: QVGA, VGA, SVGA, XGA, SXGA, UXGA
.jpeg_quality = 12,            // 0-63, lower = higher quality
.fb_count = 2,                 // Frame buffer count (1-2 with PSRAM)
```

### Audio Settings

```cpp
#define SAMPLE_RATE 16000      // Audio sample rate (Hz)
#define SAMPLE_BITS 16         // Bit depth
```

### Storage Management

```cpp
#define SD_MIN_FREE_SPACE_MB 100     // Trigger cleanup threshold
#define MAX_FILE_AGE_HOURS 24        // Auto-delete old files
#define CLEANUP_INTERVAL 3600000     // Cleanup frequency (ms)
```

## 🐛 Troubleshooting

### Upload Fails
- Hold the **BOOT button** while connecting USB
- Try reducing upload speed in `platformio.ini`:
  ```ini
  upload_speed = 115200
  ```

### Camera Initialization Failed
- Verify camera is properly connected to expansion board
- Check that camera connector is fully seated
- Ensure PSRAM is available (check serial output)
- Try power cycling the device

### SD Card Mount Failed
- Ensure SD card is properly inserted
- Format as FAT32 (exFAT and NTFS not supported)
- Use 32GB or smaller card
- Check card for physical damage

### WiFi Connection Failed
- Double-check WiFi credentials (case-sensitive)
- Ensure 2.4GHz WiFi (ESP32 doesn't support 5GHz)
- Check WiFi signal strength
- Serial monitor will show connection attempts

### BLE Not Visible
- Wait for "BLE Server started" message in serial
- Ensure device is within Bluetooth range (<10m)
- Try restarting BLE app
- Check that another device isn't already connected

### Audio Not Working
- Click "Enable Audio" in browser (required for autoplay policies)
- Check browser console for WebSocket errors
- Verify microphone is present on Sense version
- Check audio sample rate matches (16kHz)

### Recording Not Starting
- Ensure USB MSC is disabled (`DISABLE_USB` command)
- Check SD card has free space
- Verify SD mutex isn't locked (restart device)
- Look for error messages in serial output

### USB Mass Storage Issues
- **"Failed to allocate RAM disk"**: Limited PSRAM available
  - Use web file browser instead (`http://<IP>/files`)
  - Reduce camera resolution to free PSRAM
  - Progressive allocation tries smaller sizes automatically
- **Drive not appearing**: Check USB cable supports data transfer
- **Files not visible**: Format the virtual drive as FAT32 first
- **Can't write**: Ensure drive is mounted and not ejected

### Out of Memory Errors
- PSRAM is enabled by default in `platformio.ini`
- Reduce frame buffer count or resolution if issues persist
- Lower `fb_count` from 2 to 1 if needed
- Use web file browser instead of USB MSC to save PSRAM

## 📁 Project Structure

```
Video-Streamer/
├── src/
│   └── main.cpp              # Main application code (PlatformIO)
├── Video_Streamer/
│   └── Video_Streamer.ino    # Arduino IDE version
├── include/                  # Header files
├── lib/                      # Private libraries
├── test/                     # Unit tests
├── platformio.ini            # PlatformIO configuration
├── USB_MSC_README.md         # USB Mass Storage documentation
├── .github/
│   └── copilot-instructions.md  # AI coding agent instructions
└── README.md                 # This file
```

## 📚 Dependencies

All dependencies are managed automatically by PlatformIO:

- `espressif/esp32-camera@^2.0.4` - Camera driver
- `me-no-dev/ESPAsyncWebServer@^1.2.3` - Async web server
- `me-no-dev/AsyncTCP@^1.1.1` - Async TCP library
- `pschatzmann/ESP32-audioI2S@^3.0.0` - I2S audio (ESP_I2S 3.0)
- Arduino SD library (built-in) - SD card file system
- Arduino BLE library (built-in) - Bluetooth Low Energy
- TinyUSB (built-in) - USB Mass Storage Class

## 🔮 Future Enhancements

- [x] BLE control for recording
- [x] SD card recording with timestamps
- [x] USB Mass Storage access
- [x] Web-based file browser
- [ ] BLE configuration for WiFi credentials (write SSID/password via BLE)
- [ ] OLED display support for status (currently disabled)
- [ ] Motion detection for smart recording
- [ ] Time-lapse mode
- [ ] Multiple camera support
- [ ] WebRTC support for lower latency
- [ ] Mobile app companion with native UI

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## 📄 License

This project is open source and available under the [MIT License](LICENSE).

## 👤 Author

**Jyrone Parker** ([@mastashake08](https://github.com/mastashake08))

## 🙏 Acknowledgments

- Seeed Studio for the XIAO ESP32S3 platform
- Espressif for the ESP32-S3 chip and camera driver
- The Arduino and PlatformIO communities

## 📞 Support

- **Issues**: [GitHub Issues](https://github.com/mastashake08/Video-Streamer/issues)
- **Discussions**: [GitHub Discussions](https://github.com/mastashake08/Video-Streamer/discussions)

---

**⭐ Star this repository if you find it helpful!**
