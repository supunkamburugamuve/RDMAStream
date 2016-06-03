#include "stream_verbs.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <malloc.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <time.h>
#include <infiniband/verbs.h>

enum {
      STREAM_RECV_WRID = 1,
	  STREAM_SEND_WRID = 2,
};

struct stream_dest {
      int lid;
      int qpn;
      int psn;
};

// configurations for stream
struct stream_cfg {
	int use_event;    // use the completion event channel
	int rx_depth;     // receive depth
	int buff_size;    // size of the buffer to use
	int port;         // port to be used
};

struct stream_context {
      struct ibv_context      *context;
      struct ibv_comp_channel *channel;
      struct ibv_pd           *pd;
      struct ibv_mr           *mr;
      struct ibv_cq           *cq;
      struct ibv_qp           *qp;
      void              *buf;
      int                size;
      int                rx_depth;
      int                pending;
};

uint16_t stream_get_local_lid(struct ibv_context *context, int port) {
	struct ibv_port_attr attr;
	if (ibv_query_port(context, port, &attr))
		return 0;
	return attr.lid;
}

static struct stream_context *stream_init_ctx(struct ibv_device *ib_dev, int size,
                                  int port,
                                  int page_size, struct stream_cfg *cfg) {
	struct stream_context *ctx;

	ctx = (struct stream_context *)malloc(sizeof *ctx);
	if (!ctx)
		return NULL;

	ctx->size = size;
	ctx->rx_depth = cfg->rx_depth;

	// allocate the buffer
	ctx->buf = memalign(page_size, cfg->buff_size);
	if (!ctx->buf) {
		fprintf(stderr, "Couldn't allocate work buf.\n");
	    return NULL;
	}
	memset(ctx->buf, 0, size);

	ctx->context = ibv_open_device(ib_dev);
	if (!ctx->context) {
		fprintf(stderr, "Couldn't get context for %s\n",
			  ibv_get_device_name(ib_dev));
		return NULL;
	}

	if (cfg->use_event) {
		ctx->channel = ibv_create_comp_channel(ctx->context);
		if (!ctx->channel) {
			  fprintf(stderr, "Couldn't create completion channel\n");
			  return NULL;
		}
	} else {
		ctx->channel = NULL;
	}

	ctx->pd = ibv_alloc_pd(ctx->context);
	if (!ctx->pd) {
		fprintf(stderr, "Couldn't allocate PD\n");
		return NULL;
	}

	ctx->mr = ibv_reg_mr(ctx->pd, ctx->buf, size, IBV_ACCESS_LOCAL_WRITE);
	if (!ctx->mr) {
		fprintf(stderr, "Couldn't register MR\n");
		return NULL;
	}

	ctx->cq = ibv_create_cq(ctx->context, cfg->rx_depth + 1, NULL,
					ctx->channel, 0);
	if (!ctx->cq) {
		fprintf(stderr, "Couldn't create CQ\n");
		return NULL;
	}


	struct ibv_qp_init_attr init_attr;
	init_attr.send_cq = ctx->cq;
	init_attr.recv_cq = ctx->cq;
	init_attr.cap.max_send_wr  = 1;
	init_attr.cap.max_recv_wr  = cfg->rx_depth;
	init_attr.cap.max_send_sge = 1;
	init_attr.cap.max_recv_sge = 1;
	init_attr.qp_type = IBV_QPT_RC;
	ctx->qp = ibv_create_qp(ctx->pd, &init_attr);
	if (!ctx->qp)  {
		  fprintf(stderr, "Couldn't create QP\n");
		  return NULL;
	}

	struct ibv_qp_attr qp_attr;
	qp_attr.qp_state        = IBV_QPS_INIT;
	qp_attr.pkey_index      = 0;
	qp_attr.port_num        = port;
	qp_attr.qp_access_flags = 0;
	if (ibv_modify_qp(ctx->qp, &qp_attr,
				  IBV_QP_STATE              |
				  IBV_QP_PKEY_INDEX         |
				  IBV_QP_PORT               |
				  IBV_QP_ACCESS_FLAGS)) {
		  fprintf(stderr, "Failed to modify QP to INIT\n");
		  return NULL;
	}

	return ctx;
}

int stream_close_ctx(struct stream_context *ctx)
{
      if (ibv_destroy_qp(ctx->qp)) {
            fprintf(stderr, "Couldn't destroy QP\n");
            return 1;
      }

      if (ibv_destroy_cq(ctx->cq)) {
            fprintf(stderr, "Couldn't destroy CQ\n");
            return 1;
      }

      if (ibv_dereg_mr(ctx->mr)) {
            fprintf(stderr, "Couldn't de-register MR\n");
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

static int stream_connect_ctx(struct stream_context *ctx, int port, int my_psn,
                    enum ibv_mtu mtu, struct stream_dest *dest)
{
      struct ibv_qp_attr qp_attr;
      qp_attr.qp_state         = IBV_QPS_RTR;
      qp_attr.path_mtu         = mtu;
      qp_attr.dest_qp_num            = dest->qpn;
      qp_attr.rq_psn                 = dest->psn;
      qp_attr.max_dest_rd_atomic     = 1;
      qp_attr.min_rnr_timer          = 12;

      qp_attr.ah_attr.is_global  = 0;
      qp_attr.ah_attr.dlid       = dest->lid;
      qp_attr.ah_attr.sl         = 0;
      qp_attr.ah_attr.src_path_bits    = 0;
      qp_attr.ah_attr.port_num   = port;

      if (ibv_modify_qp(ctx->qp, &qp_attr,
                    IBV_QP_STATE              |
                    IBV_QP_AV                 |
                    IBV_QP_PATH_MTU           |
                    IBV_QP_DEST_QPN           |
                    IBV_QP_RQ_PSN             |
                    IBV_QP_MAX_DEST_RD_ATOMIC |
                    IBV_QP_MIN_RNR_TIMER)) {
            fprintf(stderr, "Failed to modify QP to RTR\n");
            return 1;
      }

      qp_attr.qp_state         = IBV_QPS_RTS;
      qp_attr.timeout          = 14;
      qp_attr.retry_cnt        = 7;
      qp_attr.rnr_retry        = 7;
      qp_attr.sq_psn     = my_psn;
      qp_attr.max_rd_atomic  = 1;
      if (ibv_modify_qp(ctx->qp, &qp_attr,
                    IBV_QP_STATE              |
                    IBV_QP_TIMEOUT            |
                    IBV_QP_RETRY_CNT          |
                    IBV_QP_RNR_RETRY          |
                    IBV_QP_SQ_PSN             |
                    IBV_QP_MAX_QP_RD_ATOMIC)) {
            fprintf(stderr, "Failed to modify QP to RTS\n");
            return 1;
      }

      return 0;
}

static int stream_post_recv(struct stream_context *ctx, int n) {
	// we only use one buffer
	struct ibv_sge list;
	list.addr = (uintptr_t) ctx->buf;
	list.length = ctx->size;
	list.lkey = ctx->mr->lkey;

	// sending struct
	struct ibv_recv_wr wr;
	wr.wr_id          = STREAM_RECV_WRID;
	wr.sg_list    = &list;
	wr.num_sge    = 1;
	struct ibv_recv_wr *bad_wr;

	// retry in case of failure
	int i;
	for (i = 0; i < n; ++i) {
		if (ibv_post_recv(ctx->qp, &wr, &bad_wr)) {
	        break;
		}
	}
    return i;
}

static int stream_post_send(struct stream_context *ctx) {
	// we only use 1 buffer
    struct ibv_sge list;
	list.addr = (uintptr_t) ctx->buf;
	list.length = ctx->size;
	list.lkey = ctx->mr->lkey;

	// send
    struct ibv_send_wr wr;
    wr.wr_id = STREAM_SEND_WRID;
	wr.sg_list = &list;
	wr.num_sge = 1;
	wr.opcode = IBV_WR_SEND;
	wr.send_flags = IBV_SEND_SIGNALED;

	// in case of failure
    struct ibv_send_wr *bad_wr;

    return ibv_post_send(ctx->qp, &wr, &bad_wr);
}

static struct stream_dest *stream_client_exch_dest(const char *servername, int port,
                                     const struct stream_dest *my_dest) {
      struct addrinfo *res, *t;
      struct addrinfo hints;
      hints.ai_family   = AF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;

      char *service;
      char msg[sizeof "0000:000000:000000"];
      int n;
      int sockfd = -1;
      struct stream_dest *rem_dest = NULL;

      if (asprintf(&service, "%d", port) < 0)
            return NULL;

      n = getaddrinfo(servername, service, &hints, &res);

      if (n < 0) {
            fprintf(stderr, "%s for %s:%d\n", gai_strerror(n), servername, port);
            return NULL;
      }

      for (t = res; t; t = t->ai_next) {
            sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
            if (sockfd >= 0) {
                  if (!connect(sockfd, t->ai_addr, t->ai_addrlen))
                        break;
                  close(sockfd);
                  sockfd = -1;
            }
      }

      freeaddrinfo(res);

      if (sockfd < 0) {
            fprintf(stderr, "Couldn't connect to %s:%d\n", servername, port);
            return NULL;
      }

      sprintf(msg, "%04x:%06x:%06x", my_dest->lid, my_dest->qpn, my_dest->psn);
      if (write(sockfd, msg, sizeof msg) != sizeof msg) {
            fprintf(stderr, "Couldn't send local address\n");
            goto out;
      }

      if (read(sockfd, msg, sizeof msg) != sizeof msg) {
            perror("client read");
            fprintf(stderr, "Couldn't read remote address\n");
            goto out;
      }

      write(sockfd, "done", sizeof "done");

      rem_dest = (struct stream_dest *)malloc(sizeof *rem_dest);
      if (!rem_dest) {
            goto out;
      }
      sscanf(msg, "%x:%x:%x", &rem_dest->lid, &rem_dest->qpn, &rem_dest->psn);

out:
      close(sockfd);
      return rem_dest;
}

static struct stream_dest *stream_server_exch_dest(struct stream_context *ctx,
                                     int ib_port, enum ibv_mtu mtu, int port,
                                     const struct stream_dest *my_dest)
{
      struct addrinfo *res, *t;
      struct addrinfo hints;
      hints.ai_flags    = AI_PASSIVE;
      hints.ai_family   = AF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;

      char *service;
      char msg[sizeof "0000:000000:000000"];
      int n;
      int sockfd = -1, connfd;
      struct stream_dest *rem_dest = NULL;

      if (asprintf(&service, "%d", port) < 0)
            return NULL;

      n = getaddrinfo(NULL, service, &hints, &res);

      if (n < 0) {
            fprintf(stderr, "%s for port %d\n", gai_strerror(n), port);
            return NULL;
      }

      for (t = res; t; t = t->ai_next) {
            sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
            if (sockfd >= 0) {
                  n = 1;

                  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof n);

                  if (!bind(sockfd, t->ai_addr, t->ai_addrlen))
                        break;
                  close(sockfd);
                  sockfd = -1;
            }
      }

      freeaddrinfo(res);

      if (sockfd < 0) {
            fprintf(stderr, "Couldn't listen to port %d\n", port);
            return NULL;
      }

      listen(sockfd, 1);
      connfd = accept(sockfd, NULL, 0);
      close(sockfd);
      if (connfd < 0) {
            fprintf(stderr, "accept() failed\n");
            return NULL;
      }

      n = read(connfd, msg, sizeof msg);
      if (n != sizeof msg) {
            perror("server read");
            fprintf(stderr, "%d/%d: Couldn't read remote address\n", n, (int) sizeof msg);
            goto out;
      }

      rem_dest = (struct stream_dest *)malloc(sizeof *rem_dest);
      if (!rem_dest)
            goto out;

      sscanf(msg, "%x:%x:%x", &rem_dest->lid, &rem_dest->qpn, &rem_dest->psn);

      if (stream_connect_ctx(ctx, ib_port, my_dest->psn, mtu, rem_dest)) {
            fprintf(stderr, "Couldn't connect to remote QP\n");
            free(rem_dest);
            rem_dest = NULL;
            goto out;
      }

      sprintf(msg, "%04x:%06x:%06x", my_dest->lid, my_dest->qpn, my_dest->psn);
      if (write(connfd, msg, sizeof msg) != sizeof msg) {
            fprintf(stderr, "Couldn't send local address\n");
            free(rem_dest);
            rem_dest = NULL;
            goto out;
      }

      read(connfd, msg, sizeof msg);

out:
      close(connfd);
      return rem_dest;
}

int main(int argv, char *argc[]) {
	struct ibv_device      **dev_list;
	// we will use the first device
	struct ibv_device *ib_dev;
	// the context to be used
	struct stream_context *ctx;
	// size of the buffer
	int size = 4096;
	// system page size
	int page_size = sysconf(_SC_PAGESIZE);
	// port
	int port = 10456;
	// number of retries made
	int routs;
	// server name
	char *servername = argc[0];
	// MTU for ib
	enum ibv_mtu mtu = IBV_MTU_1024;

	// self destination identifiers
	struct stream_dest     self_dest;
	// remote destination identifiers
	struct stream_dest    *rem_dest;

	// timing
	struct timeval start, end;
	// receive count and send count
	int rcnt, scnt;
	// no of iterations
	int iters = 1000;
	int num_cq_events = 0;
	// get the device list
	dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
	    fprintf(stderr, "No IB devices found\n");
		return 1;
	}

	// get the first device
	ib_dev = *dev_list;
	if (!ib_dev) {
		  fprintf(stderr, "No IB devices found\n");
		  return 1;
	}

	struct stream_cfg cfg;
	cfg.buff_size = size;
	cfg.use_event = 1;
	cfg.rx_depth = 1;
	cfg.port = port;

	ctx = stream_init_ctx(ib_dev, size, port, page_size, &cfg);
    if (!ctx){
		  fprintf(stderr, "Couldn't initialize IB\n");
		  return 1;
	}

    routs = stream_post_recv(ctx, ctx->rx_depth);
	if (routs < ctx->rx_depth) {
		fprintf(stderr, "Couldn't post receive (%d)\n", routs);
		return 1;
	}

	if (cfg.use_event) {
		if (ibv_req_notify_cq(ctx->cq, 0)) {
			fprintf(stderr, "Couldn't request CQ notification\n");
			return 1;
		}
	}

	self_dest.lid = stream_get_local_lid(ctx->context, port);
	self_dest.qpn = ctx->qp->qp_num;
	self_dest.psn = lrand48() & 0xffffff;
	if (!self_dest.lid) {
		fprintf(stderr, "Couldn't get local LID\n");
		return 1;
	}

	printf("  local address:  LID 0x%04x, QPN 0x%06x, PSN 0x%06x\n",
			self_dest.lid, self_dest.qpn, self_dest.psn);

	if (servername) {
		rem_dest = stream_client_exch_dest(servername, port, &self_dest);
	} else {
		rem_dest = stream_server_exch_dest(ctx, port, mtu, port, &self_dest);
	}

	if (!rem_dest) {
		fprintf(stderr, "Couldn't get remote destination\n");
		return 1;
	}

	printf("  remote address: LID 0x%04x, QPN 0x%06x, PSN 0x%06x\n",
	             rem_dest->lid, rem_dest->qpn, rem_dest->psn);

	if (servername) {
		if (stream_connect_ctx(ctx, port, self_dest.psn, mtu, rem_dest)) {
			  return 1;
		}
	}

	ctx->pending = STREAM_RECV_WRID;

	if (servername) {
		if (stream_post_send(ctx)) {
			  fprintf(stderr, "Couldn't post send\n");
			  return 1;
		}
		ctx->pending |= STREAM_SEND_WRID;
	}

	if (gettimeofday(&start, NULL)) {
		perror("gettimeofday");
		return 1;
	}

	while (rcnt < iters || scnt < iters) {
		if (cfg.use_event) {
			struct ibv_cq *ev_cq;
			void          *ev_ctx;

			if (ibv_get_cq_event(ctx->channel, &ev_cq, &ev_ctx)) {
				fprintf(stderr, "Failed to get cq_event\n");
				return 1;
			}

			++num_cq_events;

			if (ev_cq != ctx->cq) {
				fprintf(stderr, "CQ event for unknown CQ %p\n", ev_cq);
				return 1;
			}

			if (ibv_req_notify_cq(ctx->cq, 0)) {
				fprintf(stderr, "Couldn't request CQ notification\n");
				return 1;
			}
		}

		{
			struct ibv_wc wc[2];
			int ne, i;
			do {
				ne = ibv_poll_cq(ctx->cq, 2, wc);
				if (ne < 0) {
					  fprintf(stderr, "poll CQ failed %d\n", ne);
					  return 1;
				}
			} while (!cfg.use_event && ne < 1);

			for (i = 0; i < ne; ++i) {
				if (wc[i].status != IBV_WC_SUCCESS) {
					  fprintf(stderr, "Failed status %d for wr_id %d\n",
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
								  fprintf(stderr,
										"Couldn't post receive (%d)\n",
										routs);
								  return 1;
							}
					  }

					  ++rcnt;
					  break;

				default:
					  fprintf(stderr, "Completion for unknown wr_id %d\n",
							(int) wc[i].wr_id);
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
		}
	}

	if (gettimeofday(&end, NULL)) {
		perror("gettimeofday");
		return 1;
	}

	{
		float usec = (end.tv_sec - start.tv_sec) * 1000000 +
			  (end.tv_usec - start.tv_usec);
		long long bytes = (long long) size * iters * 2;

		printf("%lld bytes in %.2f seconds = %.2f Mbit/sec\n",
			   bytes, usec / 1000000., bytes * 8. / usec);
		printf("%d iters in %.2f seconds = %.2f usec/iter\n",
			   iters, usec / 1000000., usec / iters);
	}

	ibv_ack_cq_events(ctx->cq, num_cq_events);

	if (stream_close_ctx(ctx)) {
		return 1;
	}

	ibv_free_device_list(dev_list);
	free(rem_dest);
	return 0;
}
