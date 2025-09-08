#include <gst/gst.h>

int main(int argc, char *argv[]) {
    GstElement *pipeline, *src, *depay, *dec, *conv, *sink;
    GstBus *bus;
    GstMessage *msg;

    gst_init(&argc, &argv);

    /* Create elements */
    pipeline = gst_pipeline_new("receiver");
    src      = gst_element_factory_make("udpsrc", "src");
    depay    = gst_element_factory_make("rtph264depay", "depay");
    dec      = gst_element_factory_make("avdec_h264", "dec");
    conv     = gst_element_factory_make("videoconvert", "conv");
    sink     = gst_element_factory_make("autovideosink", "sink");

    if (!pipeline || !src || !depay || !dec || !conv || !sink) {
        g_printerr("Failed to create elements\n");
        return -1;
    }

    /* Configure udpsrc caps */
    GstCaps *caps = gst_caps_new_simple(
        "application/x-rtp",
        "media", G_TYPE_STRING, "video",
        "encoding-name", G_TYPE_STRING, "H264",
        "clock-rate", G_TYPE_INT, 90000,
        "payload", G_TYPE_INT, 96,
        NULL);
    g_object_set(src, "port", 5000, "caps", caps, NULL);
    gst_caps_unref(caps);

    /* Build pipeline */
    gst_bin_add_many(GST_BIN(pipeline), src, depay, dec, conv, sink, NULL);
    if (!gst_element_link_many(src, depay, dec, conv, sink, NULL)) {
        g_printerr("Failed to link elements\n");
        gst_object_unref(pipeline);
        return -1;
    }

    /* Start playing */
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    /* Wait until error or EOS */
    bus = gst_element_get_bus(pipeline);
    msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                     GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    if (msg != NULL) {
        GError *err;
        gchar *debug_info;

        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
            gst_message_parse_error(msg, &err, &debug_info);
            g_printerr("Error received: %s\n", err->message);
            g_printerr("Debug info: %s\n", debug_info ? debug_info : "none");
            g_clear_error(&err);
            g_free(debug_info);
        } else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
            g_print("End-Of-Stream reached.\n");
        }
        gst_message_unref(msg);
    }

    /* Cleanup */
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    return 0;
}
