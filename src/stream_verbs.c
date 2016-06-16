#include "stream.h"

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
int stream_send_msg(struct stream_connect_ctx *ctx, char *buf, int size) {
	int ret;
	// first check the credit
	if (ctx->rem_credit.credit <= 0) {
		return 1;
	}
	// if credit is available
	ctx->rem_credit.credit--;

	// now get the buffer for sending
	uint8_t *send_buf = (uint8_t *)malloc(size);

	// now prepare a send buffer for sending
	struct stream_message msg;
	msg.buf = buf;
	msg.credit = ctx->rem_credit.credit;
	msg.part = 0;
	msg.sequence = 0;
	msg.head = 1;
	msg.tail = 1;
	msg.length = size;

	stream_data_message_copy_to_buffer(msg, send_buf);

	// get the set of buffers required for sending
	ret = stream_post_send_buf(ctx, send_buf, size);
	if (ret) {
		fprintf(stderr, "Failed to post the message\n");
		return STREAM_ERROR;
	}
	return STREAM_OK;
}

/**
 * Receive message
 */
int stream_recv_msg(struct stream_connect_ctx *ctx, char *buf, int size) {
	process_cq_event(ctx);
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
			++scnt;
			break;

		case STREAM_RECV_WRID:
			if (--routs <= 1) {
				routs += stream_post_recv(ctx, ctx->rx_depth - routs);
				if (routs < ctx->rx_depth) {
					fprintf(stderr, "Couldn't post receive (%d)\n", routs);
					return 1;
				}
			}

			++rcnt;
			break;

		default:
			fprintf(stderr, "Completion for unknown wr_id %d\n", (int) wc[i].wr_id);
			return 1;
		}

		ctx->pending &= ~(int) wc[i].wr_id;
		if (scnt < iters && !ctx->pending) {
			if (stream_post_send(ctx)) {
				fprintf(stderr, "Couldn't post send\n");
				return 1;
			}
			ctx->pending = STREAM_RECV_WRID |
					STREAM_SEND_WRID;
		}
	}

	return 0;
}
