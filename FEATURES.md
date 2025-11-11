# Video Streamer v2.0 - New Features

## 🎯 Implemented Features

### 1. ✅ Motion Detection
**Location:** `detectMotion()`, `updateLastFrame()`

- **How it works:** Compares consecutive frames pixel-by-pixel to detect changes
- **Configuration:**
  - `MOTION_THRESHOLD`: Sensitivity (0-255, default: 15)
  - `MOTION_MIN_PIXELS`: Minimum pixels changed to trigger (default: 1000)
- **Behavior:** Recording only starts when motion is detected, saving storage and power
- **Status:** Motion status shown in serial output and `/api/status` endpoint

### 2. ⏰ Timestamps on Recordings
**Location:** `initTime()`, `getTimestamp()`, `saveFrameToSD()`, `saveAudioToSD()`

- **NTP Sync:** Automatically syncs time from `pool.ntp.org` on WiFi connection
- **Filename Format:** 
  - Video: `/video/20251110_143025_frame_000001.jpg`
  - Audio: `/audio/20251110_143025_audio_000001.wav`
- **Configuration:**
  - `gmtOffset_sec`: Timezone offset in seconds
  - `daylightOffset_sec`: Daylight saving offset
- **Fallback:** Uses `millis()` if NTP sync fails

### 3. 🗑️ File Rotation & Cleanup
**Location:** `cleanupOldFiles()`

- **Auto-cleanup:** Runs every hour to delete old files when space is low
- **Configuration:**
  - `SD_MIN_FREE_SPACE_MB`: Minimum free space threshold (default: 100MB)
  - `MAX_FILE_AGE_HOURS`: Maximum file age before deletion (default: 24h)
  - `CLEANUP_INTERVAL`: Cleanup frequency (default: 1 hour)
- **Strategy:** Deletes oldest files first from `/video` directory
- **Status:** Cleanup logs show deleted files and recovered space

### 4. 🔋 Power Management
**Location:** `getBatteryVoltage()`, `checkBatteryStatus()`, `enterDeepSleep()`, `checkIdleTimeout()`

- **Battery Monitoring:**
  - Reads voltage from GPIO 1 (ADC)
  - Low battery warning at 3.3V threshold
  - Shows voltage in status output
- **Idle Detection:**
  - Enters deep sleep after 5 minutes of inactivity
  - Wakes up after 1 minute
- **Sleep Modes:**
  - Light sleep during idle
  - Deep sleep on prolonged inactivity or low battery
- **Configuration:**
  - `BATTERY_PIN`: ADC pin for voltage monitoring
  - `LOW_BATTERY_VOLTAGE`: Low battery threshold (3.3V)
  - `IDLE_SLEEP_TIMEOUT`: Idle timeout before sleep (5 min)

### 5. 🔄 OTA Updates
**Location:** `initOTA()`

- **Wireless Firmware Updates:** Upload new firmware over WiFi
- **Network Configuration:**
  - Hostname: `esp32-cam-streamer`
  - Password: `admin` (⚠️ CHANGE THIS!)
  - Port: 3232
- **Upload Methods:**
  - Arduino IDE: Tools → Port → Network Port
  - PlatformIO: `pio run -t upload --upload-port IP_ADDRESS`
  - Web browser: `http://IP_ADDRESS:3232`
- **Progress:** Shows upload progress in serial monitor
- **Safety:** Stops recording during update

### 6. 💡 Status LED Indicators
**Location:** `updateStatusLED()`

- **LED Patterns:**
  - **Fast blink (200ms):** Recording active
  - **Slow blink (1000ms):** WiFi connected/streaming
  - **Very fast blink (100ms):** Error or low battery
  - **Solid OFF:** Idle/initializing
- **LED Pin:** GPIO 21 (built-in LED)
- **State Machine:**
  - `STATE_INIT`: Initializing
  - `STATE_WIFI_CONNECTING`: Connecting to WiFi
  - `STATE_WIFI_CONNECTED`: WiFi connected
  - `STATE_RECORDING`: Recording to SD
  - `STATE_STREAMING`: Streaming over network
  - `STATE_ERROR`: System error
  - `STATE_LOW_BATTERY`: Battery low

## 🌐 New API Endpoints

### `/api/status` (GET)
Returns real-time system status in JSON format:

```json
{
  "uptime": 12345,
  "freeHeap": 234567,
  "sdFree": 1024,
  "sdTotal": 32768,
  "frames": 150,
  "audioFiles": 15,
  "motionDetected": true,
  "batteryVoltage": 4.2,
  "rssi": -45,
  "timestamp": "20251110_143025"
}
```

## ⚙️ Configuration Constants

### Motion Detection
```cpp
#define MOTION_THRESHOLD 15        // Pixel difference threshold
#define MOTION_MIN_PIXELS 1000     // Minimum pixels for motion
```

### Time & Date
```cpp
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;      // Your timezone offset
const int daylightOffset_sec = 0;  // DST offset
```

### File Management
```cpp
#define SD_MIN_FREE_SPACE_MB 100   // Minimum free space
#define MAX_FILE_AGE_HOURS 24      // File retention period
#define CLEANUP_INTERVAL 3600000   // 1 hour cleanup cycle
```

### Power Management
```cpp
#define BATTERY_PIN 1              // ADC pin
#define LOW_BATTERY_VOLTAGE 3.3    // Low battery threshold
#define IDLE_SLEEP_TIMEOUT 300000  // 5 minutes idle
```

### LED Indicators
```cpp
#define LED_BUILTIN 21             // LED GPIO
#define LED_BLINK_RECORDING 200    // Recording blink rate
#define LED_BLINK_WIFI 1000        // WiFi blink rate
#define LED_BLINK_ERROR 100        // Error blink rate
```

## 📊 Serial Monitor Output

The enhanced output now shows:
- Motion detection status
- Battery voltage
- Timestamp (if NTP synced)
- File cleanup operations
- OTA update progress
- System state changes

## 🔧 Usage Tips

### Adjusting Motion Sensitivity
- **More sensitive:** Lower `MOTION_THRESHOLD` (try 10)
- **Less sensitive:** Raise `MOTION_THRESHOLD` (try 25)
- **Larger objects only:** Increase `MOTION_MIN_PIXELS` (try 2000)

### Timezone Configuration
```cpp
// Example: EST (UTC-5)
const long gmtOffset_sec = -5 * 3600;
const int daylightOffset_sec = 3600;  // During DST
```

### Battery Monitoring Calibration
Adjust the voltage divider ratio in `getBatteryVoltage()`:
```cpp
float voltage = (adcValue / 4095.0) * 3.3 * 2.0; // 2.0 is divider ratio
```

### OTA Update via PlatformIO
```bash
# Find device IP first
pio device list

# Upload wirelessly
pio run -t upload --upload-port 192.168.1.100
```

### OTA Update via Arduino IDE
1. File → Preferences → Show verbose output
2. Tools → Port → Network Ports → esp32-cam-streamer
3. Upload as normal

## 🚀 Performance Impact

- **Motion Detection:** ~50ms per frame check (configurable)
- **File Cleanup:** Runs in background, <2 seconds typically
- **NTP Sync:** One-time 3-10 second delay on WiFi connect
- **Battery Check:** <5ms, runs every minute
- **OTA:** Pauses normal operation during update (~30-60s)

## 🐛 Troubleshooting

### Motion detection too sensitive
- Increase `MOTION_THRESHOLD` to 20-30
- Increase `MOTION_MIN_PIXELS` to 2000-3000

### Time not syncing
- Check WiFi connection
- Verify `ntpServer` is accessible
- Check timezone offsets

### SD card fills up anyway
- Lower `SD_MIN_FREE_SPACE_MB` threshold
- Decrease `MAX_FILE_AGE_HOURS`
- Reduce `CLEANUP_INTERVAL` to run more often

### Battery voltage reads 0.0V
- Check `BATTERY_PIN` configuration
- Verify voltage divider circuit
- Adjust divider ratio in `getBatteryVoltage()`

### OTA update fails
- Verify WiFi connection is stable
- Check password matches
- Ensure sufficient free heap memory
- Use lower upload speed

## 📈 Next Steps

Consider implementing:
- Web-based configuration UI
- Email/push notifications on motion
- Cloud storage integration
- Face detection
- Time-lapse mode
- Multiple motion zones
