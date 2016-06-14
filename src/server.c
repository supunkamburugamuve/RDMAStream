#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <stdlib.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/param.h>
#include <pthread.h>

#include "stream.h"

#define MAX_CONNECTIONS 100

int stream_process_messages(struct stream_connect_cfg *cfg, struct stream_connect_ctx *ctx);

struct stream_connections {
	struct stream_connect_ctx *ctxs[MAX_CONNECTIONS];
	int count;
};

struct stream_tcp_server_info {
	struct stream_connect_cfg *cfg;
	struct stream_connections *conns;
};

struct stream_tcp_server_worker_info {
	struct stream_connect_cfg *cfg;
	struct stream_connect_ctx *context;
};

void *stream_tcp_server_worker_thread(void *thread) {
	struct stream_tcp_server_worker_info *tcp_worker = (struct stream_tcp_server_worker_info *) thread;
	struct stream_connect_cfg *cfg = tcp_worker->cfg;
	struct stream_connect_ctx *ctx = tcp_worker->context;

	printf("Start processing the request\n");
	stream_process_messages(cfg, ctx);
	return NULL;
}

/**
 * Read incoming TCP messages and create verbs connections to clients
 */
void *stream_tcp_server_thread(void *thread) {
	struct stream_tcp_server_info *tcp_server = (struct stream_tcp_server_info *) thread;
	struct stream_connect_cfg *cfg = tcp_server->cfg;

	struct addrinfo *res, *t;
	struct addrinfo hints = {
			.ai_flags    = AI_PASSIVE,
			.ai_family   = AF_INET,
			.ai_socktype = SOCK_STREAM
	};

	char *service;
	int n;
	int sockfd = -1;

	if (asprintf(&service, "%d", cfg->port) < 0) {
		return NULL;
	}

	n = getaddrinfo(NULL, service, &hints, &res);

	if (n < 0) {
		fprintf(stderr, "%s for port %d\n", gai_strerror(n), cfg->port);
		free(service);
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
	free(service);

	if (sockfd < 0) {
		fprintf(stderr, "Couldn't listen to port %d\n", cfg->port);
		return NULL;
	}

	listen(sockfd, 1);
	//char msg[sizeof "0000:000000:000000:00000000000000000000000000000000"];
	int buf_size = sizeof(struct stream_connect_message);
	uint8_t *buf = (uint8_t *)malloc(buf_size);
	struct stream_connect_message conn_msg;
	while (1) {

		int n;
		int connfd;
		struct stream_dest *rem_dest = NULL;
		// char gid[33];
		struct stream_connect_ctx *ctx;

		connfd = accept(sockfd, NULL, 0);
		if (connfd < 0) {
			fprintf(stderr, "accept() failed\n");
			return NULL;
		}
		printf("Wait for new connection:\n");
		n = read(connfd, buf, buf_size);
		if (n != sizeof buf_size) {
			perror("server read");
			fprintf(stderr, "%d/%d: Couldn't read remote address\n", n, buf_size);
			goto out;
		}

		rem_dest = malloc(sizeof *rem_dest);
		if (!rem_dest) {
			goto out;
		}

		//sscanf(msg, "%x:%x:%x:%s", &rem_dest->lid, &rem_dest->qpn, &rem_dest->psn, gid);
		stream_connect_message_copy_from_buffer(buf, &conn_msg);
		//wire_gid_to_gid(conn_msg.dest.gid, &conn_msg->dest.gid);
		stream_dest_message_to_dest(&conn_msg.dest, rem_dest);

		printf("Connect context:\n");
		ctx = stream_process_connect_request(cfg, rem_dest);
		if (!ctx) {
			printf("Failed to connect context: \n");
		}
		printf("Connected context:\n");

	    int routs = stream_post_recv(ctx, ctx->rx_depth);
		if (routs < ctx->rx_depth) {
			fprintf(stderr, "Couldn't post receive (%d)\n", routs);
			goto out;
		}

		stream_dest_to_dest_message(&ctx->self_dest, &conn_msg.dest);
		// sprintf(msg, "%04x:%06x:%06x:%s", ctx->self_dest.lid, ctx->self_dest.qpn, ctx->self_dest.psn, gid);
		stream_connect_message_copy_to_buffer(&conn_msg, buf);
		if (write(connfd, buf, buf_size) != buf_size) {
			fprintf(stderr, "Couldn't send local address\n");
			free(rem_dest);
			rem_dest = NULL;
			goto out;
		}

		printf("Connected context 2 \n");
		read(connfd, buf, buf_size);

		pthread_t worker_thread;
		struct stream_tcp_server_worker_info * worker_ctx = calloc(1, sizeof (struct stream_tcp_server_worker_info));
		if (!worker_ctx) {
			return NULL;
		}
		worker_ctx->cfg = cfg;
		worker_ctx->context = ctx;

		// start the TCP server thread for accepting incoming communications
		if (pthread_create(&worker_thread, NULL, stream_tcp_server_worker_thread,
				(void *) worker_ctx)) {
			fprintf(stderr, "Couldn't create worker thread\n");
			goto out;
		}

		out:
		close(connfd);
	}
}

int stream_process_messages(struct stream_connect_cfg *cfg, struct stream_connect_ctx *ctx) {
	struct timeval start, end;

	int iters = 1000;
	int routs = ctx->rx_depth;
	int rcnt, scnt;
	int num_cq_events = 0;

    printf("steram process messages \n");
	ctx->pending = STREAM_RECV_WRID;

	if (gettimeofday(&start, NULL)) {
		perror("gettimeofday");
		return 1;
	}

	rcnt = scnt = 0;
	while (rcnt < iters || scnt < iters) {
		if (cfg->use_event) {
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

			} while (!cfg->use_event && ne < 1);

			for (i = 0; i < ne; ++i) {
				if (wc[i].status != IBV_WC_SUCCESS) {
					fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
							ibv_wc_status_str(wc[i].status),
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
		long long bytes = (long long) cfg->size * iters * 2;

		printf("%lld bytes in %.2f seconds = %.2f Mbit/sec\n",
				bytes, usec / 1000000., bytes * 8. / usec);
		printf("%d iters in %.2f seconds = %.2f usec/iter\n",
				iters, usec / 1000000., usec / iters);
	}

	ibv_ack_cq_events(ctx->cq, num_cq_events);

	if (stream_close_ctx(ctx))
		return 1;

	return 0;
}

static struct stream_dest *stream_server_exch_dest(struct stream_connect_cfg *cfg,
		struct stream_connect_ctx *ctx,
		int ib_port, enum ibv_mtu mtu,
		int port, int sl,
		const struct stream_dest *my_dest,
		int sgid_idx) {
	struct addrinfo *res, *t;
	struct addrinfo hints = {
			.ai_flags    = AI_PASSIVE,
			.ai_family   = AF_INET,
			.ai_socktype = SOCK_STREAM
	};
	char *service;
	char msg[sizeof "0000:000000:000000:00000000000000000000000000000000"];
	int n;
	int sockfd = -1, connfd;
	struct stream_dest *rem_dest = NULL;
	char gid[33];

	if (asprintf(&service, "%d", port) < 0)
		return NULL;

	n = getaddrinfo(NULL, service, &hints, &res);

	if (n < 0) {
		fprintf(stderr, "%s for port %d\n", gai_strerror(n), port);
		free(service);
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
	free(service);

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

	rem_dest = malloc(sizeof *rem_dest);
	if (!rem_dest)
		goto out;

	sscanf(msg, "%x:%x:%x:%s", &rem_dest->lid, &rem_dest->qpn, &rem_dest->psn, gid);
	wire_gid_to_gid(gid, &rem_dest->gid);

	if (stream_connect_ctx(cfg, ctx)) {
		fprintf(stderr, "Couldn't connect to remote QP\n");
		free(rem_dest);
		rem_dest = NULL;
		goto out;
	}

	gid_to_wire_gid(&my_dest->gid, gid);
	sprintf(msg, "%04x:%06x:%06x:%s", my_dest->lid, my_dest->qpn, my_dest->psn, gid);
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

static void usage(const char *argv0){
	printf("Usage:\n");
	printf("  %s            start a server and wait for connection\n", argv0);
	printf("  %s <host>     connect to server at <host>\n", argv0);
	printf("\n");
	printf("Options:\n");
	printf("  -p, --port=<port>      listen on/connect to port <port> (default 18515)\n");
	printf("  -d, --ib-dev=<dev>     use IB device <dev> (default first device found)\n");
	printf("  -i, --ib-port=<port>   use port <port> of IB device (default 1)\n");
	printf("  -s, --size=<size>      size of message to exchange (default 4096)\n");
	printf("  -m, --mtu=<size>       path MTU (default 1024)\n");
	printf("  -r, --rx-depth=<dep>   number of receives to post at a time (default 500)\n");
	printf("  -n, --iters=<iters>    number of exchanges (default 1000)\n");
	printf("  -l, --sl=<sl>          service level value\n");
	printf("  -e, --events           sleep on CQ events (default poll)\n");
	printf("  -g, --gid-idx=<gid index> local port gid index\n");
}

int main(int argc, char *argv[]) {

	struct stream_connect_ctx *ctx;

	int num_cq_events = 0;
	int	gidx = -1;
	//char gid[33];
	int iters;
	// server thread
	pthread_t server_thread;

	struct stream_connect_cfg *cfg;
	cfg = calloc(1, sizeof (struct stream_connect_cfg));
	if (!cfg) {
		return 1;
	}
	stream_init_cfg(cfg);

	ctx = calloc(1, sizeof (struct stream_connect_ctx));
	if (!ctx) {
		return 1;
	}
	srand48(getpid() * time(NULL));

	while (1) {
		int c;

		static struct option long_options[] = {
				{ .name = "port",     .has_arg = 1, .val = 'p' },
				{ .name = "ib-dev",   .has_arg = 1, .val = 'd' },
				{ .name = "ib-port",  .has_arg = 1, .val = 'i' },
				{ .name = "size",     .has_arg = 1, .val = 's' },
				{ .name = "mtu",      .has_arg = 1, .val = 'm' },
				{ .name = "rx-depth", .has_arg = 1, .val = 'r' },
				{ .name = "iters",    .has_arg = 1, .val = 'n' },
				{ .name = "sl",       .has_arg = 1, .val = 'l' },
				{ .name = "events",   .has_arg = 0, .val = 'e' },
				{ .name = "gid-idx",  .has_arg = 1, .val = 'g' },
				{ 0 }
		};

		c = getopt_long(argc, argv, "p:d:i:s:m:r:n:l:eg:", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'p':
			cfg->port = strtol(optarg, NULL, 0);
			if (cfg->port < 0 || cfg->port > 65535) {
				usage(argv[0]);
				return 1;
			}
			break;

		case 'd':
			cfg->ib_devname = strdup(optarg);
			break;

		case 'i':
			cfg->ib_port = strtol(optarg, NULL, 0);
			if (cfg->ib_port < 0) {
				usage(argv[0]);
				return 1;
			}
			break;

		case 's':
			cfg->size = strtol(optarg, NULL, 0);
			break;

		case 'm':
			cfg->mtu = stream_mtu_to_enum(strtol(optarg, NULL, 0));
			if (cfg->mtu < 0) {
				usage(argv[0]);
				return 1;
			}
			break;

		case 'r':
			cfg->rx_depth = strtol(optarg, NULL, 0);
			break;

		case 'n':
			iters = strtol(optarg, NULL, 0);
			break;

		case 'l':
			cfg->sl = strtol(optarg, NULL, 0);
			break;

		case 'e':
			++cfg->use_event;
			break;

		case 'g':
			gidx = strtol(optarg, NULL, 0);
			break;

		default:
			usage(argv[0]);
			return 1;
		}
	}
	cfg->page_size = sysconf(_SC_PAGESIZE);

	struct stream_tcp_server_info *tcp_server;
	tcp_server = calloc(1, sizeof (struct stream_tcp_server_info));
	if (!tcp_server) {
		return 1;
	}

	tcp_server->cfg = cfg;
	// start the TCP server thread for accepting incoming communications
	if (pthread_create(&server_thread, NULL, stream_tcp_server_thread,
	        (void *) tcp_server)) {

	}

	// wait until the tcp thread finishes
	pthread_join(server_thread, NULL);

	return 0;
}
