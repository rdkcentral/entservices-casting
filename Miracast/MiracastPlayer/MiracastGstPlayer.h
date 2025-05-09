/**
 * If not stated otherwise in this file or this component's LICENSE
 * file the following copyright and licenses apply:
 *
 * Copyright 2023 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 **/

#ifndef _MIRACAST_GST_PLAYER_H_
#define _MIRACAST_GST_PLAYER_H_

#include <string>
#include <vector>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <glib.h>
#include <pthread.h>
#include <stdint.h>

/**
 * @enum GstPlayFlags
 * @brief Enum of configuration flags used by playbin
 */
typedef enum {
	GST_PLAY_FLAG_VIDEO = (1 << 0),             /**< value is 0x001 */
	GST_PLAY_FLAG_AUDIO = (1 << 1),             /**< value is 0x002 */
	GST_PLAY_FLAG_TEXT = (1 << 2),              /**< value is 0x004 */
	GST_PLAY_FLAG_VIS = (1 << 3),               /**< value is 0x008 */
	GST_PLAY_FLAG_SOFT_VOLUME = (1 << 4),       /**< value is 0x010 */
	GST_PLAY_FLAG_NATIVE_AUDIO = (1 << 5),      /**< value is 0x020 */
	GST_PLAY_FLAG_NATIVE_VIDEO = (1 << 6),      /**< value is 0x040 */
	GST_PLAY_FLAG_DOWNLOAD = (1 << 7),          /**< value is 0x080 */
	GST_PLAY_FLAG_BUFFERING = (1 << 8),         /**< value is 0x100 */
	GST_PLAY_FLAG_DEINTERLACE = (1 << 9),       /**< value is 0x200 */
	GST_PLAY_FLAG_SOFT_COLORBALANCE = (1 << 10) /**< value is 0x400 */
}GstPlayFlags;

class MiracastGstPlayer
{
public:
    static MiracastGstPlayer *getInstance();
    static void destroyInstance();
    bool launch(std::string& localip , std::string& streaming_port,MiracastRTSPMsg *rtsp_instance);
    bool stop();
    bool pause();
    bool resume();
    bool setVideoRectangle( VIDEO_RECT_STRUCT video_rect , bool apply = false );
    int getPlayerstate();
    double getDuration(GstElement *pipeline = nullptr);
    bool seekTo(double seconds,GstElement *pipeline = nullptr);
    double getCurrentPosition(GstElement *pipeline = nullptr);
    bool get_player_statistics();
    void print_pipeline_state(GstElement *pipeline = nullptr);

private:
    GstElement  *m_append_pipeline{nullptr};
    GstElement  *m_udpsrc{nullptr};
    GstElement  *m_rtpjitterbuffer{nullptr};
    GstElement  *m_rtpmp2tdepay{nullptr};
    GstElement  *m_Queue{nullptr};
    GstElement  *m_tsparse{nullptr};
    GstElement  *m_appsink{nullptr};

    GstElement  *m_playbin_pipeline{nullptr};
    GstElement  *m_appsrc;
    GstCaps     *m_capsSrc;

    bool m_firstVideoFrameReceived{false};

    MiracastRTSPMsg *m_rtsp_reference_instance{nullptr};
    MessageQueue* m_customQueueHandle{nullptr};

    std::string m_uri;
    guint64 m_streaming_port;
    int m_bus_watch_id{-1};
    bool m_bBuffering;
    bool m_is_live;
    bool m_bReady;
    int m_buffering_level;
    double m_currentPosition;
    GstElement *m_video_sink{nullptr};
    GstElement *m_audio_sink{nullptr};
    pthread_t m_playback_thread{0};
    VIDEO_RECT_STRUCT m_video_rect_st;

    bool m_pushBufferLoop;
    pthread_t m_pushbuffer_handler_tid{0};
    static void *pushbuffer_handler_thread(void *ctx);

    static MiracastGstPlayer *m_GstPlayer;
    MiracastGstPlayer();
    virtual ~MiracastGstPlayer();
    MiracastGstPlayer &operator=(const MiracastGstPlayer &) = delete;
    MiracastGstPlayer(const MiracastGstPlayer &) = delete;

    bool createPipeline();
    bool updateVideoSinkRectangle(void);
    static void onFirstVideoFrameCallback(GstElement* object, guint arg0, gpointer arg1,gpointer userdata);
    void notifyPlaybackState(eMIRA_GSTPLAYER_STATES gst_player_state, eM_PLAYER_REASON_CODE state_reason_code = MIRACAST_PLAYER_REASON_CODE_SUCCESS );
    bool changePipelineState(GstElement* pipeline, GstState state) const;

    static void *playbackThread(void *ctx);
    GMainLoop *m_main_loop{nullptr};
    GMainContext *m_main_loop_context{nullptr};

    bool m_statistics_thread_loop{false};
    
    pthread_t m_player_statistics_tid{0};
    static void *monitor_player_statistics_thread(void *ctx);

    static GstFlowReturn appendPipelineNewSampleHandler(GstElement *elt, gpointer userdata);
    static gboolean appendPipelineBusMessage(GstBus * bus, GstMessage * message, gpointer userdata);
    static gboolean playbinPipelineBusMessage (GstBus * bus, GstMessage * message, gpointer userdata);
    static void gst_bin_need_data(GstAppSrc *src, guint length, gpointer user_data);
    static void gst_bin_enough_data(GstAppSrc *src, gpointer user_data);
    static void source_setup(GstElement *pipeline, GstElement *source, gpointer userdata);
    static void gstBufferReleaseCallback(void* userParam);
};

#endif /* _MIRACAST_GST_PLAYER_H_ */