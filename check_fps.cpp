#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <mutex>
#include <map>
#include <string>
#include <fstream>
#include <iomanip>
// Global mutex for synchronizing FPS updates and console output
std::mutex fps_mutex;
// Global map to store FPS for each camera
std::map<std::string, int> fps_map;
std::map<std::string, int> downtime_map; // Track downtime in seconds for each camera

class Camera {
public:
    Camera(const std::string& name, const std::string& uri) : name(name), uri(uri), frame_count(0), running(true) {
        std::cout << "Initializing camera with URI: " << uri << std::endl;
        downtime_map[name] = -1;
        pipeline = gst_pipeline_new("pipeline");
        appsink = gst_element_factory_make("appsink", "sink");
        source = gst_element_factory_make("rtspsrc", "source");
        parsebin = gst_element_factory_make("parsebin", "parsebin");

        if (!pipeline || !appsink || !source || !parsebin) {
            std::cerr << "Failed to create GStreamer elements!" << std::endl;
            return;
        }

        // Configure the source
        g_object_set(source, "location", uri.c_str(), NULL);
        g_object_set(source, "protocols", 4, NULL); // set TCP read

        // Configure appsink
        g_object_set(appsink, "emit-signals", TRUE, NULL);
        g_signal_connect(appsink, "new-sample", G_CALLBACK(&Camera::on_new_sample), this);

        // Set up the pipeline
        gst_bin_add_many(GST_BIN(pipeline), source, parsebin, appsink, NULL);
        g_signal_connect(source, "pad-added", G_CALLBACK(&Camera::on_pad_added), this);

        // Link parsebin to appsink
        g_signal_connect(parsebin, "pad-added", G_CALLBACK(&Camera::on_parsebin_pad_added), this);

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

                // Check for downtime and handle reconnect if necessary
                if (fps == 0) {
                    downtime_map[name] += 1;
                    if (downtime_map[name] >= 5) {  // Reconnect if downtime is >= 5*interval seconds
                        std::cout << "Reconnecting camera: " << name << std::endl;
                        stop();
                        start();
                        downtime_map[name] = 0;  // Reset downtime counter after reconnect
                    }
                } else {
                    downtime_map[name] = 0;  // Reset downtime counter if FPS > 0
                }
            }
        }
    }

    static void on_pad_added(GstElement* src, GstPad* pad, Camera* camera) {
        std::cout << "Pad added for camera: " << camera->uri << std::endl;

        GstCaps* caps = gst_pad_query_caps(pad, NULL);
        GstStructure* s = gst_caps_get_structure(caps, 0);
        const gchar* encoding_name = gst_structure_get_string(s, "encoding-name");

        if (encoding_name) {
            std::cout << "ENCODING NAME: " << encoding_name << std::endl;
        } else {
            std::cout << "Failed to get encoding-name." << std::endl;
            return; // Exit if encoding-name is not found
        }

        GstPad* sink_pad = gst_element_get_static_pad(camera->parsebin, "sink");
        if (gst_pad_link(pad, sink_pad) != GST_PAD_LINK_OK) {
            std::cerr << "Failed to link pad from rtspsrc to parsebin!" << std::endl;
        }
        gst_object_unref(sink_pad);
    }

    static void on_parsebin_pad_added(GstElement* parsebin, GstPad* pad, Camera* camera) {
        std::cout << "Pad added for parsebin for camera: " << camera->uri << std::endl;

        // Link to appsink
        GstPad* sink_pad = gst_element_get_static_pad(camera->appsink, "sink");
        if (gst_pad_link(pad, sink_pad) != GST_PAD_LINK_OK) {
            std::cerr << "Failed to link parsebin pad to appsink!" << std::endl;
        } else {
            std::cout << "Linked pad from parsebin to appsink." << std::endl;
        }

        gst_object_unref(sink_pad);
    }

    static GstFlowReturn on_new_sample(GstElement* sink, Camera* camera) {
        GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
        if (sample) {
            std::lock_guard<std::mutex> lock(camera->mutex);
            camera->frame_count++;
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
    GstElement* parsebin;
    GstElement* source;
    const gchar* encoding_name;
    int frame_count;
    bool running;
    std::mutex mutex;  // To protect frame_count
};

std::map<std::string, std::string> read_camera_uris(const std::string& filename) {
    std::map<std::string, std::string> camera_uris;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Could not open the file: " << filename << std::endl;
        return camera_uris; // Return an empty map
    }
    
    std::string line;
    int index = 0; // Start indexing from 0

    while (std::getline(file, line)) {
        if (!line.empty()) { // Check if the line is not empty
            camera_uris["cam"+ std::to_string(index++)] = line; // Store the line in the map
        }
    }

    file.close(); // Close the file
    return camera_uris;
}

void print_fps(int interval) {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(interval));
        
        std::lock_guard<std::mutex> fps_lock(fps_mutex);
        size_t count = fps_map.size();
        size_t current = 0;


        // Get the current time
        std::time_t now = std::time(nullptr);
        std::tm* local_time = std::localtime(&now);

        // Print the timestamp
        std::cout << "[\033[1;34m" << std::put_time(local_time, "%d:%m:%Y %H:%M:%S") << "]\033[0m ";

        for (const auto& entry : fps_map) {
            ++current;
            if (entry.second < 5) {
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

    // Add camera URIs here or read from a file
    // Example: std::map<std::string, std::string> camera_uris = {
    //     {"Camera1", "rtspt://localhost:8554/test"},
    //     {"Camera2", "rtspt://localhost:8554/test2"}
    // };

    std::map<std::string, std::string> camera_uris = read_camera_uris("../cameras.txt");
    
    for (const auto& entry : camera_uris) {
        Camera* camera = new Camera(entry.first, entry.second);
        camera->start();
        cameras.push_back(camera);
        threads.emplace_back(&Camera::run, camera, interval); // Start camera run in a thread
    }

    std::thread fps_thread(print_fps, interval); // Start the FPS printing thread

    // Run the main thread for a fixed duration
    std::this_thread::sleep_for(std::chrono::seconds(6000)); // Run for 6000 seconds

    // Cleanup
    for (auto& camera : cameras) {
        camera->set_running(false); // Stop the camera thread
        camera->stop(); // Stop the camera
        delete camera; // Free the memory
    }

    fps_thread.join(); // Wait for FPS thread to finish
    return 0;
}
