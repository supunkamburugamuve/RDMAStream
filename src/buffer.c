#include "buffer.h"

int stream_allocate_buffers(struct stream_buffer *buffers, uint32_t buf_size, uint32_t no_bufs) {
	int i = 0;
	buffers->buffers = malloc(sizeof(uint8_t *) * no_bufs);
	buffers->sizes = malloc(sizeof(uint32_t))
	for (i = 0; i < no_bufs; i++) {
        buffers->buffers[i] = malloc(sizeof(uint8_t) * buf_size);
	}
	return 0;
}
