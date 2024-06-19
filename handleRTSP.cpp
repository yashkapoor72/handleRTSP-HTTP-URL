#include <iostream>
#include <opencv2/opencv.hpp>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

int main() {
    avformat_network_init(); // Initialize network components

    std::string rtsp_url = "rtsp://your_rtsp_here";
    AVFormatContext *format_ctx = nullptr;
    AVDictionary *format_opts = nullptr;

    // Set options based on ffmpeg command
    av_dict_set(&format_opts, "rtsp_transport", "tcp", 0); // Use TCP transport
    av_dict_set(&format_opts, "stimeout", "10000000", 0); // Set timeout (in microseconds)
    av_dict_set(&format_opts, "vsync", "2", 0); // Vsync method (passthrough)
    av_dict_set(&format_opts, "err_detect", "careful", 0); // Error detection level
    av_dict_set(&format_opts, "skip_frame", "nokey", 0); // Skip non-key frames

    // Open RTSP stream
    if (avformat_open_input(&format_ctx, rtsp_url.c_str(), nullptr, &format_opts) != 0) {
        std::cerr << "Could not open input stream." << std::endl;
        return -1;
    }

    // Retrieve stream information
    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream information." << std::endl;
        avformat_close_input(&format_ctx);
        return -1;
    }

    av_dump_format(format_ctx, 0, rtsp_url.c_str(), 0);

    int video_stream_index = -1;
    AVCodec *codec = nullptr;
    AVCodecParameters *codec_params = nullptr;
    AVCodecContext *codec_ctx = nullptr;

    // Find video stream and codec
    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            codec_params = format_ctx->streams[i]->codecpar;
            codec = avcodec_find_decoder(codec_params->codec_id);
            break;
        }
    }

    if (video_stream_index == -1 || !codec || !codec_params) {
        std::cerr << "Could not find a video stream or codec." << std::endl;
        avformat_close_input(&format_ctx);
        return -1;
    }

    // Allocate codec context
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        std::cerr << "Failed to allocate codec context." << std::endl;
        avformat_close_input(&format_ctx);
        return -1;
    }

    // Copy codec parameters to codec context
    if (avcodec_parameters_to_context(codec_ctx, codec_params) < 0) {
        std::cerr << "Could not copy codec parameters." << std::endl;
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return -1;
    }

    // Open codec
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        std::cerr << "Could not open codec." << std::endl;
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return -1;
    }

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    // Prepare for color conversion
    SwsContext *sws_ctx = sws_getContext(codec_ctx->width, codec_ctx->height,
                                         codec_ctx->pix_fmt,
                                         codec_ctx->width, codec_ctx->height,
                                         AV_PIX_FMT_BGR24,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!sws_ctx) {
        std::cerr << "Failed to allocate SwsContext." << std::endl;
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return -1;
    }

    int frame_count = 0;
    while (av_read_frame(format_ctx, packet) >= 0) {
        if (packet->stream_index == video_stream_index) {
            if (avcodec_send_packet(codec_ctx, packet) >= 0) {
                while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
                    frame_count++;

                    // Convert frame to OpenCV Mat for visualization or processing
                    cv::Mat img(codec_ctx->height, codec_ctx->width, CV_8UC3);
                    size_t dest_linesize = img.step; // size_t is preferred for step

                    uint8_t *dest[1] = {img.data};
                    int dest_linesize_int[1] = {static_cast<int>(dest_linesize)};

                    sws_scale(sws_ctx, frame->data, frame->linesize,
                              0, codec_ctx->height, dest, dest_linesize_int);

                    // Save frame as JPEG
                    cv::imwrite("frame.jpg", img);

                    std::cout << "Frame " << frame_count << " saved." << std::endl;
                }
            }
        }
        av_packet_unref(packet);
    }

    // Cleanup
    sws_freeContext(sws_ctx);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&format_ctx);
    avformat_network_deinit();

    return 0;
}