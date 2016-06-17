#include "stream.h"

static int stream_add_message_to_buffer(struct stream_connect_ctx *ctx, char *buf, uint64_t size);
static int stream_post_send_buffers(struct stream_connect_ctx *ctx);
static int stream_send_complete(struct stream_connect_ctx *ctx, uint64_t id);

/**
 * Create a single connection using a connection message
 */
struct stream_connect_ctx * stream_create_connection(
		struct stream_connect_cfg *cfg,
		struct stream_connect_message *conn_msg) {

}

/**
 * Connect to a remote
 */
int stream_connect(struct stream_connect_cfg *cfg) {
	return 0;
}

/**
 * Send message
 */
int stream_send_msg(struct stream_connect_ctx *ctx, char *buf, uint64_t size) {
	int ret;
	// first check the credit
	if (ctx->rem_credit.credit <= 0) {
		return 1;
	}
	// if credit is available
	ctx->rem_credit.credit--;



	return STREAM_OK;
}

static int stream_add_message_to_buffer(struct stream_connect_ctx *ctx, char *buf, uint64_t size) {
	// get the current buffer
	struct stream_buffer *buffer = &ctx->send_buf;
	// first lets check weather we have enough space for message
	int head = stream_buffer_cyclic_increment(buffer->no_bufs, buffer->head);
	// no space
	if (head == buffer->tail) {
		return STREAM_ERROR;
	}

	uint8_t *send_buf = buffer->buffers[ctx->send_buf.head];
	// now prepare a send buffer
	struct stream_message msg;
	msg.buf = buf;
	// our own credit
	msg.credit = ctx->self_credit.credit;
	msg.no_parts = 0;
	msg.part = 0;
	msg.sequence = ctx->wire_seq->send_seq++;
	msg.head = 1;
	msg.tail = 1;
	msg.length = size;

	uint64_t total_size = sizeof (uint8_t) + sizeof(uint64_t) +
			sizeof(uint16_t) + sizeof(uint16_t) +
			sizeof (uint16_t) + sizeof(uint64_t) + size + sizeof(uint8_t);

	stream_data_message_copy_to_buffer(msg, send_buf);
	buffer->content_sizes[ctx->send_buf.head] = total_size;
	// we will reset this to 0, this will indicate a send is in order
	buffer->wr_ids[ctx->send_buf.head] = 0;
	// now lets increment the head
	stream_buffer_increment_head(buffer);
	return STREAM_OK;
}

/**
 * Receive message
 */
int stream_recv_msg(struct stream_connect_ctx *ctx, char *buf, int size) {
	process_cq_event(ctx);
	return 0;
}

/**
 * Update the buffers after getting a send complete event
 */
static int stream_send_complete(struct stream_connect_ctx *ctx, uint64_t id) {
	struct stream_buffer *buffer = &ctx->send_buf;
	int ret;
	int tail = buffer->tail;
	int count = 0;
	// head == buffer->tail means we don't have anything in the ring buffers
	while (tail != buffer->head) {
		// ok we have an item to send
		if (buffer->wr_ids[tail] == id) {
			buffer->wr_ids[tail] = 0;
			// increment the tail if this is the first element
			if (count == 0) {
				stream_buffer_increment_tail(buffer);
			} else {
				fprintf(stderr, "Received a send completion out of order \n");
				return STREAM_ERROR;
			}
		}
		tail = stream_buffer_cyclic_increment(buffer->no_bufs, tail);
	}
	return STREAM_OK;
}

/**
 * Go through the buffers and send the buffers that are ready
 */
static int stream_post_send_buffers(struct stream_connect_ctx *ctx) {
	struct stream_buffer *buffer = &ctx->send_buf;
	int ret;

	int head = buffer->head;
	// head == buffer->tail means we don't have anything in the ring buffers
	while (head != buffer->tail) {
		// ok we have in item to send
		if (buffer->wr_ids[head] == 0) {
			ret = stream_post_send_buf(ctx, buffer->buffers[head], buffer->content_sizes[head]);
			if (ret) {
				fprintf(stderr, "Failed to post the message\n");
				return STREAM_ERROR;
			} else {
				buffer->wr_ids[head] = head;
			}
		}
		head = stream_buffer_cyclic_increment(buffer->no_bufs, head);
	}
}

/**
 * Post receive buffers, so that they can be used for incoming messages
 */
int stream_post_recv_buffers(struct stream_connect_ctx *ctx) {
	struct stream_buffer *buffer = &ctx->recv_buf;
	int ret;

	while (buffer->head != buffer->tail) {
		stream_buffer_increment_head(buffer);
		// post the head
		if (buffer->head != buffer->tail) {
			ret = stream_post_recv_buf(ctx, buffer->buffers[buffer->head], buffer->sizes[buffer->head]);
			if (ret) {
				fprintf(stderr, "Failed to post receives\n");
				return STREAM_ERROR;
			}
		}
	}
	return STREAM_OK;
}

/**
 * Go through the buffers and build a complete buffer
 */
int stream_build_message_from_buffers(struct stream_connect_ctx *ctx, uint8_t *buf, uint64_t size) {
	struct stream_buffer *buffer = &ctx->recv_buf;

	// length pointer distance
	uint64_t lengthPtr = sizeof (uint8_t) + sizeof(uint64_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof (uint16_t);
	uint64_t bufPtr = lengthPtr + sizeof(uint64_t);
	uint8_t *b = buffer->buffers[buffer->tail];
	// tail is the first
	uint8_t *tail = b;
    // check the begining
	if (*tail == TAIL_FLAG) {
		// now check the end
		uint64_t *length = b + lengthPtr;
		uint8_t *head = b + bufPtr + *length;
		if (*head == HEAD_FLAG) {
			memcpy(buf, b + bufPtr, sizeof(uint8_t) * length);
			stream_buffer_increment_tail(buffer);
		}
	}
	return 0;
}

int stream_process_cq_event(struct stream_connect_ctx *ctx) {
	struct stream_cfg *cfg;
	struct ibv_wc wc[2];
	int ne, i;
	int routs, scnt, rcnt, iters;

	cfg = ctx->cfg;
	if (cfg->use_event) {
		struct ibv_cq *ev_cq;
		void          *ev_ctx;

		if (ibv_get_cq_event(ctx->channel, &ev_cq, &ev_ctx)) {
			fprintf(stderr, "Failed to get cq_event\n");
			return 1;
		}

		if (ev_cq != ctx->cq) {
			fprintf(stderr, "CQ event for unknown CQ %p\n", ev_cq);
			return 1;
		}

		if (ibv_req_notify_cq(ctx->cq, 0)) {
			fprintf(stderr, "Couldn't request CQ notification\n");
			return 1;
		}
	}


	do {
		ne = ibv_poll_cq(ctx->cq, 2, wc);
		if (ne < 0) {
			fprintf(stderr, "poll CQ failed %d\n", ne);
			return 1;
		}

	} while (!cfg->use_event && ne < 1);

	for (i = 0; i < ne; ++i) {
		if (wc[i].status != IBV_WC_SUCCESS) {
			fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
					ibv_wc_status_str(wc[i].status),
					wc[i].status, (int) wc[i].wr_id);
			return 1;
		}

		switch ((int) wc[i].wr_id) {
		case STREAM_SEND_WRID:
			break;
		case STREAM_RECV_WRID:
			if (--routs <= 1) {
				routs += stream_post_recv(ctx, ctx->rx_depth - routs);
				if (routs < ctx->rx_depth) {
					fprintf(stderr, "Couldn't post receive (%d)\n", routs);
					return 1;
				}
			}
			break;
		default:
			fprintf(stderr, "Completion for unknown wr_id %d\n", (int) wc[i].wr_id);
			return 1;
		}
	}

	return 0;
}
