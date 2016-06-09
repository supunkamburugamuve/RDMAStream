#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "stream.h"

int stream_server_connect(struct stream_context *ctx, struct stream_cfg *cfg) {
	if (stream_connect_ctx(ctx, cfg->ib_port, ctx->self_dest->psn, cfg->mtu, cfg->sl, ctx->rem_dest, cfg->gidx)) {
		fprintf(stderr, "Couldn't connect to remote QP\n");
		return 1;
	}
}
