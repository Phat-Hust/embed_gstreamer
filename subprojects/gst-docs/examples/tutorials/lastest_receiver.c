#include <gst/gst.h>

int main(int argc, char *argv[]) {
    GstElement *pipeline;
    GstBus *bus;
    GstMessage *msg;

    gst_init(&argc, &argv);

    /* Build pipeline string:
       Receive RTP H264 over UDP (port 5000) → depay → decode → convert → sink
    */
    const gchar *pipeline_desc =
        "udpsrc address=127.0.0.1 port=5000 caps=\"application/x-rtp, "
        "media=video, encoding-name=H264, clock-rate=90000, payload=96\" ! "
        "rtph264depay ! avdec_h264 ! videoconvert ! autovideosink sync=false";

    pipeline = gst_parse_launch(pipeline_desc, NULL);
    if (!pipeline) {
        g_printerr("Failed to create pipeline\n");
        return -1;
    }

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    /* Wait until error or EOS */
    bus = gst_element_get_bus(pipeline);
    msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                     GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    if (msg != NULL) {
        GError *err;
        gchar *debug_info;

        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR:
            gst_message_parse_error(msg, &err, &debug_info);
            g_printerr("Error received: %s\n", err->message);
            g_printerr("Debug info: %s\n", debug_info ? debug_info : "none");
            g_clear_error(&err);
            g_free(debug_info);
            break;
        case GST_MESSAGE_EOS:
            g_print("End-Of-Stream reached.\n");
            break;
        default:
            break;
        }
        gst_message_unref(msg);
    }

    /* Clean up */
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    return 0;
}
