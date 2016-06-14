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
int stream_send_msg(struct stream_connect_ctx *ctx, char *buf) {
	return 0;
}

/**
 * Receive message
 */
int stream_recv_msg(struct stream_connect_ctx *ctx, char *buf) {
	return 0;
}
