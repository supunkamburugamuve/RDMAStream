#ifndef IBV_BUFFER_H
#define IBV_BUFFER_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct stream_buffer {
	// the list of buffers
	uint8_t *buffers;
	// list of sizes
	uint32_t *sizes;
};

int stream_allocate_buffers(struct stream_buffer *buffers, int buf_size, int no_bufs);

#endif /* IBV_BUFFER_H */
