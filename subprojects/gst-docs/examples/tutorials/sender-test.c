#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <string.h>
#include <unistd.h>

static GstClockTime current_real_time = GST_CLOCK_TIME_NONE;

static gboolean push_metadata(gpointer user_data) {
    GstElement *appsrc = GST_ELEMENT(user_data);
    static int counter = 0;

    // Get wall-clock time in microseconds since epoch
    gint64 real_time = g_get_real_time();
    double real_time_sec = real_time / 1000000.0;

    // Update global variable
    current_real_time = (GstClockTime)real_time * 1000; // convert usec -> nsec

    // Prepare metadata string
    gchar *meta_str = g_strdup_printf("frame_metadata_%d", counter++);
    GstBuffer *buffer = gst_buffer_new_allocate(NULL, strlen(meta_str), NULL);
    gst_buffer_fill(buffer, 0, meta_str, strlen(meta_str));

    // Set timestamps for the metadata buffer
    GST_BUFFER_PTS(buffer) = current_real_time;
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(1, GST_SECOND, 6); // ~30fps

    // Push buffer into appsrc
    GstFlowReturn ret;
    g_signal_emit_by_name(appsrc, "push-buffer", buffer, &ret);

    if (ret != GST_FLOW_OK) {
        g_printerr("Error pushing metadata buffer\n");
        gst_buffer_unref(buffer);
        g_free(meta_str);
        return FALSE;
    }

    g_print("Frame %d | Real time: %.6f sec | Metadata: '%s' | PTS: %" GST_TIME_FORMAT "\n",
            counter, real_time_sec, meta_str,
            GST_TIME_ARGS(GST_BUFFER_PTS(buffer)));

    gst_buffer_unref(buffer);
    g_free(meta_str);

    return TRUE; // keep running
}

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);

    GstElement *pipeline, *src, *videoconvert, *x264enc, *h264parse, *mux, *udp, *metasrc;

    pipeline = gst_pipeline_new("video-meta-pipeline");

    // Video branch
    src = gst_element_factory_make("v4l2src", "camera");
    g_object_set(src, "device", "/dev/video0", NULL);
    
    videoconvert = gst_element_factory_make("videoconvert", NULL);
    
    x264enc = gst_element_factory_make("x264enc", NULL);
    g_object_set(x264enc, 
                 "tune", 4,           // zerolatency
                 "bitrate", 500,      // 500 kbps
                 "speed-preset", 1,   // ultrafast
                 NULL);

    h264parse = gst_element_factory_make("h264parse", NULL);

    // Metadata branch
    metasrc = gst_element_factory_make("appsrc", "meta-src");
    g_object_set(metasrc,
                 "caps", gst_caps_new_simple("meta/x-klv",
                                             "parsed", G_TYPE_BOOLEAN, TRUE,
                                             "sparse", G_TYPE_BOOLEAN, TRUE,
                                             "is-live", G_TYPE_BOOLEAN, TRUE,
                                             NULL),
                 "format", GST_FORMAT_TIME,
                 "do-timestamp", TRUE,
                 "min-latency", 0,
                 "max-latency", 0,
                 NULL);

    // Muxer + UDP sink
    mux = gst_element_factory_make("mpegtsmux", NULL);
    g_object_set(mux, "alignment", 7, "prog-map-pid", 256, NULL);
    
    udp = gst_element_factory_make("udpsink", NULL);
    g_object_set(udp, "host", "127.0.0.1", "port", 5000, "sync", FALSE, "async", FALSE, "buffer-size", 1024, NULL);

    if (!pipeline || !src || !videoconvert || !x264enc || !h264parse || !mux || !udp || !metasrc) {
        g_printerr("Failed to create elements\n");
        return -1;
    }

    gst_bin_add_many(GST_BIN(pipeline), src, videoconvert, x264enc, h264parse, metasrc, mux, udp, NULL);

    if (!gst_element_link_many(src, videoconvert, x264enc, h264parse, mux, udp, NULL)) {
        g_printerr("Video branch linking failed\n");
        return -1;
    }

    if (!gst_element_link(metasrc, mux)) {
        g_printerr("Metadata branch linking failed\n");
        return -1;
    }

    // Push metadata in loop
    g_timeout_add(1000 / 5, push_metadata, metasrc); // ~30fps

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    g_print("Streaming video + metadata...\n");
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    return 0;
}