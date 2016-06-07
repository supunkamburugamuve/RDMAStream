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

#include "stream.h"

static struct stream_dest *stream_client_exch_dest(const char *servername, int port,
						 const struct stream_dest *my_dest) {
	struct addrinfo *res, *t;
	struct addrinfo hints = {
		.ai_family   = AF_INET,
		.ai_socktype = SOCK_STREAM
	};
	char *service;
	char msg[sizeof "0000:000000:000000:00000000000000000000000000000000"];
	int n;
	int sockfd = -1;
	struct stream_dest *rem_dest = NULL;
	char gid[33];

	if (asprintf(&service, "%d", port) < 0)
		return NULL;

	n = getaddrinfo(servername, service, &hints, &res);

	if (n < 0) {
		fprintf(stderr, "%s for %s:%d\n", gai_strerror(n), servername, port);
		free(service);
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
	free(service);

	if (sockfd < 0) {
		fprintf(stderr, "Couldn't connect to %s:%d\n", servername, port);
		return NULL;
	}

	gid_to_wire_gid(&my_dest->gid, gid);
	sprintf(msg, "%04x:%06x:%06x:%s", my_dest->lid, my_dest->qpn, my_dest->psn, gid);
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

	rem_dest = malloc(sizeof *rem_dest);
	if (!rem_dest)
		goto out;

	sscanf(msg, "%x:%x:%x:%s", &rem_dest->lid, &rem_dest->qpn, &rem_dest->psn, gid);
	wire_gid_to_gid(gid, &rem_dest->gid);

out:
	close(sockfd);
	return rem_dest;
}

static struct stream_dest *stream_server_exch_dest(struct stream_context *ctx,
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

	if (stream_connect_ctx(ctx, ib_port, my_dest->psn, mtu, sl, rem_dest, sgid_idx)) {
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


static void usage(const char *argv0) {
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
	struct stream_context *ctx;
	struct stream_dest my_dest;
	struct stream_dest *rem_dest;
	struct timeval start, end;

	int iters = 1000;
	int routs;
	int rcnt, scnt;
	int num_cq_events = 0;
	int	gidx = -1;
	char gid[33];

	struct stream_cfg cfg;
	ctx = calloc(1, sizeof *ctx);
	if (!ctx) {
		return 1;
	}
	stream_init_cfg(&cfg);

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
			cfg.port = strtol(optarg, NULL, 0);
			if (cfg.port < 0 || cfg.port > 65535) {
				usage(argv[0]);
				return 1;
			}
			break;

		case 'd':
			cfg.ib_devname = strdup(optarg);
			break;

		case 'i':
			cfg.ib_port = strtol(optarg, NULL, 0);
			if (cfg.ib_port < 0) {
				usage(argv[0]);
				return 1;
			}
			break;

		case 's':
			cfg.size = strtol(optarg, NULL, 0);
			break;

		case 'm':
			cfg.mtu = stream_mtu_to_enum(strtol(optarg, NULL, 0));
			if (cfg.mtu < 0) {
				usage(argv[0]);
				return 1;
			}
			break;

		case 'r':
			cfg.rx_depth = strtol(optarg, NULL, 0);
			break;

		case 'n':
			iters = strtol(optarg, NULL, 0);
			break;

		case 'l':
			cfg.sl = strtol(optarg, NULL, 0);
			break;

		case 'e':
			++cfg.use_event;
			break;

		case 'g':
			gidx = strtol(optarg, NULL, 0);
			break;

		default:
			usage(argv[0]);
			return 1;
		}
	}

	if (optind == argc - 1)
		cfg.servername = strdup(argv[optind]);
	else if (optind < argc) {
		usage(argv[0]);
		return 1;
	}

	page_size = sysconf(_SC_PAGESIZE);

	if (stream_assign_device(&cfg, ctx)) {
		fprintf(stderr, "Failed to get infiniband device\n");
		return 1;
	}

	if (stream_init_ctx(&cfg, ctx, cfg.size, cfg.rx_depth, cfg.ib_port, cfg.use_event, !cfg.servername, page_size)) {
		fprintf(stderr, "Failed to initialize context\n");
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

	if (stream_get_port_info(ctx->context, cfg.ib_port, &ctx->portinfo)) {
		fprintf(stderr, "Couldn't get port info\n");
		return 1;
	}

	ctx->self_dest.lid = ctx->portinfo.lid;
	if (ctx->portinfo.link_layer == IBV_LINK_LAYER_INFINIBAND && !ctx->self_dest.lid) {
		fprintf(stderr, "Couldn't get local LID\n");
		return 1;
	}

	if (cfg.gidx >= 0) {
		if (ibv_query_gid(ctx->context, cfg.ib_port, cfg.gidx, &ctx->self_dest.gid)) {
			fprintf(stderr, "Could not get local gid for gid index %d\n", cfg.gidx);
			return 1;
		}
	} else {
		memset(&ctx->self_dest.gid, 0, sizeof ctx->self_dest.gid);
	}

	ctx->self_dest.qpn = ctx->qp->qp_num;
	ctx->self_dest.psn = lrand48() & 0xffffff;

	my_dest.qpn = ctx->qp->qp_num;
	my_dest.psn = lrand48() & 0xffffff;
	inet_ntop(AF_INET6, &my_dest.gid, gid, sizeof gid);
	printf("  local address:  LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s\n",
			my_dest.lid, my_dest.qpn, my_dest.psn, gid);


	if (cfg.servername)
		rem_dest = stream_client_exch_dest(cfg.servername, cfg.port, &my_dest);
	else
		rem_dest = stream_server_exch_dest(ctx, cfg.ib_port, cfg.mtu, cfg.port, cfg.sl, &my_dest, gidx);

	if (!rem_dest) {
		return 1;
	}

	inet_ntop(AF_INET6, &rem_dest->gid, gid, sizeof gid);
	printf("  remote address: LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s\n",
			rem_dest->lid, rem_dest->qpn, rem_dest->psn, gid);

	if (cfg.servername)
		if (stream_connect_ctx(ctx, cfg.ib_port, my_dest.psn, cfg.mtu, cfg.sl, rem_dest, gidx))
			return 1;

	ctx->pending = STREAM_RECV_WRID;

	if (cfg.servername) {
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

	rcnt = scnt = 0;
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
		long long bytes = (long long) cfg.size * iters * 2;

		printf("%lld bytes in %.2f seconds = %.2f Mbit/sec\n",
				bytes, usec / 1000000., bytes * 8. / usec);
		printf("%d iters in %.2f seconds = %.2f usec/iter\n",
				iters, usec / 1000000., usec / iters);
	}

	ibv_ack_cq_events(ctx->cq, num_cq_events);

	if (stream_close_ctx(ctx))
		return 1;

	free(rem_dest);

	return 0;
}
