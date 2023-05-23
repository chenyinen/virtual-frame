#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <semaphore.h>
#include <stdbool.h>

#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>

#include <queue.h>
#include "emu_coder.h"

/* 解码配置 */
struct emu_decode_config {
    char decode_file[64];
    enum AVMediaType decode_type;
};

/* 编解码任务 */
struct emu_task {
    bool cancel;
    queue_t *encode_queue; 
    queue_t *frame_queue; 
    struct emu_encode_config config;
    struct emu_decode_config decode_config;
    sem_t exit_sem;

    AVCodecContext *encode_ctx; ///> 编码上下文
    AVFormatContext *decode_format;  ///> 解码文件信息
    AVCodecContext *decode_ctx; ///> 解码上下文
    int decode_stream;
};
/** 
 * @brief 设置解码上下文
 * @note 
 */
static int set_decode_context(struct emu_task *task)
{
    int ret;
    int video_stream_index = -1;
    unsigned int i;
    struct emu_decode_config *config = &task->decode_config;
    /* 打开文件 */
    AVFormatContext *format_ctx = NULL;
    if ((ret = avformat_open_input(&format_ctx, config->decode_file, NULL, NULL)) != 0) {
        printf("avformat_open_input %s fail[%s]\n", config->decode_file,  av_err2str(ret));
        return -10;
    }
    /* 寻找流 */
    if (avformat_find_stream_info(format_ctx, NULL) < 0) {
        printf("avformat_find_stream_info fail\n");
        avformat_close_input(&format_ctx);
        return -1;
    }
    for (i = 0; i < format_ctx->nb_streams; i++) {
        /* 指定视频流 */
        if (format_ctx->streams[i]->codecpar->codec_type == config->decode_type) {
            video_stream_index = (int)i;
            break;
        }
    }
    if (video_stream_index == -1) {
        printf("can't found video stream\n");
        avformat_close_input(&format_ctx);
        return -1;
    }
    /* 寻找解码器 */
    AVCodecParameters *codec_params = format_ctx->streams[video_stream_index]->codecpar;
    AVCodec *codec = avcodec_find_decoder(codec_params->codec_id);
    if (codec == NULL) {
        printf("can't found decoder\n");
        avformat_close_input(&format_ctx);
        return -1;
    }
    /* 将解码器参数转换为解码器上下文 */
    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    if (avcodec_parameters_to_context(codec_ctx, codec_params) != 0) {
        printf("avcodec_alloc_context3 fail\n");
        avformat_close_input(&format_ctx);
        return -1;
    }
    /* 打开解码器 */
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        avformat_close_input(&format_ctx);
        avcodec_free_context(&codec_ctx);
        return -1;
    }
    task->decode_ctx = codec_ctx;
    task->decode_format = format_ctx;
    task->decode_stream = video_stream_index;
    return 0;
}
/** 
 * @brief 设置编码上下文
 * @note 
 */
static int set_encode_context(struct emu_task *task)
{
    int ret;
    struct emu_encode_config *config = &task->config;
    /* 获取编码器 */
    AVCodec *codec = avcodec_find_encoder_by_name(config->coder);
    if (!codec) {
        fprintf(stderr, "Could not find %s encoder\n", config->coder);
        return -1;
    }
    /* 获取编码器上下文 */
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        fprintf(stderr, "Could not allocate encoding context\n");
        return -1;
    }
    ctx->width = config->width;
    ctx->height = config->height;
    ctx->time_base = (AVRational){1, config->frame_rate};
    ctx->framerate = (AVRational){config->frame_rate, 1};
    ctx->gop_size = config->gop_size;
    ctx->bit_rate = config->bit_rate;
    if (codec->id == AV_CODEC_ID_MJPEG) {
        ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
    }
    else {
        ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    }
    if (codec->id == AV_CODEC_ID_H265) {
        av_opt_set(ctx->priv_data, "preset", "ultrafast", 0);
        av_opt_set(ctx->priv_data, "tune", "zerolatency", 0);
        av_opt_set(ctx->priv_data, "x265-params", "--keyint=25:--radl=0:--no-open-gop=1:--info=0", 0);
    }
    ret = avcodec_open2(ctx, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open encoder: %s\n", av_err2str(ret));
        avcodec_free_context(&ctx);
        return -1;
    }
    task->encode_ctx = ctx;
    return 0;
}
/**
 * @brief 创建默认编码数据
*/
static AVFrame * create_frame_default()
{
    AVFrame *frame = av_frame_alloc();
    int x,y;

    frame->width = 1920;
    frame->height = 1080;
    frame->format = AV_PIX_FMT_YUV420P;
    av_frame_get_buffer(frame, 0);
    av_frame_make_writable(frame);
    static int i = 0;
    /* Y */
    for ( y = 0; y < frame->height; y++) {
        for ( x = 0; x < frame->width; x++) {
            frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
        }
    }
    /* Cb and Cr */
    for ( y = 0; y < frame->height / 2; y++) {
        for ( x = 0; x < frame->width / 2; x++) {
            frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
            frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
        }
    }
    frame->pts = i++;
    return frame;
}
/** 
 * @brief 制作一个视频帧
 * @note 
 */
static int make_frame(struct emu_task *task, AVFrame *encode_frame)
{
    static int i=0;
    int ret;
    AVPacket *pkt;

    ret = av_frame_make_writable(encode_frame);
    encode_frame->pts = i++;
    ret = avcodec_send_frame(task->encode_ctx, encode_frame);
    while (ret >= 0) {
        pkt = av_packet_alloc();
        if (!pkt) {
            return -1;
        }
        ret = avcodec_receive_packet(task->encode_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return 0;
        else if (ret < 0) {
            fprintf(stderr, "Error during encoding\n");
            return -1;
        }
        //放入帧队列供提取
        queue_put_wait(task->frame_queue, (void*)pkt);
    }
    return 0;
}
/** 
 * @brief 开始解码任务
 * @note 
 */
static void *decode_proc(void *p)
{
    int ret;
    AVPacket packet;
    AVFrame *decode_frame;

    pthread_setname_np(pthread_self(), "x86 decoder");
    struct emu_task *task = (struct emu_task *) p;
    av_init_packet(&packet);
    packet.data = NULL;
    packet.size = 0;
    while(!task->cancel) {
        if (task->decode_format) {
            while (av_read_frame(task->decode_format, &packet) >= 0) {
                if (packet.stream_index == task->decode_config.decode_type) {
                    // 解码视频帧
                    decode_frame = av_frame_alloc();
                    ret = avcodec_send_packet(task->decode_ctx, &packet);
                    if (ret == 0) {
                        ret = avcodec_receive_frame(task->decode_ctx, decode_frame);
                        if (ret == 0) {
                            /* 将视频帧放入编码队列 */
                            queue_put_wait(task->encode_queue, (void*)decode_frame);
                        }
                    }
                }
                av_packet_unref(&packet);
                if (task->cancel) {
                    break;
                }
            }
            /* 返回起始位置 */
            ret = av_seek_frame(task->decode_format, task->decode_stream, 0, AVSEEK_FLAG_ANY);
            if (ret < 0) {
                printf("av_seek_frame error\n");
            }
        }
        else {
            decode_frame = create_frame_default();
            /* 将默认帧放入编码队列 */
            queue_put_wait(task->encode_queue, (void*)decode_frame);
        }
    }
    /* 任务销毁 */
    if (task->decode_format) {
        avformat_close_input(&task->decode_format);
    }
    if (task->decode_ctx) {
        avcodec_free_context(&task->decode_ctx);
    }
    sem_post(&task->exit_sem);
    printf("decode task exit\n");
    return NULL;
}
/** 
 * @brief 执行编码任务
 * @note 
 */
static void *encode_proc(void *p)
{
    int ret;
    AVFrame *origin_frame;
    struct SwsContext *sws_ctx;
    struct emu_task *task = (struct emu_task*)p;
    pthread_setname_np(pthread_self(), "x86 encoder");

    AVFrame *encode_frame = av_frame_alloc();
    if (!encode_frame) {
        return NULL;
    }
    encode_frame->format =  task->encode_ctx->pix_fmt; 
    encode_frame->width  = task->encode_ctx->width; 
    encode_frame->height = task->encode_ctx->height;
    ret = av_frame_get_buffer(encode_frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate the video frame data\n");
        av_frame_free(&encode_frame);
        return NULL;
    }
    while(!task->cancel) {
        /* 获取编码数据源 */
        queue_get_wait(task->encode_queue, (void**)&origin_frame);
        /* 格式转换 */
        sws_ctx = sws_getContext(origin_frame->width, origin_frame->height, origin_frame->format,
                                    encode_frame->width, encode_frame->height, encode_frame->format,
                                    SWS_BILINEAR, NULL, NULL, NULL);
        sws_scale(sws_ctx, (const unsigned char * const*)origin_frame->data, origin_frame->linesize,
                    0, origin_frame->height, (unsigned char * const*)encode_frame->data, encode_frame->linesize);
        sws_freeContext(sws_ctx);
        av_frame_free(&origin_frame);
        /* 制作一个视频帧 */
        make_frame(task, encode_frame);
    }
    avcodec_free_context(&task->encode_ctx);
    sem_post(&task->exit_sem);
    printf("encode task exit\n");
    return NULL;
}
long emu_init(char *file, struct emu_encode_config *encode_config)
{
    int ret;
    pthread_t pthread_id;

    if (!encode_config) {
        return -1;
    }
    avcodec_register_all();
    
    struct emu_task *task = calloc(1, sizeof(*task));

    memcpy(&task->config, encode_config, sizeof(*encode_config));
    if (file) {
        snprintf(task->decode_config.decode_file, sizeof(task->decode_config.decode_file)-1, "%s", file);
    }

    task->decode_config.decode_type = AVMEDIA_TYPE_VIDEO;

    ret = set_encode_context(task);
    if (ret < 0) {
        printf("set_encode_context fail\n");
        return -2;
    }
    if (file) {
        ret = set_decode_context(task);
        if (ret < 0 && ret != -10) {
            printf("set_decode_context fail\n");
            return -3;
        }
    }
    task->encode_queue = queue_create_limited(1);
    task->frame_queue = queue_create_limited(1);

    sem_init(&task->exit_sem, 0, 0);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&pthread_id, &attr, decode_proc, task);
    pthread_create(&pthread_id, &attr, encode_proc, task);

    return (long)task;
}
void emu_destroy_task(long emu_handle)
{
    struct emu_task *task = (struct emu_task *)emu_handle;
    AVPacket *pkt = NULL;
    AVFrame *frame;
    task->cancel = true;

    while(Q_OK == queue_get(task->frame_queue, (void**)&pkt)) {
        av_packet_free(&pkt);
    }
    while(Q_OK == queue_get(task->encode_queue, (void**)&frame)) {
        av_frame_free(&frame);
    }
    sem_wait(&task->exit_sem);
    sem_wait(&task->exit_sem);
    /* 防止数据残留 */
    while(Q_OK == queue_get(task->frame_queue, (void**)&pkt)) {
        av_packet_free(&pkt);
    }
    while(Q_OK == queue_get(task->encode_queue, (void**)&frame)) {
        av_frame_free(&frame);
    }
    return ;
}
void emu_destroy_frame(unsigned char **data)
{
    AVPacket *pkt;

    pkt = CONTAINER_OF(data, AVPacket, data);
    av_packet_free(&pkt);
    return ;
}
int emu_get_frame_data(long emu_handle, unsigned char***data, int *len)
{
    struct emu_task *task = (struct emu_task *)emu_handle;
    AVPacket *pkt = NULL;

    if (!data || !len) {
        return -1;
    }

    queue_get_wait(task->frame_queue, (void**)&pkt);

    *data = &pkt->data;
    *len = pkt->size;
    return 0;
}
