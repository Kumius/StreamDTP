#ifndef DECODE_VIDEO_H
#define DECODE_VIDEO_H

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#include <stdio.h>
#include <ev.h>
#include <sys/stat.h>
#include <url_file.h>
#include <sodtp_config.h>
#include <sodtp_jitter.h>
#include <sdl_play.h>
#include <sodtp_decoder.h>


#undef av_err2str
#define av_err2str(errnum) av_make_error_string((char*)__builtin_alloca(AV_ERROR_MAX_STRING_SIZE), AV_ERROR_MAX_STRING_SIZE, errnum)


void SaveFrame(Decoder *decoder) {
    FILE            *pFile;
    AVCodecContext  *ctx    = decoder->pVCodecCtx;
    AVFrame         *pFrame = decoder->pFrameRGB;
    char path[128];

    if (access(decoder->path, 0) == -1)
    {
        if (mkdir(decoder->path, 0775) < 0) {
            fprintf(stderr, "mkdir: fail to create %s\n", decoder->path);
            return;
        }
    }

    sprintf(path, "%s/%d", decoder->path, decoder->iStream);
    if (access(path, 0) == -1)
    {
        if (mkdir(path, 0775) < 0) {
            fprintf(stderr, "mkdir: fail to create %s\n", path);
            return;
        }
    }

    sprintf(path, "%s/frame%d.ppm", path, decoder->iFrame);
    pFile = fopen(path, "wb");
    if(pFile == NULL) {
        return;
    }

    // Write header
    fprintf(pFile, "P6\n%d %d\n255\n", ctx->width, ctx->height);

    // Write pixel data
    for(int y = 0; y < ctx->height; y++)
        fwrite(pFrame->data[0] + y*pFrame->linesize[0], 1, ctx->width*3, pFile);

    // Close file
    fclose(pFile);
}


// Warning! We do not flush avcodec to get the remaining frames.
// So streamer needs to make sure each frame can be decoded directly without
// pushing a empty packet into codec.
void DecodePacket(Decoder *decoder)
{
    AVCodecContext      *pVCodecCtx = decoder->pVCodecCtx;
    struct SwsContext   *pSwsCtx    = decoder->pSwsCtx;
    AVFrame             *pFrame     = decoder->pFrame;
    AVFrame             *pFrameRGB  = decoder->pFrameRGB;
    AVFrame             *pFrameYUV  = decoder->pFrameYUV;
    AVFrame             *pFrameRGB  = decoder->pFrameRGB;
    AVPacket            *pPacket    = decoder->pPacket;
    int32_t             iStream     = decoder->iStream;
    int32_t             iFrame      = decoder->iFrame;

    int ret = avcodec_send_packet(pVCodecCtx, pPacket);
    if (ret < 0) {
        fprintf(stderr, "Error sending a packet for decoding\n");
        exit(1);
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(pVCodecCtx, pFrame);

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // fprintf(stderr, "debug: %s:%s:%d\n", __FILE__, __FUNCTION__, __LINE__);
            // printf("decoding once over : stream %d,\t frame %d\n", iStream, iFrame);
            return;
        }
        else if (ret < 0) {
            fprintf(stderr, "Error during decoding\n");
            exit(1);
        }

        printf("saving: stream %d,\t frame %d,\t codec frame %d\n",
            iStream, iFrame, pVCodecCtx->frame_number);
        fflush(stdout);


        if (decoder->bPlay) {
            // Convert the image from its native format to YUV
            sws_scale(pSwsCtx, (uint8_t const * const *)pFrame->data,
                    pFrame->linesize, 0, pVCodecCtx->height,
                    pFrameYUV->data, pFrameYUV->linesize);

            scoped_lock lock(decoder->mutex);
            decoder->iStart = 1;
            AVFrame *tmp = decoder->pFrameYUV;
            decoder->pFrameYUV = decoder->pFrameShow;
            decoder->pFrameShow = tmp;
        }

        if (decoder->bSave && decoder->path) {
            // Convert the image from its native format to RGB
        
            if(!pSwsCtx) {
                printf("pswsctx = NULL\n");
                fflush(stdout);
                exit(-1);
            }

            if(!pFrame->data) {
                printf("pframe data = NULL\n");
                fflush(stdout);
                exit(-1);
            }

            // if(!pFrameYUV->data) {
            //     printf("pframeYUV data = NULL\n");
            //     fflush(stdout);
            //     exit(-1);
            // }

            // if(!pFrameYUV->linesize) {
            //     printf("pframeYUV linesize = NULL\n");
            //     fflush(stdout);
            //     exit(-1);
            // }

            if(!pFrame->linesize) {
                printf("pframe linesize = NULL\n");
                fflush(stdout);
                exit(-1);
            }
            sws_scale(pSwsCtx, (uint8_t const * const *)pFrame->data,
                    pFrame->linesize, 0, pVCodecCtx->height,
                    pFrameRGB->data, pFrameRGB->linesize);
            SaveFrame(decoder);
        }
    }
}


int SodtpReadPacket(
    SodtpJitter             *pJitter,
    AVPacket                *pPacket,
    SodtpBlockPtr           &pBlock) {

    int ret;
    ret = pJitter->pop(pBlock);
    if (!pBlock) {
        pPacket->data = NULL;
        pPacket->size = 0;
        return ret;
    }

    // Warning!!!
    // Block buffer size should be size + AV_INPUT_BUFFER_PADDING_SIZE
    // Warning!!!
    //
    //
    // if (pBlock) {
    if (ret == SodtpJitter::STATE_NORMAL) {
        av_packet_from_data(pPacket, pBlock->data, pBlock->size);
    }
    // else {
    //     fprintf(stderr, "jitter is buffering now.\n");
    // }

    return ret;
}


struct buffer_data {
    uint8_t *ptr;
    size_t size; ///< size left in the buffer
};

int read_buffer(void *opaque, uint8_t *buf, int buf_size)
{
    struct buffer_data *bd = (struct buffer_data *)opaque;
    buf_size = FFMIN(buf_size, bd->size);

    if (!buf_size)
        return AVERROR_EOF;
    fprintf(stderr, "ptr:%p size:%zu\n", bd->ptr, bd->size);

    /* copy internal buffer data to buf */
    memcpy(buf, bd->ptr, buf_size);
    bd->ptr  += buf_size;
    bd->size -= buf_size;

    return buf_size;
}

AVFormatContext* sniff_format(uint8_t *data, size_t size) {
    AVFormatContext *fmt_ctx = NULL;
    AVIOContext *avio_ctx = NULL;
    uint8_t *avio_ctx_buffer = NULL;
    size_t avio_ctx_buffer_size = 2000000;
    char *input_filename = NULL;
    int ret = 0;
    struct buffer_data bd = { 0 };


    /* fill opaque structure used by the AVIOContext read callback */
    bd.ptr  = data;
    bd.size = size;

    if (!(fmt_ctx = avformat_alloc_context())) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    avio_ctx_buffer = (uint8_t *)av_malloc(avio_ctx_buffer_size);
    if (!avio_ctx_buffer) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    avio_ctx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size,
                                  0, &bd, &read_buffer, NULL, NULL);
    if (!avio_ctx) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    fmt_ctx->pb = avio_ctx;

    ret = avformat_open_input(&fmt_ctx, NULL, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open input\n");
        goto end;
    }

    ret = avformat_find_stream_info(fmt_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not find stream information\n");
        goto end;
    }

    av_dump_format(fmt_ctx, 0, input_filename, 0);

end:
    // avformat_close_input(&fmt_ctx);

    /* note: the internal buffer could have changed, and be != avio_ctx_buffer */
    // if (avio_ctx)
    //     av_freep(&avio_ctx->buffer);
    // avio_context_free(&avio_ctx);

    if (ret < 0) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return NULL;
    }

    return fmt_ctx;
}


void worker_cb(EV_P_ ev_timer *w, int revents) {
    // AVPacket packet;
    Decoder *decoder = (Decoder *)w->data;

    int ret = SodtpReadPacket(decoder->pJitter, decoder->pPacket, decoder->pBlock);


    if (decoder->pJitter->state == SodtpJitter::STATE_CLOSE) {
        // Stream is closed.
        // Thread will be closed by breaking event loop.
        ev_timer_stop(loop, w);
        fprintf(stderr, "Stream %d is closed!\n", decoder->iStream);
        return;
    }

    if (ret == SodtpJitter::STATE_NORMAL) {
        // Receive one more block.
        decoder->iBlock++;
        // printf("decoding: stream %d,\t block %d,\t size %d,\t received block count %d\n",
        //     decoder->pBlock->stream_id, decoder->pBlock->block_id,
        //     decoder->pPacket->size, decoder->iBlock);
        printf("decoding: stream %d,\t block %d,\t size %d,\t delay %d\n",
            decoder->pBlock->stream_id, decoder->pBlock->block_id,
            decoder->pPacket->size, (int)(current_mtime() - decoder->pBlock->block_ts));

        decoder->iFrame = decoder->pBlock->block_id + 1;
        DecodePacket(decoder);
    }
    else if (ret == SodtpJitter::STATE_BUFFERING) {
        printf("decoding: buffering stream %d\n", decoder->iStream);
    }
    else if (ret == SodtpJitter::STATE_SKIP) {
        printf("decoding: skip one block of stream %d\n", decoder->iStream);
    }
    else {
        printf("decoding: warning! unknown state of stream %d!\n", decoder->iStream);
    }
    // Free the packet that was allocated by av_read_frame
    // av_free_packet(&packet);
}


// decode and save the pictures.
int video_viewer3(SodtpJitterPtr pJitter, const char *path) {

    printf("viewer: viewing stream %u!\n", pJitter->stream_id);

    // Initalizing these to NULL prevents segfaults!
    AVFormatContext         *pVFormatCtx = NULL;
    AVCodecContext          *pVCodecCtx = NULL;
    const AVCodec           *pVCodec = NULL;
    AVFrame                 *pFrame = NULL;
    AVFrame                 *pFrameRGB = NULL;
    AVPacket                packet;
    int                     iFrame;
    int                     ret;
    int                     numBytes;
    uint8_t                 *buffer = NULL;
    struct SwsContext       *pSwsCtx = NULL;

    SodtpBlockPtr           pBlock = NULL;

    // Register all formats and codecs
    // av_register_all();

    av_init_packet(&packet);

    pVCodecCtx = avcodec_alloc_context3(NULL);
    if (!pVCodecCtx) {
        fprintf(stderr, "Could not allocate video codec context\n");
        return -1;
    }
    pVCodecCtx->thread_count = 1;

    int i = 0;
    int WAITING_UTIME = 20000;
    int WAITING_ROUND = 500;
    int SKIPPING_ROUND = 70;

    while (true) {
        ret = pJitter->front(pBlock);

        if (ret == SodtpJitter::STATE_NORMAL && pBlock->key_block) {
            fprintf(stdout, "sniffing: stream %d,\t block %d,\t size %d\n",
                    pBlock->stream_id, pBlock->block_id, pBlock->size);
            pVFormatCtx = sniff_format(pBlock->data, pBlock->size);
            break;
        }
        fprintf(stderr, "decoding: waiting for the key block of stream %u. sleep round %d!\n", pJitter->stream_id, i);

        if (++i > WAITING_ROUND) {
            fprintf(stderr, "decoding: fail to read the key block of stream %u.\n", pJitter->stream_id);
            break;
        }
        usleep(WAITING_UTIME);
    }

    if (!pVFormatCtx) {
        fprintf(stderr, "viewer: quit stream %u.\n", pJitter->stream_id);
        return -1;
    }
    avcodec_parameters_to_context(pVCodecCtx, pVFormatCtx->streams[0]->codecpar);

    pVCodec = avcodec_find_decoder(pVCodecCtx->codec_id);
    if (!pVCodec) {
        fprintf(stderr, "Codec not found\n");
        return -1;
    }

    // Open Codec
    if (avcodec_open2(pVCodecCtx, pVCodec, NULL) < 0) {
        fprintf(stderr, "Fail to open codec!\n");
        return -1;
    }

    // Allocate video frame
    pFrame = av_frame_alloc();
    // Allocate an AVFrame structure
    pFrameRGB = av_frame_alloc();
    if (pFrame == NULL || pFrameRGB == NULL)
        return -1;

    // Determine required buffer size and allocate buffer
    numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, pVCodecCtx->width, pVCodecCtx->height, 1);
    buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));

    // Assign appropriate parts of buffer to image planes in pFrameRGB
    av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, buffer,
        AV_PIX_FMT_RGB24, pVCodecCtx->width, pVCodecCtx->height, 1);

    // initialize SWS context for software scaling
    pSwsCtx = sws_getContext(pVCodecCtx->width,
                            pVCodecCtx->height,
                            pVCodecCtx->pix_fmt,
                            pVCodecCtx->width,
                            pVCodecCtx->height,
                            AV_PIX_FMT_RGB24,
                            SWS_BILINEAR,
                            NULL,
                            NULL,
                            NULL
                            );

    // SaveConfig scon;
    // if (!path) {
    //     scon.parse("./config/save.conf");
    //     path = scon.path.c_str();
    // }

    Decoder decoder;
    decoder.pVCodecCtx  = pVCodecCtx;
    decoder.pSwsCtx     = pSwsCtx;
    decoder.pFrame      = pFrame;
    decoder.pFrameRGB   = pFrameRGB;
    decoder.pPacket     = &packet;
    decoder.pJitter     = pJitter.get();
    decoder.pBlock      = NULL;
    decoder.iStream     = pJitter->stream_id;
    decoder.iFrame      = 0;
    decoder.iBlock      = 0;
    decoder.bPlay       = false;
    decoder.bSave       = true;
    decoder.path        = path;

    ev_timer worker;
    struct ev_loop *loop = ev_loop_new(EVFLAG_AUTO);

    double nominal = (double)pJitter->get_nominal_depth() / 1000.0;
    double interval = 0.040;    // 40ms, i.e. 25fps

    ev_timer_init(&worker, worker_cb, nominal, interval);
    ev_timer_start(loop, &worker);
    worker.data = &decoder;

    ev_loop(loop, 0);


    // Free the RGB image
    av_free(buffer);
    av_frame_free(&pFrameRGB);

    // Free the YUV frame
    av_frame_free(&pFrame);

    // Close the codecs
    avcodec_close(pVCodecCtx);

    // Close the video file
    avformat_close_input(&pVFormatCtx);

    // Notification for clearing the jitter.
    sem_post(pJitter->_sem);
    ev_feed_signal(SIGUSR1);

    return 0;
}


// decode and display the pictures.
int video_viewer4(SodtpJitterPtr pJitter, SDLPlay *splay, const char *path) {

    printf("viewer: viewing stream %u!\n", pJitter->stream_id);

    // Initalizing these to NULL prevents segfaults!
    AVFormatContext         *pVFormatCtx = NULL;
    AVCodecContext          *pVCodecCtx = NULL;
    const AVCodec           *pVCodec = NULL;
    AVFrame                 *pFrame = NULL;
    AVFrame                 *pFrameYUV = NULL;
    AVFrame                 *pFrameShow = NULL;
    AVPacket                packet;
    int                     iFrame;
    int                     ret;
    int                     numBytes;
    uint8_t                 *buffer = NULL;
    struct SwsContext       *pSwsCtx = NULL;

    SodtpBlockPtr           pBlock = NULL;
    SDL_Texture             *pTexture = NULL;

    // Register all formats and codecs
    // av_register_all();

    av_init_packet(&packet);

    pVCodecCtx = avcodec_alloc_context3(NULL);
    if (!pVCodecCtx) {
        fprintf(stderr, "Could not allocate video codec context\n");
        return -1;
    }
    pVCodecCtx->thread_count = 1;

    int i = 0;
    int WAITING_UTIME = 20000;
    int WAITING_ROUND = 500;
    int SKIPPING_ROUND = 70;

    while (true) {
        ret = pJitter->front(pBlock);

        if (ret == SodtpJitter::STATE_NORMAL && pBlock->key_block) {
            fprintf(stdout, "sniffing: stream %d,\t block %d,\t size %d\n",
                    pBlock->stream_id, pBlock->block_id, pBlock->size);
            pVFormatCtx = sniff_format(pBlock->data, pBlock->size);
            break;
        }
        fprintf(stderr, "decoding: waiting for the key block of stream %u. sleep round %d!\n", pJitter->stream_id, i);

        if (++i > WAITING_ROUND) {
            fprintf(stderr, "decoding: fail to read the key block of stream %u.\n", pJitter->stream_id);
            break;
        }
        usleep(WAITING_UTIME);
    }

    if (!pVFormatCtx) {
        fprintf(stderr, "viewer: quit stream %u.\n", pJitter->stream_id);
        return -1;
    }
    avcodec_parameters_to_context(pVCodecCtx, pVFormatCtx->streams[0]->codecpar);

    pVCodec = avcodec_find_decoder(pVCodecCtx->codec_id);
    if (!pVCodec) {
        fprintf(stderr, "Codec not found\n");
        return -1;
    }

    // Open Codec
    if (avcodec_open2(pVCodecCtx, pVCodec, NULL) < 0) {
        fprintf(stderr, "Fail to open codec!\n");
        return -1;
    }

    // Allocate video frame
    pFrame = av_frame_alloc();
    // Allocate an AVFrame structure
    pFrameYUV = av_frame_alloc();
    pFrameShow = av_frame_alloc();
    if (pFrame == NULL || pFrameYUV == NULL || pFrameShow == NULL) {
        printf("fail to allocate frame!\n");
        return -1;
    }

    // Determine required buffer size and allocate buffer
    numBytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pVCodecCtx->width, pVCodecCtx->height, 1);
    buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));

    // Assign appropriate parts of buffer to image planes in pFrameYUV
    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, buffer,
        AV_PIX_FMT_YUV420P, pVCodecCtx->width, pVCodecCtx->height, 1);
    av_image_fill_arrays(pFrameShow->data, pFrameShow->linesize, buffer,
        AV_PIX_FMT_YUV420P, pVCodecCtx->width, pVCodecCtx->height, 1);

    // initialize SWS context for software scaling
    pSwsCtx = sws_getContext(pVCodecCtx->width,
                            pVCodecCtx->height,
                            pVCodecCtx->pix_fmt,
                            pVCodecCtx->width,
                            pVCodecCtx->height,
                            AV_PIX_FMT_YUV420P,
                            SWS_BILINEAR,
                            NULL,
                            NULL,
                            NULL
                            );

    pTexture = SDL_CreateTexture(splay->renderer,
                            SDL_PIXELFORMAT_IYUV,
                            SDL_TEXTUREACCESS_STREAMING,
                            pVCodecCtx->width,
                            pVCodecCtx->height
                            );
    // SaveConfig scon;
    // if (!path) {
    //     scon.parse("./config/save.conf");
    //     path = scon.path.c_str();
    // }

    Decoder &decoder    = pJitter->decoder;
    decoder.pVCodecCtx  = pVCodecCtx;
    decoder.pSwsCtx     = pSwsCtx;
    decoder.pFrame      = pFrame;
    decoder.pFrameYUV   = pFrameYUV;
    decoder.pFrameRGB   = NULL;
    decoder.pFrameShow  = pFrameShow;
    decoder.pPacket     = &packet;
    decoder.pJitter     = pJitter.get();
    decoder.pBlock      = NULL;
    decoder.iStream     = pJitter->stream_id;
    decoder.iFrame      = 0;
    decoder.iBlock      = 0;
    decoder.iStart      = 0;
    decoder.bPlay       = true;
    decoder.bSave       = false;
    // Set the path to be NULL, which means we do not
    // save picture in SDL display mode.
    decoder.path        = NULL;
    decoder.pTexture    = pTexture;


    ev_timer worker;
    struct ev_loop *loop = ev_loop_new(EVFLAG_AUTO);

    double nominal = (double)pJitter->get_nominal_depth() / 1000.0;
    double interval = 0.040;    // 40ms, i.e. 25fps

    ev_timer_init(&worker, worker_cb, nominal, interval);
    ev_timer_start(loop, &worker);
    worker.data = &decoder;

    ev_loop(loop, 0);


    // Free the YUV image
    av_free(buffer);
    av_frame_free(&pFrameYUV);
    av_frame_free(&pFrameShow);

    // Free the original frame
    av_frame_free(&pFrame);

    // Close the codecs
    avcodec_close(pVCodecCtx);

    // Close the video file
    avformat_close_input(&pVFormatCtx);

    // Notification for clearing the jitter.
    sem_post(pJitter->_sem);
    ev_feed_signal(SIGUSR1);

    return 0;
}

#endif // DECODE_VIDEO_H