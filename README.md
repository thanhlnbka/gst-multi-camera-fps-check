# gst-multi-camera-fps-check

A GStreamer-based application for monitoring and displaying the FPS of multiple RTSP cameras. This application supports various video codecs, including `H264`, `H265`, `MJPEG`, `VP8`, `VP9`, and `H263`.

## Features


- **Codec support**: Handles a wide range of video codecs, including `H264`, `H265`, `MJPEG`, `VP8`, `VP9` and `H263`.
- **Customizable FPS check interval**: Define the interval (in seconds) for calculating and displaying the FPS.
- **Real-time FPS display**: Outputs the FPS for each camera in a readable format (e.g., `cam1: 5 FPS, cam2: 4 FPS`).

## Prerequisites

- **GStreamer**: Ensure GStreamer and its development libraries are installed on your system.
- **C++ Compiler**: You will need a C++ compiler that supports C++11 or higher.

### Installation (Linux/Ubuntu)

```bash
sudo apt-get update
sudo apt-get install gstreamer1.0 gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-tools libgstreamer1.0-dev build-essential
```

### Build 

```bash
git clone https://github.com/thanhlnbka/gst-multi-camera-fps-check.git
cd gst-multi-camera-fps-check
mkdir build
cd build 
cmake ..
make
```

### Usage

Run the application with a specified interval (in seconds) for FPS checks. For example, to check FPS every 5 seconds:


```bash
./check_fps <interval_in_seconds>
```

### Customization

* Adding cameras: Modify the `camera_uris` map in the `main.cpp` file to add or change RTSP camera URIs.
* Runtime duration: You can adjust the runtime duration by modifying the `std::this_thread::sleep_for(std::chrono::seconds(300));` line in `main.cpp`.

### Example Output

```bash
[dd:mm:yyyy h:m:s] cam1: 5 FPS, cam2: 4 FPS, cam3: 6 FPS, ...
```

### Note

If you encounter an error when reading more than 200 RTSP streams simultaneously with the message ***Creating pipes for GWakeup: Too many open files*** you can try running the following command in your terminal to increase the maximum number of open files:

```bash
ulimit -n 4096
```

