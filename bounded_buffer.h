#ifndef BOUNDED_BUFFER_H
#define BOUNDED_BUFFER_H

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <url_file.h>
#include <sodtp_block.h>
#include <unistd.h>


#define MAX_BOUNDED_BUFFER_SIZE  3


class StreamPacket {
public:
    SodtpStreamHeader   header;
    AVPacket            packet;

public:
    ~StreamPacket() {
        av_packet_unref(&packet);
    }
};

typedef std::shared_ptr<StreamPacket>  StreamPktPtr;
typedef std::shared_ptr<std::vector<StreamPktPtr>> StreamPktVecPtr;


template <typename T>
class BoundedBuffer {
public:
    BoundedBuffer(const BoundedBuffer& rhs) = delete;

    BoundedBuffer& operator=(const BoundedBuffer& rhs) = delete;

    BoundedBuffer() : begin_(0), end_(0), buffered_(0), circular_buffer_(0) {}

    BoundedBuffer(std::size_t size) : begin_(0), end_(0), buffered_(0), circular_buffer_(size, NULL) {
        //
    }

    void reset(std::size_t size) {
        begin_ = 0;
        end_ = 0;
        buffered_ = 0;
        circular_buffer_.resize(size, NULL);
    }

    // ~BoundedBuffer() {
    //     for (int i = 0; i < circular_buffer_.size(); i++) {
    //         if (circular_buffer_[i]) {
    //             delete circular_buffer_[i];
    //         }
    //     }
    //     circular_buffer_.clear();
    // }

    // write data into the buffer, and return the stale pointer.
    T produce(T val) {
        std::unique_lock<std::mutex> lock(mutex_);
        // wait for a spare place in the buffer.
        not_full_cv_.wait(lock, [=] { return buffered_ < circular_buffer_.size(); });

        // insert a new element, and update the index.
        auto ret = circular_buffer_[end_];
        circular_buffer_[end_] = val;
        end_ = (end_ + 1) % circular_buffer_.size();

        ++buffered_;

        // unlock manually.
        lock.unlock();
        // notify consumer.
        not_empty_cv_.notify_one();
        return ret;
    }

    T consume() {
        std::unique_lock<std::mutex> lock(mutex_);
        // waiting for an non-empty buffer.
        not_empty_cv_.wait(lock, [=] { return buffered_ > 0; });

        // return one element.
        auto ret = circular_buffer_[begin_];
        begin_ = (begin_ + 1) % circular_buffer_.size();

        --buffered_;

        // unlock manually.
        lock.unlock();
        // notify producer.
        not_full_cv_.notify_one();
        return ret;
    }

private:
    std::size_t begin_;
    std::size_t end_;
    std::size_t buffered_;
    std::vector<T> circular_buffer_;
    std::condition_variable not_full_cv_;
    std::condition_variable not_empty_cv_;
    std::mutex mutex_;
};


// read packets from files, and flush them into the buffer.
void produce(BoundedBuffer<StreamPktVecPtr> *pBuffer, const char *conf) {
    StreamPktVecPtr pStmPktVec = NULL;
    std::vector<StreamCtxPtr> vStmCtx;

    int i = 0;      // packet index
    int ret = 0;
    int stream_num = 0;
    int block_id = 0;

    init_resource(&vStmCtx, conf);

    while (true) {
        stream_num = vStmCtx.size();
        fprintf(stderr, "stream num %d\n", stream_num);
        if (!pStmPktVec) {
            pStmPktVec = std::make_shared<std::vector<StreamPktPtr>>(stream_num);
            for (auto &item : *pStmPktVec) {
                item = std::make_shared<StreamPacket>();
            }
        }
        // (*ptr) already exists, then we can just resize it.
        else {
            if (pStmPktVec->size() != stream_num) {
                pStmPktVec->resize(stream_num);
            }
        }

        // Assert all packets have already been av_packet_unref(), which
        // should be done after reading data from the bounded buffer. 

        i = 0;  // packet index
        for (auto it = vStmCtx.begin(); it != vStmCtx.end(); i++) {
            ret = file_read_packet((*it)->pFmtCtx, &(*pStmPktVec)[i]->packet);

            if (ret < 0) {
                (*pStmPktVec)[i]->header.flag = HEADER_FLAG_FIN;  // end of stream
            } else {
                (*pStmPktVec)[i]->header.flag = HEADER_FLAG_NULL; // normal state
            }

            (*pStmPktVec)[i]->header.stream_id = (*it)->stream_id;
            // We do not set the block timestamp since the block is pre-fetched from files.
            // However, we need to set the timestamp when forwarding them. In fact, it
            // should be set by the streamer.
            // (*pStmPktVec)[i]->header.block_ts = packet.dts;
            // (*pStmPktVec)[i]->header.block_ts = current_mtime();
            (*pStmPktVec)[i]->header.block_id = block_id;
            // duration in milli-seconds
            (*pStmPktVec)[i]->header.duration = (*pStmPktVec)[i]->packet.duration * 1000;
            (*pStmPktVec)[i]->header.duration *= (*it)->pFmtCtx->streams[0]->time_base.num;
            (*pStmPktVec)[i]->header.duration /= (*it)->pFmtCtx->streams[0]->time_base.den;


            // printf("frame duration %lld, time base: num %d den %d\n", packet.duration,
            //         conn_io->vFmtCtxPtrs[i]->streams[0]->time_base.num,
            //         conn_io->vFmtCtxPtrs[i]->streams[0]->time_base.den);

            if ((*pStmPktVec)[i]->packet.flags & AV_PKT_FLAG_KEY) {
                (*pStmPktVec)[i]->header.flag |= HEADER_FLAG_KEY;
            }

            fprintf(stderr, "produce: stream %d,\tblock %d,\tsize %d\n",
                (*it)->stream_id, block_id, (*pStmPktVec)[i]->packet.size);

            if (ret < 0) {
                fprintf(stderr, "remove a format context\n");
                it = vStmCtx.erase(it);
            }
            else {
                it++;
            }
        }

        block_id ++;

        // write a vec pointer of packet ptrs into the buffer, and get a stale vec pointer.
        pStmPktVec = pBuffer->produce(pStmPktVec);
        pStmPktVec = NULL;

        if (vStmCtx.empty()) {
            fprintf(stderr, "produce: streaming stops now\n");
            fprintf(stderr, "produce: insert a NULL data.\n");
            pBuffer->produce(NULL);
            break;
        }
    }
}


// read packets from the buffer, to which packets in files are written.
void consume(BoundedBuffer<StreamPktVecPtr> *pBuffer) {
    StreamPktVecPtr pStmPktVec = NULL;

    while (true) {
        pStmPktVec = pBuffer->consume();
        if (!pStmPktVec) {
            fprintf(stderr, "consume: consuming stops now\n");
            break;
        }
        for (auto &ptr : *pStmPktVec) {
            fprintf(stderr, "consume: stream %d,\tblock %d,\tsize %d\n",
                ptr->header.stream_id, ptr->header.block_id,
                ptr->packet.size);
            av_packet_unref(&ptr->packet);
        }
        // if ((*pStmPktVec)[0]->header.flag & HEADER_FLAG_FIN) {
        //     fprintf(stderr, "consume: consuming stops now\n");
        //     break;
        // }

        // 25FPS
        usleep(40000);
    }
}



#endif  // BOUNDED_BUFFER_H