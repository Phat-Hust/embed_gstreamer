#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/un.h>

// Global variables
struct timespec lastPcktTime;
float frameDuration = 0.1;
GMainLoop *loop;
unsigned long long counter = 0;

float time_diff(struct timespec *start, struct timespec *end) {
  return (end->tv_sec - start->tv_sec) + (end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

static void pushKlv(GstElement *src) {
  GstFlowReturn ret;
  GstBuffer *buffer;
  bool fInsertKlv = false;
  g_print("Pushing KLV packet\n");
  while (!fInsertKlv) {
    struct timespec now;
    clock_t now_clock = clock();
    now.tv_sec = now_clock / CLOCKS_PER_SEC;
    now.tv_nsec = (now_clock % CLOCKS_PER_SEC) * (1000000000 / CLOCKS_PER_SEC);
    
    float diff = time_diff(&lastPcktTime, &now);

    if (diff > frameDuration) fInsertKlv = true;
    else usleep(10000);
  }

  // Create a GetBuffer of 100 bytes filled with 0x55
  size_t length = 100;
  GstMapInfo map;
  buffer = gst_buffer_new_allocate(NULL, length, NULL);
  gst_buffer_map(buffer, &map, GST_MAP_WRITE);
  memset(map.data, 0x55, length);
  gst_buffer_unmap(buffer, &map);

  GST_BUFFER_FLAG_SET(buffer, GST_STREAM_FLAG_SPARSE);
  GST_BUFFER_PTS(buffer) = 0;

  gst_buffer_fill(buffer, 0, buffer, length);
  ret = gst_app_src_push_buffer(GST_APP_SRC(src), buffer);

  if (ret != GST_FLOW_OK) {
    g_printerr("Error pushing buffer: %s\n", gst_flow_get_name(ret));
    g_main_loop_quit(loop);
  }else {
    clock_t now_clock = clock();
    lastPcktTime.tv_sec = now_clock / CLOCKS_PER_SEC;
    lastPcktTime.tv_nsec = (now_clock % CLOCKS_PER_SEC) * (1000000000 / CLOCKS_PER_SEC);
    counter++;
    g_print("Klv packet count: %llu.  Buf size: %zu\n", counter, length);
  }
  g_signal_emit_by_name(GST_APP_SRC(src), "need-data", NULL);
  free(buffer);
}

int main(int argc, char *argv[]) {
    GstElement *pipeline, *src, *conv, *enc, *pay, *udpsink, *dataAppSrc, *capsfilter;

    gst_init(&argc, &argv);

    /* Create elements */
    src     = gst_element_factory_make("v4l2src", "camera-source");
    conv    = gst_element_factory_make("videoconvert", "converter");
    enc     = gst_element_factory_make("x264enc", "encoder");
    pay     = gst_element_factory_make("rtph264pay", "payloader");
    udpsink = gst_element_factory_make("udpsink", "udpsink");
    capsfilter = gst_element_factory_make("capsfilter", NULL);
    dataAppSrc = gst_element_factory_make("appsrc", "NULL");

    if (!src || !conv || !enc || !pay || !udpsink) {
        g_printerr("Not all sender elements could be created.\n");
        return -1;
    }

    /* Create pipeline */
    pipeline = gst_pipeline_new("video-sender");
    if (!pipeline) {
        g_printerr("Pipeline could not be created.\n");
        return -1;
    }

    /* Configure elements */
    g_object_set(src, "device", "/dev/video0", NULL);
    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        "framerate", GST_TYPE_FRACTION, 30, 1,
                                        NULL);
    g_object_set(capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);                                            
    g_object_set(udpsink, "host", "127.0.0.1", "port", 5000, NULL);
    g_object_set(G_OBJECT(dataAppSrc), "caps", gst_caps_new_simple("meta/x-klv", "parsed", G_TYPE_BOOLEAN, TRUE, "sparse", G_TYPE_BOOLEAN, TRUE, "is-live", G_TYPE_BOOLEAN, TRUE, NULL), NULL);    g_object_set(G_OBJECT(dataAppSrc), "format", GST_FORMAT_TIME, NULL);
    g_object_set(G_OBJECT(dataAppSrc), "format", GST_FORMAT_TIME, NULL);
    g_object_set(G_OBJECT(dataAppSrc), "do-timestamps", TRUE, NULL);

    /* Link elements: v4l2src → videoconvert → x264enc → rtph264pay → udpsink */
    if (!gst_element_link_many(src, conv, enc, pay, udpsink, NULL)) {
        g_printerr("Sender elements could not be linked.\n");
        gst_object_unref(pipeline);
        return -1;
    }
    gst_bin_add_many(GST_BIN(pipeline), src, dataAppSrc, conv, enc, pay, udpsink, NULL);
    g_signal_connect(dataAppSrc, "need-data", G_CALLBACK(pushKlv), NULL);

    /* Start streaming */
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    g_print("Streaming from /dev/video0 to 127.0.0.1:5000 ...\n");

    /* Run loop */
    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                                 GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    if (msg) gst_message_unref(msg);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    return 0;
}
