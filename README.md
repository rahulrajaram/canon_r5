# Canon R5 Linux Driver Suite

Linux kernel driver suite for Canon R5 camera providing complete integration with standard Linux APIs (V4L2, ALSA, MTP).

## Quick Start

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt install build-essential linux-headers-$(uname -r) git

# Fedora/RHEL
sudo dnf install kernel-devel kernel-headers gcc make git

# Arch Linux
sudo pacman -S linux-headers base-devel git
```

### Installation

```bash
git clone https://github.com/your-username/canon-r5-linux-driver.git
cd canon-r5-linux-driver
make
sudo make install
```

### Loading Drivers

```bash
# Load core modules
sudo modprobe canon-r5-core
sudo modprobe canon-r5-usb

# Load feature modules (optional)
sudo modprobe canon-r5-video    # For video streaming
sudo modprobe canon-r5-still    # For still image capture
sudo modprobe canon-r5-audio    # For audio recording
sudo modprobe canon-r5-storage  # For file transfer
```

### Basic Usage

After connecting your Canon R5:

```bash
# Check if camera is detected
lsusb | grep Canon

# List video devices
v4l2-ctl --list-devices

# Stream video with ffmpeg
ffmpeg -f v4l2 -i /dev/video0 output.mp4

# Check camera files via MTP
gio mount mtp://Canon_R5/

# Monitor camera events
dmesg | grep canon-r5
```

## Camera Setup

1. **Power on** your Canon R5
2. Navigate to **Menu → Communication Settings → USB Connection**
3. Select **PC Connection** mode
4. Connect USB-C cable to camera and USB-A/USB-C to computer
5. Camera LCD should show "PC Connection" indicator

## Supported Features

| Feature | Status | Interface | Notes |
|---------|--------|-----------|-------|
| Video Streaming | ✅ | `/dev/video*` | V4L2 compatible |
| Still Capture | ✅ | `/dev/canon-r5-still` | RAW/JPEG support |
| Audio Recording | ✅ | `/dev/snd/pcm*` | ALSA compatible |
| File Transfer | ⚠️ | MTP mount | Basic implementation |
| Camera Control | ⚠️ | `/sys/class/canon-r5/` | In development |
| Battery Monitor | ⚠️ | `/sys/class/power_supply/` | In development |

✅ = Fully supported, ⚠️ = Beta/partial support, ❌ = Not implemented

## Troubleshooting

### Camera Not Detected

**Problem**: `lsusb` doesn't show Canon device

**Solutions**:
1. Check USB cable connection
2. Verify camera is in PC Connection mode
3. Try different USB port (USB 3.0 recommended)
4. Check dmesg: `dmesg | grep -i usb`

```bash
# Enable USB debugging
echo 'module usbcore +p' | sudo tee /sys/kernel/debug/dynamic_debug/control
```

### Module Loading Errors

**Problem**: `modprobe` fails to load modules

**Check module dependencies**:
```bash
sudo depmod -a
modinfo canon-r5-core
```

**Check for conflicts**:
```bash
# Remove conflicting modules
sudo rmmod gphoto2
sudo rmmod ptp

# Reload canon-r5 modules
sudo modprobe canon-r5-core
```

**Verify kernel headers match**:
```bash
uname -r
ls /lib/modules/$(uname -r)/build
```

### Video Streaming Issues

**Problem**: No video devices appear

**Check V4L2 registration**:
```bash
# List all video devices
v4l2-ctl --list-devices

# Check specific device capabilities
v4l2-ctl -d /dev/video0 --all

# Test basic capture
v4l2-ctl -d /dev/video0 --stream-mmap --stream-count=10
```

**Camera-specific checks**:
- Ensure camera is not in sleep mode
- Check LCD shows "Live View" or streaming indicator
- Verify USB bandwidth (USB 3.0 for higher resolutions)

### Still Image Capture Problems

**Problem**: Cannot capture images or access files

**Check PTP session**:
```bash
# Monitor PTP communication
dmesg | grep canon-r5-ptp

# Verify capture device
ls -l /dev/canon-r5-*

# Test basic capture
echo "single" > /sys/class/canon-r5/capture/mode
echo "trigger" > /sys/class/canon-r5/capture/action
```

**File system access**:
```bash
# Check MTP mounts
gio mount -l | grep -i canon

# Manual MTP mount
simple-mtpfs /mnt/camera

# Check available space
df -h /mnt/camera
```

### Audio Recording Issues

**Problem**: No audio capture device

**ALSA troubleshooting**:
```bash
# List audio devices
arecord -l

# Test audio capture
arecord -D hw:Canon,0 -f cd test.wav

# Check mixer controls
amixer -c Canon contents
```

### Permission Problems

**Problem**: Access denied errors

**Fix udev permissions**:
```bash
# Check current permissions
ls -l /dev/video* /dev/canon-r5-*

# Add user to video group
sudo usermod -a -G video $USER

# Create udev rules (optional)
sudo tee /etc/udev/rules.d/99-canon-r5.rules << EOF
SUBSYSTEM=="usb", ATTR{idVendor}=="04a9", MODE="0666"
KERNEL=="video*", SUBSYSTEM=="video4linux", MODE="0664", GROUP="video"
EOF

sudo udevadm control --reload
```

## Performance Optimization

### USB Performance
```bash
# Check USB speed
lsusb -t

# Optimize USB buffer sizes
echo 16 > /sys/module/usbcore/parameters/usbfs_memory_mb

# Disable USB autosuspend for camera
echo on > /sys/bus/usb/devices/*/power/control
```

### Video Streaming
```bash
# Reduce CPU usage with hardware acceleration
ffmpeg -f v4l2 -i /dev/video0 -c:v h264_vaapi output.mp4

# Adjust V4L2 buffer count
v4l2-ctl -d /dev/video0 --set-fmt-video=width=1920,height=1080,pixelformat=MJPG
```

## Advanced Configuration

### Manual Module Parameters

```bash
# Load with debug enabled
sudo modprobe canon-r5-core debug=1

# Adjust buffer sizes
sudo modprobe canon-r5-video buffers=8

# Enable experimental features
sudo modprobe canon-r5-still raw_support=1
```

### Persistent Settings

Create `/etc/modprobe.d/canon-r5.conf`:
```
# Canon R5 driver options
options canon-r5-core debug=0
options canon-r5-video buffers=4
options canon-r5-still raw_support=1
```

## Known Limitations

1. **USB PIDs**: Currently uses placeholder values - may not detect all R5 variants
2. **Live View Resolution**: Limited to camera's USB streaming capabilities
3. **Dual Card Access**: Simultaneous access to both cards not fully implemented
4. **WiFi Features**: Camera's built-in WiFi not accessible through driver
5. **Lens Control**: Limited lens communication (focus/aperture only)

## Getting Help

### Debug Information
Before reporting issues, collect this information:

```bash
# System info
uname -a
lsb_release -a

# Hardware info
lsusb -v | grep -A 20 Canon
lsmod | grep canon

# Driver logs
dmesg | grep canon-r5 | tail -50

# Module info
modinfo canon-r5-core
```

### Support Channels

- **GitHub Issues**: Primary support channel
- **Discussions**: General questions and community support
- **Wiki**: Additional documentation and tutorials

### Reporting Bugs

Include the following in bug reports:
1. Kernel version (`uname -r`)
2. Distribution and version
3. Canon R5 firmware version
4. Complete dmesg output
5. Steps to reproduce
6. Expected vs actual behavior

## Contributing

See [PROGRESS.md](PROGRESS.md) for development status and technical details.

### Development Setup
```bash
# Install development dependencies
sudo apt install linux-headers-$(uname -r) build-essential \
                 v4l-utils libv4l-dev libasound2-dev

# Build and test
make clean
make DEBUG=1
sudo make install
make test
```

### Code Style
- Follow Linux kernel coding standards
- Run `checkpatch.pl` before submitting
- Include proper error handling and cleanup

## License

GPL v2 - See [LICENSE](LICENSE) file for details.

This project is not affiliated with Canon Inc. Canon and EOS are trademarks of Canon Inc.