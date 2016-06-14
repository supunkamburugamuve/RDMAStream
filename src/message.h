#ifndef IBV_MESSAGE_H
#define IBV_MESSAGE_H

#include <stdint.h>

/**
 * An RDMA destination. This information is needed to connect a Queue Pair.
 */
struct stream_dest_message {
	uint32_t lid;
	uint32_t qpn;
	uint32_t psn;
	uint8_t gid[33];
};

/**
 * Actual message
 */
struct stream_message {
	// flag to indicate head
	uint8_t head;
	// sequence no
	uint64_t sequence;
	// part no in case of multiple segments
	uint16_t part;
	// available credit to receive
	uint16_t credit;
	// length of the buffer
	uint64_t length;
	// the actual data
	uint8_t *buf;
	// flag to indicate the tail
	uint8_t tail;
};

struct stream_connect_message {
	// the remote destination to connect
	struct stream_dest_message dest;
	// starting number for sequence
	uint64_t sequence_start;
	// available credit for receiving messages.
	uint16_t credit;
	// an identifier to identify the connection.
	// this should be used when sending messages
	uint64_t connect_id;
};

int stream_data_message_copy_to_buffer(struct stream_message *msg, uint8_t *buf);
int stream_connect_message_copy_to_buffer(struct stream_connect_message *msg, uint8_t *buf);
int stream_connect_message_copy_from_buffer(uint8_t *buf, struct stream_connect_message *out);

#endif /* IBV_BUFFER_H */
