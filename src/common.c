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
