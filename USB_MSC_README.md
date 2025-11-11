# USB Mass Storage Feature

## Overview
The ESP32-S3 now supports USB Mass Storage Class (MSC), allowing you to access recorded files via USB cable. The implementation uses a **PSRAM-backed virtual RAM disk** as a temporary workspace for file transfers.

## Important Note
⚠️ **This is a PSRAM-backed RAM disk, not direct SD card access!**

The device creates a virtual disk in PSRAM memory and exposes it as a USB drive. Files must be manually copied between the SD card and this RAM disk. This design was chosen because direct SD sector access via the Arduino SD library is not supported.

**Recommended Alternative**: Use the **Web File Browser** (`http://<IP>/files`) for direct SD card access without PSRAM requirements or file size limits.

## How It Works
- Creates a virtual disk in PSRAM (256KB to 16MB depending on available memory)
- Uses TinyUSB library (built into ESP32-S3)
- Progressive allocation: tries 16MB → 8MB → 4MB → 2MB → 1MB → 512KB → 256KB
- Reports detailed PSRAM usage before/after allocation
- Thread-safe operations via mutex

## Arduino IDE Settings
For the `.ino` file to compile with USB MSC support:

1. **Board**: XIAO_ESP32S3 (Seeed Studio XIAO ESP32S3)
2. **PSRAM**: OPI PSRAM
3. **Partition Scheme**: Huge APP (3MB No OTA/1MB SPIFFS)
4. **USB Mode**: Hardware CDC and JTAG ⚠️ **REQUIRED FOR USB MSC**
5. **USB CDC On Boot**: Enabled

## PlatformIO Settings
Already configured in `platformio.ini`:
```ini
build_flags = 
    -DUSE_TINYUSB
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
```

## Usage via BLE Commands

### Enable USB Mass Storage
1. Connect via BLE (nRF Connect, LightBlue, etc.)
2. Write `ENABLE_USB` to Control characteristic
3. Device will attempt progressive RAM disk allocation
4. Serial output shows allocation attempts and PSRAM usage
5. On success: "USB Mass Storage enabled" with allocated size
6. On failure: Recommends using Web File Browser instead
7. Connect USB cable to computer
8. Virtual RAM disk appears as removable drive
9. **Format as FAT32** on first use
10. Manually copy files to/from SD card as needed

### Disable USB Mass Storage
1. **IMPORTANT**: Eject/Safely Remove the drive from your OS first
2. Write `DISABLE_USB` to Control characteristic
3. Device responds: "USB Mass Storage DISABLED"
4. Recording can now resume

## Safety Features

### Automatic Recording Stop
- Recording **automatically stops** when USB MSC is enabled
- Prevents file system corruption from simultaneous access
- Recording task checks USB status before starting

### Conflict Prevention
- If USB MSC is active, `START` recording command is blocked
- User must send `DISABLE_USB` before recording can resume
- Loop monitors USB state and stops recording if USB becomes active

### Thread Safety
- All USB MSC callbacks use existing `sdMutex` semaphore
- Prevents race conditions between USB access and recording
- 1000ms timeout for mutex acquisition

### Safe Ejection
- `DISABLE_USB` command waits for OS to unmount drive
- 30-second timeout before forcing disable
- Small delay after disable to let SD card settle

## Limitations

1. **PSRAM Required**: Allocates 256KB to 16MB depending on availability
2. **Virtual Disk**: Not direct SD access - files must be manually copied
3. **Limited Size**: RAM disk size depends on free PSRAM (camera/audio use PSRAM)
4. **No Simultaneous Recording**: Recording disabled during USB access
5. **OS Compatibility**: Works on Windows, macOS, Linux (standard USB MSC)
6. **Single Client**: Only one computer can access at a time
7. **Manual File Transfer**: Not automatic - you must copy files manually

## Progressive Allocation

The device tries to allocate RAM disk in this order:
1. **16 MB** (32768 sectors) - Best for bulk transfers
2. **8 MB** (16384 sectors)
3. **4 MB** (8192 sectors)
4. **2 MB** (4096 sectors)
5. **1 MB** (2048 sectors)
6. **512 KB** (1024 sectors)
7. **256 KB** (512 sectors) - Minimum size

Serial output shows each attempt and reports:
- PSRAM total/used/free **before** allocation
- Which size succeeded
- PSRAM used/free **after** allocation
- Delta (how much was allocated)

BLE status characteristic receives notification with allocated size or failure message.

## File Access While in USB Mode

When USB MSC is enabled, the **RAM disk** (not SD card) is accessible:
- ✅ Access virtual RAM disk on computer
- ✅ Format as FAT32
- ✅ Copy files **TO** RAM disk from computer
- ✅ Copy files **FROM** RAM disk to computer
- ❌ Direct SD card access (must manually copy via code)
- ❌ Recording to SD card (disabled for safety)
- ❌ File listing via BLE (SD card busy)

**Note**: You must implement your own file copy mechanism between SD and RAM disk, or use the Web File Browser for direct SD access.

## Typical Workflow

### Download Recorded Files
```
1. Send "STOP" (if recording)
2. Send "ENABLE_USB"
3. Connect USB cable
4. Wait for drive to mount (~3-5 seconds)
5. Copy files from /video/ and /audio/ folders
6. Eject drive in OS
7. Send "DISABLE_USB"
8. Send "START" to resume recording
```

## BLE Commands Reference

| Command | Description | Response |
|---------|-------------|----------|
| `ENABLE_USB` | Enable USB Mass Storage mode | USB:Enabled |
| `DISABLE_USB` | Disable USB Mass Storage mode | USB:Disabled |
| `STATUS` | Check current status | Includes USB state |

## Troubleshooting

### "Failed to allocate RAM disk"
**Problem**: All allocation sizes failed, serial shows multiple "❌ FAILED" messages.

**Cause**: Insufficient free PSRAM (camera and audio buffers use PSRAM).

**Solutions**:
1. **Use Web File Browser instead** (recommended): `http://<IP>/files`
   - No PSRAM required
   - Direct SD card access
   - Unlimited file sizes
2. Reduce camera resolution to free PSRAM:
   - Change `FRAMESIZE_UXGA` to `FRAMESIZE_SVGA` or lower
   - Reduce `fb_count` from 2 to 1
3. Disable camera temporarily:
   - Comment out camera init to free ~4MB PSRAM
   - Use USB MSC to retrieve files
   - Re-enable camera after

### Drive Not Appearing
- Check Arduino IDE settings: USB Mode must be "Hardware CDC and JTAG"
- Verify USB cable supports data (not just charging)
- Check serial monitor for "USB Mass Storage enabled" message
- Verify allocation succeeded (not "Failed to allocate")
- Try different USB port

### "USB:Failed|UseBrowser" BLE notification
- All RAM disk sizes failed to allocate
- Switch to Web File Browser: `http://<IP>/files`
- Or free up PSRAM and retry

### Can't Disable USB
- Make sure drive is ejected in OS first
- Wait for "Unmounted" message in serial monitor
- If timeout occurs, re-send `DISABLE_USB`

### Recording Won't Start
- Check if USB MSC is still active
- Send `DISABLE_USB` first
- Verify "USB Mass Storage disabled" message
- Try `STATUS` command to check state

### Virtual Disk is Empty / No Files
- **This is expected!** USB MSC exposes a RAM disk, not the SD card
- Format the drive as FAT32 if needed
- You must manually implement file copying between SD and RAM disk
- **Recommended**: Use Web File Browser for direct SD access

### File System Errors
- Always eject drive before sending `DISABLE_USB`
- Never unplug USB cable while drive is mounted
- If corruption occurs on RAM disk, just disable and re-enable USB MSC
- SD card is unaffected by RAM disk corruption

## Technical Details

### Callback Functions
- `onRead()`: Reads 512-byte sectors from SD card
- `onWrite()`: Writes 512-byte sectors to SD card
- `onStartStop()`: Handles mount/unmount events

### Memory Usage
- USB MSC uses minimal heap RAM (~2KB for USB stack)
- RAM disk: 256KB to 16MB in PSRAM (dynamically allocated)
- Progressive allocation tries largest first
- Serial output shows exact PSRAM usage before/after
- BLE notification includes allocated size

### Performance
- Read speed: ~1-2 MB/s (depends on SD card class)
- Write speed: ~500KB-1MB/s (typical for SPI mode)
- No impact on camera/audio when USB inactive

## Code Architecture

### State Variables
```cpp
bool usbMscEnabled = false;   // USB MSC active
bool usbMscMounted = false;   // OS has mounted drive
```

### Key Functions
- `initUSBMSC()` - Initialize and start USB MSC
- `disableUSBMSC()` - Stop USB MSC safely
- `onRead/onWrite` - Sector-level SD access
- `onStartStop` - Mount/unmount handling

### Integration Points
- BLE `ControlCallbacks`: ENABLE_USB/DISABLE_USB commands
- `recordingTask()`: Checks USB state before starting
- `loop()`: Monitors USB state during recording
- All callbacks use `sdMutex` for thread safety
