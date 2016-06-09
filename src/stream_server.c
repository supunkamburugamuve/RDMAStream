#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "stream.h"

int stream_server_create_connection(struct stream_context *ctx, struct stream_cfg *cfg,
		const struct stream_dest *self_dest,) {

	if (stream_connect_ctx(ctx, cfg->ib_port, self_dest->psn, cfg->mtu, cfg->sl, self_dest, sgid_idx)) {
		fprintf(stderr, "Couldn't connect to remote QP\n");
		return 1;
	}
}
