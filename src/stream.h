#ifndef IBV_STREAM_H
#define IBV_STREAM_H

#include <sys/param.h>
#include <infiniband/verbs.h>

#include "message.h"

#define MAX_RETRIES    1

enum {
	STREAM_RECV_WRID = 1,
	STREAM_SEND_WRID = 2,
};

/**
 * An RDMA destination. This information is needed to connect a Queue Pair.
 */
struct stream_dest {
	uint32_t lid;
	uint32_t qpn;
	uint32_t psn;
	union ibv_gid gid;
};


struct stream_buffer {
	// set of buffers to hold the messages
	uint8_t **bufs;
	// set of send buffers
	// current index of the buffer
	uint16_t index;
	// no of buffers allocated
	uint16_t size;
};

/**
 * Keep track of the objects created for a connection.
 */
struct stream_connect_ctx {
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
	// device list to keep around until freed at the end
	struct ibv_device **dev_list;
	// the device to use
	struct ibv_device *device;
	struct stream_dest self_dest;   // self destination
	struct stream_dest *rem_dest;   // remote destination

	// memory mapped buffers for sending
	struct stream_buffer send_buf;
	// memory mapped buffers for receiving
	struct stream_buffer recv_buf;
};

/**
 * Stream configurations.
 */
struct stream_connect_cfg {
	char *ib_devname;     // device name, can be NULL and we use the first available device
	char *servername;     // server IP address to connect to
	int port;             // TCP port of the server
	int ib_port;          // ib port
	int size;             // size of the buffer
	enum ibv_mtu mtu;
	int rx_depth;         // receive depth
	int use_event;
	int sl;               // service level value
	int gidx;             // gid value
	int page_size;        // page size
};

/**
 * Create a connection using this information. This is done in the server
 */
struct stream_connect_req {
    struct stream_connect_cfg *cfg;
    struct stream_dest *dest;
};

/**
 * Initialize the configuration to default values
 */
void stream_init_cfg(struct stream_connect_cfg *cfg);

/**
 * Get the requested device according to configuration.
 */
int stream_assign_device(struct stream_connect_cfg *cfg, struct stream_connect_ctx *ctx);

/**
 * Connect the qps
 */
int stream_connect_ctx(struct stream_connect_cfg *cfg, struct stream_connect_ctx *ctx);

/**
 * Initialize the infiniband objects
 */
int stream_init_ctx(struct stream_connect_cfg *cfg, struct stream_connect_ctx *ctx);

/**
 * Process a connect request from a client
 */
struct stream_connect_ctx * stream_process_connect_request(struct stream_connect_cfg *cfg, struct stream_dest *dest);

int stream_post_recv(struct stream_connect_ctx *ctx, int n);
int stream_post_recv_single(struct stream_connect_ctx *ctx);
int stream_post_send(struct stream_connect_ctx *ctx);

int stream_close_ctx(struct stream_connect_ctx *ctx);

/**
 * Server and client functions
 */
int stream_server_connect(struct stream_connect_ctx *ctx, struct stream_connect_cfg *cfg,
		const struct stream_dest *self_dest);


enum ibv_mtu stream_mtu_to_enum(int mtu);
uint16_t stream_get_local_lid(struct ibv_context *context, int port);
int stream_get_port_info(struct ibv_context *context, int port,
		struct ibv_port_attr *attr);

int stream_dest_to_dest_message(struct stream_dest *dest, struct stream_dest_message *msg);
int stream_dest_message_to_dest(struct stream_dest_message *msg, struct stream_dest *dest);

// private methods
void wire_gid_to_gid(char *wgid, union ibv_gid *gid);
void gid_to_wire_gid(union ibv_gid *gid, char wgid[]);

#endif /* IBV_STREAM_H */
