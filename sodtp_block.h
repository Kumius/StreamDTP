#ifndef SODTP_BLOCK_H
#define SODTP_BLOCK_H

#include <list>
#include <memory>

#include <sodtp_util.h>

extern "C"
{
#include <libavcodec/avcodec.h>
}

// Max block data size is 2M bytes
#define MAX_BLOCK_DATA_SIZE 2000000

#define HEADER_FLAG_NULL    0x0000
#define HEADER_FLAG_META    0x0001
#define HEADER_FLAG_KEY     0x0002
#define HEADER_FLAG_FIN     0x0010


typedef struct SodtpStreamHeader {
    uint32_t    flag;
    uint32_t    stream_id;
    int64_t     block_ts;
    //
    uint32_t    block_id;
    //
    int32_t     duration;       // frame duration (ms)

    // int32_t     group_id;       // ID of the streaming group.
    // int32_t     deadline;       // deadline of this block
    
    // Currently we choose to sniff the format of the network flow.
    // While, you can just transfer the parameters of the codec to the client.
    // These parameters could be found in `avcodec_parameters_to_context()`.
    // In this way, you do not need to sniff the I-frames, which oftentimes fails.
    // Once the client receives the parameters, it can initialize its codec context and decode the stream.
    // CodecPars    *par;
} SodtpStreamHeader;


typedef struct SodtpMetaData
{
    int32_t     width;
    int32_t     height;
} SodtpMetaData;


class SodtpBlock {
public:
    bool        last_one;       // whether the last block in its stream.
    bool        key_block;      // whether the key block in its stream.

    uint32_t    block_id;
    uint32_t    stream_id;

    uint8_t     *data;
    int32_t     size;

    // Timestamp of the block.
    // Its value should be orignally set by data producer (e.g. sender).
    // block_ts will be used in checking whether a block should be dropped.
    // Warning: this needs the time synchronization.
    int64_t     block_ts;       // block timestamp at the sender side.
    int32_t     duration;       // frame duration (ms)

    ~SodtpBlock() {
        if (data) {
            delete[] data;
        }
    }
};

typedef std::shared_ptr<SodtpBlock>  SodtpBlockPtr;


class BlockData
{
public:
    uint8_t     *offset;
    uint8_t     data[MAX_BLOCK_DATA_SIZE];

    uint32_t    id;             // a temp id for collecting complete data.
    uint64_t    expire_ts;

    BlockData(uint32_t id) {
        // expire_ts = current_time() + 200; // deadline 
        offset = data;
        this->id = id;
    }

    int write(uint8_t *src, int size) {
        int ret = size;

        if ((offset - data) + size > MAX_BLOCK_DATA_SIZE) {
            ret = 0;
            fprintf(stderr, "Block data overflows!\n");
        }
        else {
            memcpy(offset, src, size);
            offset += size;
        }

        return ret;
    }
};
typedef std::shared_ptr<BlockData> BlockDataPtr;


class BlockDataBuffer {
public:
    static const int32_t MAX_BLOCK_NUM = 40;

    std::list<BlockDataPtr> buffer;


    // Curently, we use MAX_BLOCK_NUM to pop stale data of buffer.
    // But we should better use expiration time-out as the signal.
    int write(uint32_t id, uint8_t *src, int size);


    // Read after the block is completed.
    SodtpBlockPtr read(uint32_t id, SodtpStreamHeader *head);
};



#endif // SODTP_BLOCK_H