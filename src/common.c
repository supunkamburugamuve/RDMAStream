#include "stream.h"
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/**
 * Initialize the configuration to default values
 */
void stream_init_cfg(struct stream_cfg *cfg) {
	cfg->ib_devname = NULL;
	cfg->servername = NULL;
	cfg->port = 18515;
	cfg->ib_port = 1;
	cfg->size = 4096;
	cfg->mtu = IBV_MTU_1024;
	cfg->rx_depth = 500;
	cfg->use_event = 0;
	cfg->sl = 0;
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

/**
 * Initialize the stream context by creating the infiniband objects
 */
int stream_init_ctx(struct stream_context *ctx, struct ibv_device *ib_dev, int size,
		int rx_depth, int port,
		int use_event, int is_server) {
	ctx->size     = size;
	ctx->rx_depth = rx_depth;

	ctx->buf = malloc(roundup(size, page_size));
	if (!ctx->buf) {
		fprintf(stderr, "Couldn't allocate work buf.\n");
		return 1;
	}

	memset(ctx->buf, 0x7b + is_server, size);

	ctx->context = ibv_open_device(ib_dev);
	if (!ctx->context) {
		fprintf(stderr, "Couldn't get context for %s\n",
				ibv_get_device_name(ib_dev));
		return 1;
	}

	if (use_event) {
		ctx->channel = ibv_create_comp_channel(ctx->context);
		if (!ctx->channel) {
			fprintf(stderr, "Couldn't create completion channel\n");
			return 1;
		}
	} else
		ctx->channel = NULL;

	ctx->pd = ibv_alloc_pd(ctx->context);
	if (!ctx->pd) {
		fprintf(stderr, "Couldn't allocate PD\n");
		return 1;
	}

	ctx->mr = ibv_reg_mr(ctx->pd, ctx->buf, size, IBV_ACCESS_LOCAL_WRITE);
	if (!ctx->mr) {
		fprintf(stderr, "Couldn't register MR\n");
		return 1;
	}

	ctx->cq = ibv_create_cq(ctx->context, rx_depth + 1, NULL,
			ctx->channel, 0);
	if (!ctx->cq) {
		fprintf(stderr, "Couldn't create CQ\n");
		return 1;
	}

	{
		struct ibv_qp_init_attr attr = {
				.send_cq = ctx->cq,
				.recv_cq = ctx->cq,
				.cap     = {
						.max_send_wr  = 1,
						.max_recv_wr  = rx_depth,
						.max_send_sge = 1,
						.max_recv_sge = 1
				},
				.qp_type = IBV_QPT_RC
		};

		ctx->qp = ibv_create_qp(ctx->pd, &attr);
		if (!ctx->qp)  {
			fprintf(stderr, "Couldn't create QP\n");
			return 1;
		}
	}

	{
		struct ibv_qp_attr attr = {
				.qp_state        = IBV_QPS_INIT,
				.pkey_index      = 0,
				.port_num        = port,
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
	}

	return 1;
}

int stream_close_ctx(struct stream_context *ctx) {
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

	free(ctx->buf);
	free(ctx);

	return 0;
}

int stream_connect_ctx(struct stream_context *ctx, int port, int my_psn,
		enum ibv_mtu mtu, int sl,
		struct stream_dest *dest, int sgid_idx) {
	int ret;
	struct ibv_qp_attr attr = {
			.qp_state		= IBV_QPS_RTR,
			.path_mtu		= mtu,
			.dest_qp_num	= dest->qpn,
			.rq_psn			= dest->psn,
			.max_dest_rd_atomic	= 1,
			.min_rnr_timer		= 12,
			.ah_attr		= {
					.is_global	= 0,
					.dlid		= dest->lid,
					.sl		= sl,
					.src_path_bits	= 0,
					.port_num	= port
			}
	};

	if (dest->gid.global.interface_id) {
		attr.ah_attr.is_global = 1;
		attr.ah_attr.grh.hop_limit = 1;
		attr.ah_attr.grh.dgid = dest->gid;
		attr.ah_attr.grh.sgid_index = sgid_idx;
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

	attr.qp_state	    = IBV_QPS_RTS;
	attr.timeout	    = 14;
	attr.retry_cnt	    = 7;
	attr.rnr_retry	    = 7;
	attr.sq_psn	    = my_psn;
	attr.max_rd_atomic  = 1;
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

int stream_post_recv(struct stream_context *ctx, int n) {
	struct ibv_sge list = {
		.addr	= (uintptr_t) ctx->buf,
		.length = ctx->size,
		.lkey	= ctx->mr->lkey
	};
	struct ibv_recv_wr wr = {
		.wr_id	    = STREAM_RECV_WRID,
		.sg_list    = &list,
		.num_sge    = 1,
	};
	struct ibv_recv_wr *bad_wr;
	int i;

	for (i = 0; i < n; ++i)
		if (ibv_post_recv(ctx->qp, &wr, &bad_wr))
			break;

	return i;
}

int stream_post_send(struct stream_context *ctx) {
	struct ibv_sge list = {
		.addr	= (uintptr_t) ctx->buf,
		.length = ctx->size,
		.lkey	= ctx->mr->lkey
	};
	struct ibv_send_wr wr = {
		.wr_id	    = STREAM_SEND_WRID,
		.sg_list    = &list,
		.num_sge    = 1,
		.opcode     = IBV_WR_SEND,
		.send_flags = IBV_SEND_SIGNALED,
	};
	struct ibv_send_wr *bad_wr;

	return ibv_post_send(ctx->qp, &wr, &bad_wr);
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

void wire_gid_to_gid(const char *wgid, union ibv_gid *gid) {
	char tmp[9];
	uint32_t v32;
	int i;

	for (tmp[8] = 0, i = 0; i < 4; ++i) {
		memcpy(tmp, wgid + i * 8, 8);
		sscanf(tmp, "%x", &v32);
		*(uint32_t *)(&gid->raw[i * 4]) = ntohl(v32);
	}
}

void gid_to_wire_gid(const union ibv_gid *gid, char wgid[]) {
	int i;

	for (i = 0; i < 4; ++i)
		sprintf(&wgid[i * 8], "%08x", htonl(*(uint32_t *)(gid->raw + i * 4)));
}
