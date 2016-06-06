#ifndef IBV_STREAM_H
#define IBV_STREAM_H

#include <sys/param.h>

#include <infiniband/verbs.h>

enum {
	STREAM_RECV_WRID = 1,
	STREAM_SEND_WRID = 2,
};

static int page_size;

struct stream_context {
	struct ibv_context	*context;
	struct ibv_comp_channel *channel;
	struct ibv_pd		*pd;
	struct ibv_mr		*mr;
	struct ibv_cq		*cq;
	struct ibv_qp		*qp;
	void			*buf;
	int			 size;
	int			 rx_depth;
	int			 pending;
	struct ibv_port_attr     portinfo;
};

struct stream_dest {
	int lid;
	int qpn;
	int psn;
	union ibv_gid gid;
};

enum ibv_mtu pp_mtu_to_enum(int mtu);
uint16_t pp_get_local_lid(struct ibv_context *context, int port);
int pp_get_port_info(struct ibv_context *context, int port,
		     struct ibv_port_attr *attr);
void wire_gid_to_gid(const char *wgid, union ibv_gid *gid);
void gid_to_wire_gid(const union ibv_gid *gid, char wgid[]);

#endif /* IBV_STREAM_H */
