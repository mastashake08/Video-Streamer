# ESP32-CAM Video & Audio Streamer

A real-time MJPEG video and audio streaming solution for the Seeed Studio XIAO ESP32S3 Sense development board. Stream live video and audio from your ESP32-CAM to any web browser over WiFi.

![ESP32S3](https://img.shields.io/badge/ESP32-S3-blue)
![PlatformIO](https://img.shields.io/badge/PlatformIO-compatible-orange)
![Arduino](https://img.shields.io/badge/Framework-Arduino-00979D)

## 🌟 Features

- **📹 Live Video Streaming**: MJPEG video stream at 800x600 (SVGA) resolution
- **🎙️ Real-time Audio**: WebSocket-based audio streaming from PDM microphone (16kHz, 16-bit PCM)
- **🌐 Web Interface**: Clean, responsive HTML5 interface with dark theme
- **📱 Mobile Friendly**: Works on phones, tablets, and desktops
- **🔄 Dual-Core Processing**: Optimized for ESP32's dual-core architecture
- **⚡ Low Latency**: Direct I2S to WebSocket audio pipeline

## 🛠️ Hardware Requirements

### Required
- **Seeed Studio XIAO ESP32S3 Sense** (with camera expansion board)
  - ESP32-S3R8 processor (dual-core @ 240MHz)
  - 8MB PSRAM + 8MB Flash
  - OV2640 or OV3660 camera sensor
  - PDM digital microphone
- **USB-C Cable** for programming and power
- **WiFi Network** (2.4GHz)

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

### Web Interface

Navigate to the device's IP address in your browser:

- **Video** starts streaming automatically
- **Audio** requires user interaction - click the "🔊 Enable Audio" button
- **Mobile**: Works on iOS Safari and Android Chrome

### Endpoints

- `http://<IP>/` - Main web interface
- `http://<IP>/stream` - Raw MJPEG stream
- `ws://<IP>/audio` - WebSocket audio stream

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
| PDM Microphone DATA | 42 |
| PDM Microphone CLK | 41 |

### Optional Display Pins (Disabled)
| Function | GPIO |
|----------|------|
| OLED SDA (I2C) | 5 (D4) |
| OLED SCL (I2C) | 6 (D5) |

## ⚙️ Configuration

### Video Settings

In `src/main.cpp`, adjust camera settings:

```cpp
.frame_size = FRAMESIZE_SVGA,  // Options: QVGA, VGA, SVGA, XGA, SXGA, UXGA
.jpeg_quality = 12,            // 0-63, lower = higher quality
.fb_count = 2,                 // Frame buffer count (1-2)
```

### Audio Settings

```cpp
#define SAMPLE_RATE 16000      // Audio sample rate (Hz)
#define BUFFER_SIZE 512        // Audio buffer size
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
- Try power cycling the device

### WiFi Connection Failed
- Double-check WiFi credentials (case-sensitive)
- Ensure 2.4GHz WiFi (ESP32 doesn't support 5GHz)
- Check WiFi signal strength
- Serial monitor will show connection attempts

### Audio Not Working
- Click "Enable Audio" in browser (required for autoplay policies)
- Check browser console for WebSocket errors
- Verify microphone is present on Sense version

### Out of Memory Errors
- PSRAM is enabled by default in `platformio.ini`
- Reduce frame buffer count or resolution if issues persist

## 📁 Project Structure

```
Video-Streamer/
├── src/
│   └── main.cpp              # Main application code
├── include/                  # Header files
├── lib/                      # Private libraries
├── test/                     # Unit tests
├── platformio.ini            # PlatformIO configuration
├── .github/
│   └── copilot-instructions.md  # AI coding agent instructions
└── README.md                 # This file
```

## 📚 Dependencies

All dependencies are managed automatically by PlatformIO:

- `espressif/esp32-camera@^2.0.4` - Camera driver
- `me-no-dev/ESPAsyncWebServer@^1.2.3` - Async web server
- `me-no-dev/AsyncTCP@^1.1.1` - Async TCP library
- `adafruit/Adafruit SSD1306@^2.5.7` - OLED display (optional)
- `adafruit/Adafruit GFX Library@^1.11.3` - Graphics library (optional)

## 🔮 Future Enhancements

- [ ] BLE configuration for WiFi credentials
- [ ] OLED display support (currently disabled)
- [ ] SD card recording
- [ ] Motion detection
- [ ] Multiple camera support
- [ ] WebRTC support for lower latency
- [ ] Mobile app companion

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
