# Web File Browser Guide

## Overview

The web file browser provides a convenient way to access, download, and manage files on the SD card without requiring USB Mass Storage or PSRAM allocation. This is the **recommended method** for file management on the ESP32-CAM.

## Features

✅ **Direct SD Card Access** - No PSRAM required  
✅ **Web-Based Interface** - Works on any device with a browser  
✅ **Download Files** - Single-click download of recordings  
✅ **Delete Files** - Free up SD card space  
✅ **Directory Navigation** - Browse folders (video, audio, etc.)  
✅ **File Information** - See file sizes and types  
✅ **Mobile Friendly** - Works on phones and tablets  

## How to Use

### 1. Access the File Browser

1. Ensure the device is in **WiFi mode** (send `ENABLE_WIFI` via BLE if needed)
2. Find the device IP address in serial monitor
3. Navigate to: `http://<IP_ADDRESS>/files`

Example: `http://192.168.1.123/files`

### 2. Navigate Directories

- Click on **folder names** to open directories
- Click **".."** to go back to parent directory
- Root directory shows audio files (WAV)
- `/video` directory shows video files (JPEG frames)

### 3. Download Files

- Click the **⬇️ Download** button next to any file
- File will download to your device
- No file size limits (streams directly from SD card)

### 4. Delete Files

- Click the **🗑️ Delete** button next to any file
- Confirm deletion in the popup
- File is permanently removed from SD card
- Frees up space for new recordings

### 5. Refresh View

- Click **🔄 Refresh** button to update the file list
- Useful after recording new files or deleting

## API Endpoints

The file browser is built on REST API endpoints that can be used programmatically:

### List Files
```
GET /api/files/list?path=/video
```

Response:
```json
{
  "path": "/video",
  "files": [
    {
      "name": "frame_000001.jpg",
      "size": 45678,
      "isDir": false
    }
  ]
}
```

### Download File
```
GET /api/files/download?path=/video/frame_000001.jpg
```

Response: Binary file data with `Content-Disposition: attachment`

### Delete File
```
DELETE /api/files/delete?path=/video/frame_000001.jpg
```

Response:
```json
{
  "success": true
}
```

### Status Information
```
GET /api/status
```

Response:
```json
{
  "uptime": 12345,
  "freeHeap": 123456,
  "sdFree": 2048,
  "sdTotal": 8192,
  "frames": 100,
  "audioFiles": 50,
  "batteryVoltage": 4.2,
  "rssi": -45,
  "timestamp": "20231115_123045"
}
```

## File Browser vs USB Mass Storage

| Feature | File Browser | USB MSC |
|---------|--------------|---------|
| **PSRAM Required** | ❌ None | ✅ 512KB - 16MB |
| **File Size Limits** | ❌ Unlimited | ✅ Limited by RAM disk |
| **Access Method** | WiFi + Browser | USB Cable |
| **OS Compatibility** | All (web-based) | Windows/Mac/Linux |
| **Concurrent Recording** | ✅ Yes | ❌ No (must stop) |
| **Setup Required** | None | Format as FAT32 |
| **Best For** | Direct SD access | Bulk file transfer |

## Tips & Best Practices

### Performance
- The file browser streams files directly from SD card
- Large files may take time to download on slow WiFi
- Use WiFi mode for best performance (not BLE)

### Storage Management
- Regularly delete old files to free space
- The device auto-deletes files when space is low
- Check status endpoint for free space information

### Security
- The file browser has no authentication
- Use on trusted WiFi networks only
- Consider adding password protection for production use

### Troubleshooting

**File browser page won't load**
- Ensure device is in WiFi mode
- Check IP address is correct
- Verify WiFi connection is active

**Can't see files**
- Click refresh button
- Check SD card is mounted (serial output)
- Verify files exist (use BLE `LIST_ALL` command)

**Download fails**
- Check SD card is not corrupted
- Ensure file hasn't been deleted
- Try smaller files first

**"SD card busy" error**
- Recording is active - send `STOP` command
- SD mutex is locked - restart device if persistent

## Example Usage Scripts

### Python Script - Bulk Download

```python
import requests
import json

IP = "192.168.1.123"

# List all video files
response = requests.get(f"http://{IP}/api/files/list?path=/video")
files = response.json()["files"]

# Download each file
for file in files:
    if not file["isDir"]:
        path = f"/video/{file['name']}"
        print(f"Downloading {file['name']}...")
        
        file_response = requests.get(
            f"http://{IP}/api/files/download?path={path}"
        )
        
        with open(file['name'], 'wb') as f:
            f.write(file_response.content)
        
        print(f"✓ Downloaded {file['name']} ({file['size']} bytes)")
```

### cURL - Download Single File

```bash
# Download a specific file
curl "http://192.168.1.123/api/files/download?path=/video/frame_000001.jpg" \
  -o frame_000001.jpg

# List files in directory
curl "http://192.168.1.123/api/files/list?path=/video" | jq

# Delete a file
curl -X DELETE \
  "http://192.168.1.123/api/files/delete?path=/video/old_frame.jpg"
```

## Integration Examples

### JavaScript - Auto-refresh File List

```javascript
async function autoRefresh() {
  setInterval(async () => {
    const response = await fetch('/api/files/list?path=/video');
    const data = await response.json();
    console.log(`${data.files.length} files found`);
    // Update UI...
  }, 5000); // Refresh every 5 seconds
}
```

### Node.js - Watch for New Files

```javascript
const axios = require('axios');

let previousCount = 0;

setInterval(async () => {
  const response = await axios.get('http://192.168.1.123/api/files/list?path=/video');
  const currentCount = response.data.files.length;
  
  if (currentCount > previousCount) {
    console.log(`New files detected! Count: ${currentCount}`);
    // Download new files...
  }
  
  previousCount = currentCount;
}, 10000);
```

## Conclusion

The web file browser is the most convenient and reliable way to access SD card files on the ESP32-CAM. It requires no PSRAM, works from any device with a browser, and allows for concurrent recording while browsing files.

For bulk file transfers, consider using the Python script above or implementing your own automation based on the REST API endpoints.
