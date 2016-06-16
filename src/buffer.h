#ifndef IBV_BUFFER_H
#define IBV_BUFFER_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct stream_buffer {
	// the list of buffers
	uint8_t *buffers;
	// list of buffer sizes
	uint64_t *sizes;
	// array of actual data sizes
	uint64_t *content_sizes;
	// wr id for corresponding buffer
	// we need this to keep track of the buffer after a
	// completion is retrieved through cq
	// when sending this needs to be updated
	uint64_t *wr_ids;
	// tail of the buffers that are being used
	// the buffers can be in a posted state, or received messages
	uint32_t tail;
	// head of the buffer
	uint32_t head;
	// no of buffers
	uint32_t no_bufs;
};

int stream_buffer_allocate(struct stream_buffer *buffers, uint32_t buf_size, uint32_t no_bufs);
void stream_buffer_increment_head(struct stream_buffer *buf);
void stream_buffer_increment_tail(struct stream_buffer *buf);

#endif /* IBV_BUFFER_H */
