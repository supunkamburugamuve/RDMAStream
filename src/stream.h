#ifndef IBV_STREAM_H
#define IBV_STREAM_H

#include <sys/param.h>

#include <infiniband/verbs.h>

enum {
	STREAM_RECV_WRID = 1,
	STREAM_SEND_WRID = 2,
};

static int page_size;

/**
 * Keep track of the objects created for a connection.
 */
struct stream_context {
	struct ibv_context *context;
	struct ibv_comp_channel *channel;
	struct ibv_pd *pd;
	struct ibv_mr *mr;
	struct ibv_cq *cq;
	struct ibv_qp *qp;
	void *buf;
	int size;
	int	rx_depth;
	int	pending;
	struct ibv_port_attr portinfo;
};

/**
 * An RDMA destination. This information is needed to connect a Queue Pair.
 */
struct stream_dest {
	int lid;
	int qpn;
	int psn;
	union ibv_gid gid;
};

/**
 * Stream configurations.
 */
struct stream_cfg {
	char *ib_devname;     // device name, can be NULL and we use the first available device
	char *servername;     // server IP address to connect to
	int port;             // TCP port of the server
	int ib_port;          // ib port
	int size;             // size of the buffer
	enum ibv_mtu mtu;
	int rx_depth;         // receive depth
	int use_event;
	int sl;               // service level value
};

void stream_init_cfg(struct stream_cfg *cfg);
enum ibv_mtu stream_mtu_to_enum(int mtu);
uint16_t stream_get_local_lid(struct ibv_context *context, int port);
int stream_get_port_info(struct ibv_context *context, int port,
		struct ibv_port_attr *attr);
void wire_gid_to_gid(const char *wgid, union ibv_gid *gid);
void gid_to_wire_gid(const union ibv_gid *gid, char wgid[]);

#endif /* IBV_STREAM_H */
