#include <gst/gst.h>

typedef struct _CustomData {
    GstElement *pipeline;
    GstElement *udpsrc;
    GstElement *tsdemux;
    GstElement *videoQueue;
    GstElement *dataQueue;
    GstElement *h264parse;
    GstElement *avdec_h264;
    GstElement *videoconvert;
    GstElement *videosink;
    GstElement *dataSink;
} CustomData;

/* This function will be called by the pad-added signal */
static void pad_added_handler(GstElement *src, GstPad *new_pad, CustomData *data) {
    GstPad *sink_pad = NULL;
    GstPadLinkReturn ret;
    GstCaps *new_pad_caps = NULL;
    GstStructure *new_pad_struct = NULL;
    const gchar *new_pad_type = NULL;

    g_print("Received new pad '%s' from '%s':\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

    /* Get the pad's capabilities */
    new_pad_caps = gst_pad_get_current_caps(new_pad);
    new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
    new_pad_type = gst_structure_get_name(new_pad_struct);

    g_print("Pad type: %s\n", new_pad_type);

    if (g_str_has_prefix(new_pad_type, "video/x-h264")) {
        sink_pad = gst_element_get_static_pad(data->videoQueue, "sink");
        g_print("Linking video pad\n");
    } else if (g_str_has_prefix(new_pad_type, "meta/x-klv")) {
        sink_pad = gst_element_get_static_pad(data->dataQueue, "sink");
        g_print("Linking metadata pad\n");
    }

    if (sink_pad) {
        if (gst_pad_is_linked(sink_pad)) {
            g_print("Pad already linked. Ignoring.\n");
            goto exit;
        }

        ret = gst_pad_link(new_pad, sink_pad);
        if (GST_PAD_LINK_FAILED(ret)) {
            g_print("Link failed.\n");
        } else {
            g_print("Link succeeded.\n");
        }
    }

exit:
    if (new_pad_caps != NULL)
        gst_caps_unref(new_pad_caps);
    if (sink_pad != NULL)
        gst_object_unref(sink_pad);
}

/* The appsink has received a buffer */
static GstFlowReturn new_sample(GstElement *sink, CustomData *data) {
    GstSample *sample;

    /* Retrieve the buffer */
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (sample) {
        GstBuffer *gstBuffer = gst_sample_get_buffer(sample);

        if (gstBuffer) {
            GstClockTime pts = GST_BUFFER_PTS(gstBuffer);
            GstClockTime dts = GST_BUFFER_DTS(gstBuffer);

            // Get current real time for comparison
            gint64 real_time = g_get_real_time();
            double real_time_sec = real_time / 1000000.0;

            gsize bufSize = gst_buffer_get_size(gstBuffer);
            
            GstMapInfo map;
            gst_buffer_map(gstBuffer, &map, GST_MAP_READ);

            if (GST_CLOCK_TIME_IS_VALID(pts)) {
                g_print("RECEIVER: Real time: %.6f sec | KLV metadata: %.*s (size: %ld bytes) PTS: %" GST_TIME_FORMAT "\n", 
                        real_time_sec, (int)map.size, (char*)map.data, bufSize, GST_TIME_ARGS(pts));
            } else {
                g_print("RECEIVER: Real time: %.6f sec | KLV metadata: %.*s (size: %ld bytes) PTS: INVALID\n", 
                        real_time_sec, (int)map.size, (char*)map.data, bufSize);
            }

            gst_buffer_unmap(gstBuffer, &map);
            gst_sample_unref(sample);
            return GST_FLOW_OK;
        }
    }

    return GST_FLOW_ERROR;
}

int main(int argc, char *argv[]) {
    CustomData data;
    GstBus *bus;
    GstMessage *msg;

    gst_init(&argc, &argv);

    /* Create elements */
    data.pipeline = gst_pipeline_new("receiver-pipeline");
    data.udpsrc = gst_element_factory_make("udpsrc", "src");
    data.tsdemux = gst_element_factory_make("tsdemux", "demux");
    g_object_set(data.tsdemux, "latency", 0, NULL);
    
    /* Video processing elements */
    data.videoQueue = gst_element_factory_make("queue", "video-queue");
    g_object_set(data.videoQueue,
                 "max-size-buffers", 5,     // Small buffer
                 "max-size-time", 0,        // No time limit
                 "max-size-bytes", 0,       // No byte limit
                 "leaky", 2,
                 NULL);
    data.h264parse = gst_element_factory_make("h264parse", "video-parse");
    data.avdec_h264 = gst_element_factory_make("avdec_h264", "video-decoder");
    data.videoconvert = gst_element_factory_make("videoconvert", "video-convert");
    data.videosink = gst_element_factory_make("autovideosink", "video-sink");
    
    g_object_set(data.videosink,
                 "sync", FALSE,             // Don't sync video to clock
                 "async", FALSE,            // Don't buffer video
                 "max_lateness", 0,
                 NULL);

    /* Metadata processing elements */
    data.dataQueue = gst_element_factory_make("queue", "data-queue");
    data.dataSink = gst_element_factory_make("appsink", "data-sink");

    if (!data.pipeline || !data.udpsrc || !data.tsdemux || !data.videoQueue || 
        !data.h264parse || !data.avdec_h264 || !data.videoconvert || !data.videosink ||
        !data.dataQueue || !data.dataSink) {
        g_printerr("Failed to create elements\n");
        return -1;
    }

    /* Configure UDP source */
    g_object_set(data.udpsrc, "port", 5000,"buffer-size", 2097152, "timeout", 0, "close-socket", FALSE, NULL);

    /* Configure appsink for metadata */
    g_object_set(data.dataSink, "emit-signals", TRUE, "sync", TRUE, "async", FALSE, "drop", TRUE, "max-buffers", 1, NULL);
    g_signal_connect(data.dataSink, "new-sample", G_CALLBACK(new_sample), &data);
    g_print("Callback connected successfully.\n");

    /* Add elements to pipeline */
    gst_bin_add_many(GST_BIN(data.pipeline), 
                     data.udpsrc, data.tsdemux,
                     data.videoQueue, data.h264parse, data.avdec_h264, data.videoconvert, data.videosink,
                     data.dataQueue, data.dataSink, NULL);

    /* Link static elements */
    if (!gst_element_link(data.udpsrc, data.tsdemux)) {
        g_printerr("Failed to link udpsrc to tsdemux\n");
        return -1;
    }

    /* Link video processing chain */
    if (!gst_element_link_many(data.videoQueue, data.h264parse, data.avdec_h264, 
                               data.videoconvert, data.videosink, NULL)) {
        g_printerr("Failed to link video processing chain\n");
        return -1;
    }

    /* Link metadata processing chain */
    if (!gst_element_link(data.dataQueue, data.dataSink)) {
        g_printerr("Failed to link metadata processing chain\n");
        return -1;
    }

    /* Connect to the pad-added signal */
    g_signal_connect(data.tsdemux, "pad-added", G_CALLBACK(pad_added_handler), &data);

    /* Start playing */
    gst_element_set_state(data.pipeline, GST_STATE_PLAYING);

    g_print("Receiving video + metadata...\n");

    /* Wait until error or EOS */
    bus = gst_element_get_bus(data.pipeline);
    while (TRUE) {
        msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                         GST_MESSAGE_ERROR);
        
        if (msg != NULL) {
            if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                GError *err;
                gchar *debug_info;
                gst_message_parse_error(msg, &err, &debug_info);
                g_printerr("Error: %s\n", err->message);
                g_printerr("Debug: %s\n", debug_info ? debug_info : "none");
                g_clear_error(&err);
                g_free(debug_info);
                gst_message_unref(msg);
                break;  // Only exit on actual errors
            }
            gst_message_unref(msg);
        }
    }
    gst_object_unref(bus);
    gst_element_set_state(data.pipeline, GST_STATE_NULL);
    gst_object_unref(data.pipeline);

    return 0;
}