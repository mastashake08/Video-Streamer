# Quick Configuration Guide

## Essential Settings

### 1. WiFi Credentials
```cpp
// Line 15-16 in main.cpp
const char* wifi_ssid = "YourWiFiName";
const char* wifi_password = "YourPassword";
```

### 2. Timezone Configuration
```cpp
// Line 32-34 in main.cpp
const long gmtOffset_sec = -5 * 3600;  // Example: EST (-5 hours)
const int daylightOffset_sec = 3600;   // Add 1 hour during DST
```

**Common Timezones:**
- PST/PDT: `-8 * 3600` / `3600`
- MST/MDT: `-7 * 3600` / `3600`
- CST/CDT: `-6 * 3600` / `3600`
- EST/EDT: `-5 * 3600` / `3600`
- UTC: `0` / `0`

### 3. Motion Detection Sensitivity
```cpp
// Line 25-26 in main.cpp
#define MOTION_THRESHOLD 15      // Lower = more sensitive (5-50)
#define MOTION_MIN_PIXELS 1000   // Minimum changed pixels (500-3000)
```

**Presets:**
- **Very Sensitive**: `THRESHOLD=8`, `MIN_PIXELS=500`
- **Normal**: `THRESHOLD=15`, `MIN_PIXELS=1000` ← Default
- **Less Sensitive**: `THRESHOLD=25`, `MIN_PIXELS=2000`
- **Large Objects Only**: `THRESHOLD=20`, `MIN_PIXELS=3000`

### 4. SD Card Management
```cpp
// Line 37-39 in main.cpp
#define SD_MIN_FREE_SPACE_MB 100  // Start cleanup when below this
#define MAX_FILE_AGE_HOURS 24     // Delete files older than this
#define CLEANUP_INTERVAL 3600000  // Check every hour (ms)
```

**Storage Scenarios:**
- **Small SD Card (8GB)**: `MIN_FREE=50MB`, `MAX_AGE=12h`
- **Medium SD Card (16GB)**: `MIN_FREE=100MB`, `MAX_AGE=24h` ← Default
- **Large SD Card (32GB)**: `MIN_FREE=500MB`, `MAX_AGE=72h`
- **Archive Mode**: `MIN_FREE=50MB`, `MAX_AGE=168h` (1 week)

### 5. Power Management
```cpp
// Line 42-44 in main.cpp
#define BATTERY_PIN 1              // ADC pin for battery
#define LOW_BATTERY_VOLTAGE 3.3    // Warning threshold
#define IDLE_SLEEP_TIMEOUT 300000  // 5 minutes (ms)
```

**Battery Scenarios:**
- **Always On**: `IDLE_SLEEP_TIMEOUT = 86400000` (24 hours)
- **Aggressive Savings**: `IDLE_SLEEP_TIMEOUT = 60000` (1 minute)
- **Balanced**: `IDLE_SLEEP_TIMEOUT = 300000` (5 minutes) ← Default

### 6. OTA Update Password
```cpp
// Line 183 in main.cpp (inside initOTA function)
ArduinoOTA.setPassword("admin");  // ⚠️ CHANGE THIS!
```

**Security:**
- Use strong password (8+ characters)
- Mix letters, numbers, symbols
- Don't use default "admin"

### 7. LED Blink Rates
```cpp
// Line 47-49 in main.cpp
#define LED_BLINK_RECORDING 200    // Fast (ms)
#define LED_BLINK_WIFI 1000        // Slow (ms)
#define LED_BLINK_ERROR 100        // Very fast (ms)
```

## Example Configurations

### Home Security Camera
```cpp
// Sensitive to motion, keep recent footage
const long gmtOffset_sec = -8 * 3600;  // PST
#define MOTION_THRESHOLD 12
#define MOTION_MIN_PIXELS 800
#define SD_MIN_FREE_SPACE_MB 200
#define MAX_FILE_AGE_HOURS 48
#define IDLE_SLEEP_TIMEOUT 600000  // 10 min
```

### Wildlife Camera
```cpp
// Less sensitive, long battery life
const long gmtOffset_sec = -7 * 3600;  // MST
#define MOTION_THRESHOLD 25
#define MOTION_MIN_PIXELS 2000
#define SD_MIN_FREE_SPACE_MB 100
#define MAX_FILE_AGE_HOURS 72
#define IDLE_SLEEP_TIMEOUT 60000   // 1 min
#define LOW_BATTERY_VOLTAGE 3.5
```

### Office Monitor
```cpp
// Normal sensitivity, always powered
const long gmtOffset_sec = -5 * 3600;  // EST
#define MOTION_THRESHOLD 15
#define MOTION_MIN_PIXELS 1000
#define SD_MIN_FREE_SPACE_MB 500
#define MAX_FILE_AGE_HOURS 24
#define IDLE_SLEEP_TIMEOUT 86400000  // Never sleep
```

### Baby Monitor
```cpp
// Very sensitive, immediate response
const long gmtOffset_sec = 0;  // Local time
#define MOTION_THRESHOLD 8
#define MOTION_MIN_PIXELS 500
#define SD_MIN_FREE_SPACE_MB 100
#define MAX_FILE_AGE_HOURS 12
#define IDLE_SLEEP_TIMEOUT 3600000  // 1 hour
```

## Testing Your Configuration

### 1. Test Motion Detection
```cpp
// In setup(), add:
Serial.printf("Motion Config: THRESHOLD=%d, MIN_PIXELS=%d\n", 
              MOTION_THRESHOLD, MOTION_MIN_PIXELS);
```

### 2. Test File Cleanup
```cpp
// Force cleanup test by lowering threshold temporarily
#define SD_MIN_FREE_SPACE_MB 30000  // Very high to trigger cleanup
```

### 3. Test Battery Monitoring
```cpp
// In loop(), add temporary logging:
Serial.printf("Battery: %.2fV\n", getBatteryVoltage());
```

### 4. Verify Timestamp
Access `http://DEVICE_IP/api/status` and check `"timestamp"` field

## Troubleshooting Settings

### Motion Detection Not Working
```cpp
// Try more sensitive settings
#define MOTION_THRESHOLD 10
#define MOTION_MIN_PIXELS 500
```

### Too Many False Triggers
```cpp
// Try less sensitive settings
#define MOTION_THRESHOLD 30
#define MOTION_MIN_PIXELS 2500
```

### SD Card Keeps Filling Up
```cpp
// More aggressive cleanup
#define SD_MIN_FREE_SPACE_MB 500
#define MAX_FILE_AGE_HOURS 6
#define CLEANUP_INTERVAL 1800000  // Check every 30 min
```

### Battery Draining Too Fast
```cpp
// More aggressive power saving
#define IDLE_SLEEP_TIMEOUT 120000  // 2 minutes
#define LOW_BATTERY_VOLTAGE 3.6    // Earlier warning
```

### Time Shows Wrong
```cpp
// Adjust timezone offset (in seconds)
const long gmtOffset_sec = YOUR_OFFSET * 3600;

// Examples:
// GMT+1: 1 * 3600
// GMT-5: -5 * 3600
// GMT+5.5: int(5.5 * 3600)
```

## Advanced: Environment Variables (Optional)

Instead of hardcoding, use environment variables in `platformio.ini`:

```ini
build_flags = 
    -DBOARD_HAS_PSRAM
    -DCORE_DEBUG_LEVEL=3
    -Wno-narrowing
    -DWIFI_SSID=\"${sysenv.WIFI_SSID}\"
    -DWIFI_PASSWORD=\"${sysenv.WIFI_PASSWORD}\"
    -DGMT_OFFSET=${sysenv.GMT_OFFSET}
```

Then in your shell:
```bash
export WIFI_SSID="MyNetwork"
export WIFI_PASSWORD="MyPassword"
export GMT_OFFSET=-8
pio run -t upload
```

## API Quick Reference

### Get System Status
```bash
curl http://DEVICE_IP/api/status
```

### Response Fields
- `uptime`: Seconds since boot
- `freeHeap`: Available RAM (bytes)
- `sdFree`/`sdTotal`: SD card space (MB)
- `frames`: Total video frames captured
- `audioFiles`: Total audio files created
- `motionDetected`: Currently detecting motion (bool)
- `batteryVoltage`: Battery level (V)
- `rssi`: WiFi signal strength (dBm)
- `timestamp`: Current time (if NTP synced)

---

**Quick Start Checklist:**
1. ☐ Set WiFi credentials
2. ☐ Configure timezone
3. ☐ Adjust motion sensitivity (if needed)
4. ☐ Change OTA password
5. ☐ Upload firmware
6. ☐ Test motion detection
7. ☐ Verify timestamp in /api/status
8. ☐ Monitor for 24h to tune settings
