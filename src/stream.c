
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "stream.h"

/**
 * Initialize the configuration to default values
 */
void stream_init_cfg(struct stream_connect_cfg *cfg) {
	cfg->ib_devname = NULL;
	cfg->servername = NULL;
	cfg->port = 18515;
	cfg->ib_port = 1;
	cfg->size = 4096;
	cfg->mtu = IBV_MTU_1024;
	cfg->rx_depth = 12;
	cfg->use_event = 0;
	cfg->sl = 0;
	cfg->gidx = -1;
}

enum ibv_mtu stream_mtu_to_enum(int mtu) {
	switch (mtu) {
		case 256:  return IBV_MTU_256;
		case 512:  return IBV_MTU_512;
		case 1024: return IBV_MTU_1024;
		case 2048: return IBV_MTU_2048;
		case 4096: return IBV_MTU_4096;
		default:   return -1;
	}
}

int stream_assign_device(struct stream_connect_cfg *cfg, struct stream_connect_ctx *ctx) {
	struct ibv_device **dev_list;
	struct ibv_device *ib_dev;
	dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
		perror("Failed to get IB devices list");
		return 1;
	}

	if (!cfg->ib_devname) {
		ib_dev = *dev_list;
		if (!ib_dev) {
			fprintf(stderr, "No IB devices found\n");
			return 1;
		}
	} else {
		int i;
		for (i = 0; dev_list[i]; ++i) {
			if (!strcmp(ibv_get_device_name(dev_list[i]), cfg->ib_devname)) {
				break;
			}
		}
		ib_dev = dev_list[i];
		if (!ib_dev) {
			fprintf(stderr, "IB device %s not found\n", cfg->ib_devname);
			return 1;
		}
	}

	ctx->dev_list = dev_list;
	ctx->device = ib_dev;

	return 0;
}

/**
 * Initialize the stream context by creating the infiniband objects
 */
int stream_init_ctx(struct stream_connect_cfg *cfg, struct stream_connect_ctx *ctx) {
	ctx->size     = cfg->size;
	ctx->rx_depth = cfg->rx_depth;

	ctx->buf = malloc(roundup(cfg->size, cfg->page_size));
	if (!ctx->buf) {
		fprintf(stderr, "Couldn't allocate work buf.\n");
		return 1;
	}

	memset(ctx->buf, 0x7b + !cfg->servername, cfg->size);

	ctx->context = ibv_open_device(ctx->device);
	if (!ctx->context) {
		fprintf(stderr, "Couldn't get context for %s\n",
				ibv_get_device_name(ctx->device));
		return 1;
	}

	ctx->channel = NULL;
	if (cfg->use_event) {
		ctx->channel = ibv_create_comp_channel(ctx->context);
		if (!ctx->channel) {
			fprintf(stderr, "Couldn't create completion channel\n");
			return 1;
		}
	}

	ctx->pd = ibv_alloc_pd(ctx->context);
	if (!ctx->pd) {
		fprintf(stderr, "Couldn't allocate PD\n");
		return 1;
	}

	ctx->mr = ibv_reg_mr(ctx->pd, ctx->buf, cfg->size, IBV_ACCESS_LOCAL_WRITE);
	if (!ctx->mr) {
		fprintf(stderr, "Couldn't register MR\n");
		return 1;
	}

	ctx->cq = ibv_create_cq(ctx->context, cfg->rx_depth + 1, NULL,
			ctx->channel, 0);
	if (!ctx->cq) {
		fprintf(stderr, "Couldn't create CQ\n");
		return 1;
	}

	struct ibv_qp_init_attr init_attr = {
			.send_cq = ctx->cq,
			.recv_cq = ctx->cq,
			.cap     = {
					.max_send_wr  = 1,
					.max_recv_wr  = cfg->rx_depth,
					.max_send_sge = 1,
					.max_recv_sge = 1
			},
			.qp_type = IBV_QPT_RC
	};

	ctx->qp = ibv_create_qp(ctx->pd, &init_attr);
	if (!ctx->qp)  {
		fprintf(stderr, "Couldn't create QP\n");
		return 1;
	}

	struct ibv_qp_attr attr = {
			.qp_state        = IBV_QPS_INIT,
			.pkey_index      = 0,
			.port_num        = cfg->ib_port,
			.qp_access_flags = 0
	};

	if (ibv_modify_qp(ctx->qp, &attr,
			IBV_QP_STATE              |
			IBV_QP_PKEY_INDEX         |
			IBV_QP_PORT               |
			IBV_QP_ACCESS_FLAGS)) {
		fprintf(stderr, "Failed to modify QP to INIT\n");
		return 1;
	}

	if (cfg->use_event) {
		if (ibv_req_notify_cq(ctx->cq, 0)) {
			fprintf(stderr, "Couldn't request CQ notification\n");
			return 1;
		}
	}

	if (stream_get_port_info(ctx->context, cfg->ib_port, &ctx->portinfo)) {
		fprintf(stderr, "Couldn't get port info\n");
		return 1;
	}

	ctx->self_dest.lid = ctx->portinfo.lid;
	if (ctx->portinfo.link_layer == IBV_LINK_LAYER_INFINIBAND && !ctx->self_dest.lid) {
		fprintf(stderr, "Couldn't get local LID\n");
		return 1;
	}

	if (cfg->gidx >= 0) {
		if (ibv_query_gid(ctx->context, cfg->ib_port, cfg->gidx, &ctx->self_dest.gid)) {
			fprintf(stderr, "Could not get local gid for gid index %d\n", cfg->gidx);
			return 1;
		}
	} else {
		memset(&ctx->self_dest.gid, 0, sizeof ctx->self_dest.gid);
	}

	ctx->self_dest.qpn = ctx->qp->qp_num;
	ctx->self_dest.psn = lrand48() & 0xffffff;

	return 0;
}

int stream_close_ctx(struct stream_connect_ctx *ctx) {
	if (ibv_destroy_qp(ctx->qp)) {
		fprintf(stderr, "Couldn't destroy QP\n");
		return 1;
	}

	if (ibv_destroy_cq(ctx->cq)) {
		fprintf(stderr, "Couldn't destroy CQ\n");
		return 1;
	}

	if (ibv_dereg_mr(ctx->mr)) {
		fprintf(stderr, "Couldn't deregister MR\n");
		return 1;
	}

	if (ibv_dealloc_pd(ctx->pd)) {
		fprintf(stderr, "Couldn't deallocate PD\n");
		return 1;
	}

	if (ctx->channel) {
		if (ibv_destroy_comp_channel(ctx->channel)) {
			fprintf(stderr, "Couldn't destroy completion channel\n");
			return 1;
		}
	}

	if (ibv_close_device(ctx->context)) {
		fprintf(stderr, "Couldn't release context\n");
		return 1;
	}

	if (ctx->dev_list) {
		ibv_free_device_list(ctx->dev_list);
	}

	free(ctx->buf);
	free(ctx);

	return 0;
}

int stream_connect_ctx(struct stream_connect_cfg *cfg, struct stream_connect_ctx *ctx) {
	int ret;
	struct ibv_qp_attr attr = {
			.qp_state = IBV_QPS_RTR,
			.path_mtu = cfg->mtu,
			.dest_qp_num = ctx->rem_dest->qpn,
			.rq_psn = ctx->rem_dest->psn,
			.max_dest_rd_atomic	= 1,
			.min_rnr_timer = 12,
			.ah_attr = {
					.is_global = 0,
					.dlid = ctx->rem_dest->lid,
					.sl	= cfg->sl,
					.src_path_bits = 0,
					.port_num = cfg->ib_port
			}
	};

	if (ctx->rem_dest->gid.global.interface_id) {
		attr.ah_attr.is_global = 1;
		attr.ah_attr.grh.hop_limit = 1;
		attr.ah_attr.grh.dgid = ctx->rem_dest->gid;
		attr.ah_attr.grh.sgid_index = cfg->gidx;
	}

	if ((ret = ibv_modify_qp(ctx->qp, &attr,
			IBV_QP_STATE              |
			IBV_QP_AV                 |
			IBV_QP_PATH_MTU           |
			IBV_QP_DEST_QPN           |
			IBV_QP_RQ_PSN             |
			IBV_QP_MAX_DEST_RD_ATOMIC |
			IBV_QP_MIN_RNR_TIMER))) {
		fprintf(stderr, "Failed to modify QP to RTR\n");
		return ret;
	}

	attr.qp_state = IBV_QPS_RTS;
	attr.timeout = 14;
	attr.retry_cnt = 7;
	attr.rnr_retry = 7;
	attr.sq_psn = ctx->self_dest.psn;
	attr.max_rd_atomic = 1;
	if ((ret = ibv_modify_qp(ctx->qp, &attr,
			IBV_QP_STATE              |
			IBV_QP_TIMEOUT            |
			IBV_QP_RETRY_CNT          |
			IBV_QP_RNR_RETRY          |
			IBV_QP_SQ_PSN             |
			IBV_QP_MAX_QP_RD_ATOMIC))) {
		fprintf(stderr, "Failed to modify QP to RTS\n");
		return ret;
	}

	return 0;
}

int stream_post_recv_single(struct stream_connect_ctx *ctx) {
	int err, retries;

	struct ibv_sge list = {
		.addr	= (uintptr_t) ctx->buf,
		.length = ctx->size,
		.lkey	= ctx->mr->lkey
	};
	struct ibv_recv_wr wr = {
		.wr_id = STREAM_RECV_WRID,
		.sg_list = &list,
		.num_sge = 1,
	};
	struct ibv_recv_wr *bad_wr;

	retries = MAX_RETRIES;
	do {
		ibv_post_recv(ctx->qp, &wr, &bad_wr);
	} while(err && --retries);

	return err;
}

int stream_post_recv(struct stream_connect_ctx *ctx, int n) {
	//printf("recv message\n");
	struct ibv_sge list = {
		.addr	= (uintptr_t) ctx->buf,
		.length = ctx->size,
		.lkey	= ctx->mr->lkey
	};
	struct ibv_recv_wr wr = {
		.wr_id = STREAM_RECV_WRID,
		.sg_list = &list,
		.num_sge = 1,
	};
	struct ibv_recv_wr *bad_wr;
	int i;
	for (i = 0; i < n; ++i) {
		if (ibv_post_recv(ctx->qp, &wr, &bad_wr)) {
			break;
		}
	}
	return i;
}

int stream_post_send(struct stream_connect_ctx *ctx) {
	//printf("send message\n");
	int err, retries;
	struct ibv_sge list = {
		.addr = (uintptr_t) ctx->buf,
		.length = ctx->size,
		.lkey = ctx->mr->lkey
	};
	struct ibv_send_wr wr = {
		.wr_id = STREAM_SEND_WRID,
		.sg_list = &list,
		.num_sge = 1,
		.opcode = IBV_WR_SEND,
		.send_flags = IBV_SEND_SIGNALED,
	};
	struct ibv_send_wr *bad_wr;

	retries = MAX_RETRIES;
	do {
		err = ibv_post_send(ctx->qp, &wr, &bad_wr);
	} while(err && --retries);

	return err;
}

struct stream_connect_ctx * stream_process_connect_request(struct stream_connect_cfg *cfg, struct stream_dest *dest) {
  // first lets allocate the context
  struct stream_connect_ctx *ctx;
  ctx = calloc(1, sizeof *ctx);
  if (!ctx) {
    goto error;
  }
  ctx->rem_dest = dest;

  // get the available devices
  if (stream_assign_device(cfg, ctx)) {
    fprintf(stderr, "Failed to get infiniband device\n");
    goto error;
  }

  // initialize the context
  if (stream_init_ctx(cfg, ctx)) {
    fprintf(stderr, "Failed to initialize context\n");
    goto error;
  }

  // now connect the context to the destination
  if (stream_connect_ctx(cfg, ctx)) {
    fprintf(stderr, "Couldn't connect to remote QP\n");
    goto error;
  }

  return ctx;

  error:
    stream_close_ctx(ctx);
    return NULL;
}


uint16_t stream_get_local_lid(struct ibv_context *context, int port) {
	struct ibv_port_attr attr;

	if (ibv_query_port(context, port, &attr)) {
		return 0;
	}

	return attr.lid;
}

int stream_get_port_info(struct ibv_context *context, int port,
		     struct ibv_port_attr *attr) {
	return ibv_query_port(context, port, attr);
}

int stream_dest_message_to_dest(struct stream_dest_message *msg, struct stream_dest *dest) {
	dest->lid = msg->lid;
	dest->psn = msg->psn;
	dest->qpn = msg->qpn;
	wire_gid_to_gid((char *)msg->gid, &dest->gid);

	return 1;
}

int stream_dest_to_dest_message(struct stream_dest *dest, struct stream_dest_message *msg) {
	msg->lid = dest->lid;
	msg->psn = dest->psn;
	msg->qpn = dest->qpn;
	gid_to_wire_gid(&dest->gid, (char *)msg->gid);
	return 1;
}

void wire_gid_to_gid(char *wgid, union ibv_gid *gid) {
	char tmp[9];
	uint32_t v32;
	int i;

	for (tmp[8] = 0, i = 0; i < 4; ++i) {
		memcpy(tmp, wgid + i * 8, 8);
		sscanf(tmp, "%x", &v32);
		*(uint32_t *)(&gid->raw[i * 4]) = ntohl(v32);
	}
}

void gid_to_wire_gid(union ibv_gid *gid, char wgid[]) {
	int i;

	for (i = 0; i < 4; ++i)
		sprintf(&wgid[i * 8], "%08x", htonl(*(uint32_t *)(gid->raw + i * 4)));
}

