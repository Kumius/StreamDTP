#ifndef URL_FILE_H
#define URL_FILE_H


extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <memory>
#include <sodtp_config.h>


class StreamContext {
public:
    AVFormatContext *pFmtCtx;
    uint32_t stream_id;
    uint32_t flag_meta;

    StreamContext(AVFormatContext *ptr, int id) {
        pFmtCtx = ptr;
        stream_id = id;
        flag_meta = false;
    }

    ~StreamContext() {
        if (pFmtCtx) {
            avformat_close_input(&pFmtCtx);
        }
    }
};

typedef std::shared_ptr<StreamContext> StreamCtxPtr;


int init_AVFormatContext(
    AVFormatContext         **pFormatCtx,
    const char              *pFilename) {

    // Open video file
    if (avformat_open_input(pFormatCtx, pFilename, NULL, NULL) != 0)
        return -1; // Couldn't open file

    // Retrieve stream information
    if (avformat_find_stream_info(*pFormatCtx, NULL) < 0)
        return -1; // Couldn't find stream information

    // Dump information about file onto standard error
    av_dump_format(*pFormatCtx, 0, pFilename, 0);

    return 0;
}


static inline int file_read_packet(
    AVFormatContext         *pFormatCtx,
    AVPacket                *pPacket) {

    return av_read_frame(pFormatCtx, pPacket);
}


void init_resource(std::vector<StreamCtxPtr> *pStmCtxPtrs, const char *conf) {
    StreamConfig stc;

    stc.parse(conf);
    printf("stream number: %lu\n", stc.files.size());

    int id = 0;
    StreamCtxPtr cptr = NULL;
    AVFormatContext *ptr = NULL;

    for (auto &file : stc.files) {
        // It is necessary to set ptr = NULL, or else we will fail to open format context.
        ptr = NULL;

        if (init_AVFormatContext(&ptr, file.c_str()) < 0) {
            fprintf(stderr, "Could not init format context by file %s\n", file.c_str());
            if (ptr) {
                avformat_close_input(&ptr);
            }
        }
        else {
            printf("init resource: %s\n", file.c_str());
            cptr = std::make_shared<StreamContext>(ptr, id);
            pStmCtxPtrs->push_back(cptr);

            id++;
        }
    }
}


bool compare_packet(AVPacket *ptr1, AVPacket *ptr2) {
    bool ret = false;
    if (ptr1->size == ptr2->size) {
        ret = (0 == memcmp(ptr1->data, ptr2->data, ptr1->size));
    }

    return ret;
}



#endif  // URL_FILE_H