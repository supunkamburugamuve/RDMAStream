#include "buffer.h"

int stream_buffer_allocate(struct stream_buffer *buffers, uint32_t buf_size, uint32_t no_bufs) {
	int i = 0;
	buffers->no_bufs = no_bufs;
	buffers->buffers = (uint8_t *)malloc(sizeof(uint8_t *) * no_bufs);
	buffers->sizes = (uint64_t *)malloc(sizeof(uint64_t) * no_bufs);
	buffers->content_sizes = (uint64_t *)malloc(sizeof(uint64_t) * no_bufs);
	for (i = 0; i < no_bufs; i++) {
        buffers->buffers[i] = malloc(sizeof(uint8_t) * buf_size);
	}
	return 0;
}

void stream_buffer_increment_head(struct stream_buffer *buf) {
	if (buf->head != buf->tail) {
		if (buf->head < buf->no_bufs - 1) {
			buf->head++;
		} else {
			buf->head = 0;
		}
	}
}

void stream_buffer_increment_tail(struct stream_buffer *buf) {
	if (buf->head != buf->tail) {
		if (buf->tail < buf->no_bufs - 1) {
			buf->tail++;
		} else {
			buf->tail = 0;
		}
	}
}
