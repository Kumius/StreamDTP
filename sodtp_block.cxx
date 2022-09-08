#include <sodtp_block.h>


int BlockDataBuffer::write(uint32_t id, uint8_t *src, int size) {
    int ret = 0;

    // Find existing block, and push back the data.
    for (const auto &it : buffer) {
        if (it->id == id) {
            return it->write(src, size);
        }
    }

    // Else, this is a new block.
    // Create new BlockData.
    BlockDataPtr ptr(new BlockData(id));
    buffer.push_front(ptr);

    // If too many blocks, pop the stale block.
    if (buffer.size() > MAX_BLOCK_NUM) {
        buffer.pop_back();
    }

    ret = ptr->write(src, size);

    return ret;
}


SodtpBlockPtr BlockDataBuffer::read(uint32_t id, SodtpStreamHeader *head) {

    SodtpBlockPtr ptr = NULL;
    int32_t size;
    uint8_t *data;
    // printf("buffer number = %d, block_id = %d\n", buffer.size(), block_id);
    for (const auto &it : buffer) {
        // printf("block id = %d,\t size = %d\n", it->block_id, it->offset - it->data);
        if (it->id == id) {
            ptr = std::make_shared<SodtpBlock>();

            // printf("data size = %d\n", it->offset - it->data);
            // printf("debug: %s:%s:%d\n", __FILE__, __FUNCTION__, __LINE__);

            memcpy(head, it->data, sizeof(*head));
            data = it->data + sizeof(*head);
            size = it->offset - it->data - sizeof(*head);



            ptr->last_one = (bool)(head->flag & HEADER_FLAG_FIN);
            ptr->key_block = (bool)(head->flag & HEADER_FLAG_KEY);

            ptr->stream_id = head->stream_id;
            ptr->block_ts = head->block_ts;
            ptr->block_id = head->block_id;
            ptr->duration = head->duration;

            ptr->data = new uint8_t[size + AV_INPUT_BUFFER_PADDING_SIZE];
            ptr->size = size;
            memcpy(ptr->data, data, size);
            memset(ptr->data + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
            break;
        }
    }
    // printf("debug: %s:%s:%d\n", __FILE__, __FUNCTION__, __LINE__);
    return ptr;
}


