#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <mutex>
#include <map>

// Global mutex for synchronizing FPS updates and console output
std::mutex fps_mutex;
// Global map to store FPS for each camera
std::map<std::string, int> fps_map;

class Camera {
public:
    Camera(const std::string& name, const std::string& uri) : name(name), uri(uri), frame_count(0), running(true), encoding_name(nullptr) {
        std::cout << "Initializing camera with URI: " << uri << std::endl;

        pipeline = gst_pipeline_new("pipeline");
        appsink = gst_element_factory_make("appsink", "sink");
        GstElement* source = gst_element_factory_make("rtspsrc", "source");
        rtph264depay = gst_element_factory_make("rtph264depay", "h264depay");
        rtph265depay = gst_element_factory_make("rtph265depay", "h265depay");
        rtpmjpegdepay = gst_element_factory_make("rtpjpegdepay", "mjpegdepay");
        rtppv8depay = gst_element_factory_make("rtpvp8depay", "vp8depay");
        rtppv9depay = gst_element_factory_make("rtpvp9depay", "vp9depay");
        rtph263depay = gst_element_factory_make("rtph263depay", "h263depay");

        if (!pipeline || !appsink || !source || !rtph264depay || !rtph265depay || 
            !rtpmjpegdepay || !rtppv8depay || !rtppv9depay || !rtph263depay) {
            std::cerr << "Failed to create GStreamer elements!" << std::endl;
            return;
        }

        // Configure the source
        g_object_set(source, "location", uri.c_str(), NULL);
        // g_object_set(source, "latency", 100, NULL);
        // g_object_set(source, "buffer-mode", 2, NULL);
        g_object_set(source, "protocols", 4, NULL); // set TCP read
        g_object_set (source, "short-header", 1, NULL);

        // Configure appsink
        g_object_set(appsink, "emit-signals", TRUE, NULL);
        g_signal_connect(appsink, "new-sample", G_CALLBACK(&Camera::on_new_sample), this);

        // Set up the pipeline
        gst_bin_add_many(GST_BIN(pipeline), source, rtph264depay, rtph265depay, 
                         rtpmjpegdepay, rtppv8depay, rtppv9depay, rtph263depay, appsink, NULL);
        g_signal_connect(source, "pad-added", G_CALLBACK(&Camera::on_pad_added), this);

        std::cout << "Camera initialized successfully." << std::endl;
    }

    ~Camera() {
        std::cout << "Cleaning up camera for URI: " << uri << std::endl;
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(GST_OBJECT(pipeline));
    }

    void start() {
        std::cout << "Starting camera: " << uri << std::endl;
        if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
            std::cerr << "Failed to start pipeline for camera: " << uri << std::endl;
        } else {
            std::cout << "Camera started successfully: " << uri << std::endl;
        }
    }

    void stop() {
        std::cout << "Stopping camera: " << uri << std::endl;
        if (gst_element_set_state(pipeline, GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE) {
            std::cerr << "Failed to stop pipeline for camera: " << uri << std::endl;
        } else {
            std::cout << "Camera stopped successfully: " << uri << std::endl;
        }
    }

    void run(int interval) {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(interval));  // Check FPS every 'interval' seconds
            std::lock_guard<std::mutex> lock(mutex);
            int fps = frame_count / interval;
            frame_count = 0;  // Reset frame count after printing

            // Update the global FPS map
            {
                std::lock_guard<std::mutex> fps_lock(fps_mutex);
                fps_map[name] = fps;
            }
        }
    }


    static void on_pad_added(GstElement* src, GstPad* pad, Camera* camera) {
        std::cout << "Pad added for camera: " << camera->uri << std::endl;

        GstCaps* caps = gst_pad_query_caps(pad, NULL);
        GstStructure* s = gst_caps_get_structure(caps, 0);
        
        camera->encoding_name = gst_structure_get_string(s, "encoding-name");
        if (camera->encoding_name) {
            std::cout << "ENCODING NAME: " << camera->encoding_name << std::endl;
        } else {
            std::cout << "Failed to get encoding-name." << std::endl;
            return; // Exit if encoding-name is not found
        }

        GstPad* sink_pad = nullptr;

        // Link the new pad to the appropriate depayloader
        if (g_strcmp0(camera->encoding_name, "H265") == 0) {
            sink_pad = gst_element_get_static_pad(camera->rtph265depay, "sink");
        } 
        else if (g_strcmp0(camera->encoding_name, "H264") == 0) {
            sink_pad = gst_element_get_static_pad(camera->rtph264depay, "sink");
        }
        else if (g_strcmp0(camera->encoding_name, "JPEG") == 0) {
            sink_pad = gst_element_get_static_pad(camera->rtpmjpegdepay, "sink");
        }
        else if (g_strcmp0(camera->encoding_name, "VP8") == 0) {
            sink_pad = gst_element_get_static_pad(camera->rtppv8depay, "sink");
        }
        else if (g_strcmp0(camera->encoding_name, "VP9") == 0) {
            sink_pad = gst_element_get_static_pad(camera->rtppv9depay, "sink");
        }
        else if (g_strcmp0(camera->encoding_name, "H263") == 0) {
            sink_pad = gst_element_get_static_pad(camera->rtph263depay, "sink");
        } else {
            std::cerr << "Unsupported encoding name: " << camera->encoding_name << std::endl;
            return; // Exit if encoding is not supported
        }

        // Link the new pad to the depayloader
        if (gst_pad_link(pad, sink_pad) != GST_PAD_LINK_OK) {
            std::cerr << "Failed to link pad from rtspsrc to depayloader!" << std::endl;
        } else {
            std::cout << "Linked pad from rtspsrc to depayloader." << std::endl;

            // Now link the depayloader to appsink
            if (g_strcmp0(camera->encoding_name, "H264") == 0) {
                if (gst_element_link(camera->rtph264depay, camera->appsink) != TRUE) {
                    std::cerr << "Failed to link rtph264depay -> appsink!" << std::endl;
                }
            } else if (g_strcmp0(camera->encoding_name, "H265") == 0) {
                if (gst_element_link(camera->rtph265depay, camera->appsink) != TRUE) {
                    std::cerr << "Failed to link rtph265depay -> appsink!" << std::endl;
                }
            } else if (g_strcmp0(camera->encoding_name, "JPEG") == 0) {
                if (gst_element_link(camera->rtpmjpegdepay, camera->appsink) != TRUE) {
                    std::cerr << "Failed to link rtpmjpegdepay -> appsink!" << std::endl;
                }
            } else if (g_strcmp0(camera->encoding_name, "VP8") == 0) {
                if (gst_element_link(camera->rtppv8depay, camera->appsink) != TRUE) {
                    std::cerr << "Failed to link rtppv8depay -> appsink!" << std::endl;
                }
            } else if (g_strcmp0(camera->encoding_name, "VP9") == 0) {
                if (gst_element_link(camera->rtppv9depay, camera->appsink) != TRUE) {
                    std::cerr << "Failed to link rtppv9depay -> appsink!" << std::endl;
                }
            } else if (g_strcmp0(camera->encoding_name, "H263") == 0) {
                if (gst_element_link(camera->rtph263depay, camera->appsink) != TRUE) {
                    std::cerr << "Failed to link rtph263depay -> appsink!" << std::endl;
                }
            }
        }

        gst_object_unref(sink_pad);
    }

    static GstFlowReturn on_new_sample(GstElement* sink, Camera* camera) {
        GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
        if (sample) {
            std::lock_guard<std::mutex> lock(camera->mutex);
            camera->frame_count++;
            // std::cout << "Counter ..." << std::endl;
            gst_sample_unref(sample); // Free the sample
            return GST_FLOW_OK;
        } else {
            std::cerr << "Failed to pull sample for camera: " << camera->uri << std::endl;
            return GST_FLOW_ERROR; // Drop the sample if it fails
        }
    }

    void set_running(bool state) {
        running = state;
    }

private:
    std::string name;  // Added camera name
    std::string uri;
    GstElement* pipeline;
    GstElement* appsink;
    GstElement* rtph264depay; // Make rtph264depay a member variable
    GstElement* rtph265depay;
    GstElement* rtpmjpegdepay;
    GstElement* rtppv8depay;
    GstElement* rtppv9depay;
    GstElement* rtph263depay;
    const gchar* encoding_name;
    int frame_count;
    bool running;
    std::mutex mutex;  // To protect frame_count
};


void print_fps(int interval) {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(interval));
        
        std::lock_guard<std::mutex> fps_lock(fps_mutex);
        size_t count = fps_map.size();
        size_t current = 0;

        for (const auto& entry : fps_map) {
            ++current;
            if (entry.second == 0) {
                // Print in red if FPS is 0
                std::cout << "\033[1;31m" << entry.first << ": " << entry.second << " FPS\033[0m";
            } else {
                // Normal print
                std::cout << entry.first << ": " << entry.second << " FPS";
            }

            // Print a comma unless it's the last element
            if (current < count) {
                std::cout << ", ";
            }
        }
        std::cout << std::endl;
    }
}



int main(int argc, char* argv[]) {
    gst_init(&argc, &argv);

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <interval_in_seconds>" << std::endl;
        return 1;
    }

    int interval = std::stoi(argv[1]);

    std::vector<Camera*> cameras;
    std::vector<std::thread> threads;

    // Add camera URIs here
    // std::map<std::string, std::string> camera_uris = {
    //     {"cam1", "rtsp://xxx/h264"},
    //     {"cam2", "rtsp://xxx/h265"},
    //     {"cam3", "rtsp://xxx/mjpeg"},
    //     {"cam4", "rtsp://xxx/vp8"},
    //     {"cam5", "rtsp://xxx/vp9"},
    //     {"cam6", "rtsp://xxx/h263"}
    // };
    // Testing 
    std::map<std::string, std::string> camera_uris;
    for (int i = 0; i < 2; ++i) {
        camera_uris["cam" + std::to_string(i)] = "rtsp://localhost:8754/dfc2839f-6f6b-459f-9644-877382fccede";
    }

    for (const auto& entry : camera_uris) {
        const std::string& name = entry.first;
        const std::string& uri = entry.second;
        Camera* camera = new Camera(name, uri);
        camera->start();
        cameras.push_back(camera);
        threads.emplace_back(&Camera::run, camera, interval);
    }

    // Create a separate thread to print the FPS values
    std::thread print_thread(print_fps, interval);

    std::this_thread::sleep_for(std::chrono::seconds(300));  // Run for 5 minutes (can be adjusted)

    for (auto& camera : cameras) {
        camera->set_running(false);
        camera->stop();
        delete camera;
    }

    for (auto& thread : threads) {
        thread.join();
    }
    // Ensure print thread stops gracefully (optional, depending on use case)
    print_thread.detach();

    return 0;
}
