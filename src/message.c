#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "message.h"

int stream_data_message_copy_to_buffer(struct stream_message *msg, uint8_t *buf) {
	unsigned int address = 0;
	memcpy(buf + address, (uint8_t *)&msg->head, sizeof uint8_t);
	address += sizeof uint8_t;
	memcpy(buf + address, (uint64_t *)&msg->sequence, sizeof uint64_t);
	address += sizeof uint64_t;
	memcpy(buf + address, (uint16_t *)&msg->part, sizeof uint16_t);
	address += sizeof uint16_t;
	memcpy(buf + address, (uint16_t *)&msg->credit, sizeof uint16_t);
	address += sizeof uint16_t;
	memcpy(buf + address, (uint64_t *)&msg->length, sizeof uint64_t);
	address += sizeof uint64_t;
	memcpy(buf + address, (uint8_t *)msg->buf, msg->length);
	address += msg->length;
	memcpy(buf + address, (uint8_t *)&msg->tail, msg->tail);
}

int stream_connect_message_copy_to_buffer(struct stream_connect_message *msg, uint8_t *buf) {
	memcpy(buf, (uint8_t *)msg, sizeof (struct stream_connet_message));
}

int stream_connect_message_copy_from_buffer(uint8_t *buf, struct stream_connect_message *out) {
	memcpy(out, (struct stream_connect_message *)buf, sizeof(struct stream_connect_message));
	return 1;
}
