#include "pingpong.h"
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum ibv_mtu pp_mtu_to_enum(int mtu) {
	switch (mtu) {
	case 256:  return IBV_MTU_256;
	case 512:  return IBV_MTU_512;
	case 1024: return IBV_MTU_1024;
	case 2048: return IBV_MTU_2048;
	case 4096: return IBV_MTU_4096;
	default:   return -1;
	}
}

uint16_t pp_get_local_lid(struct ibv_context *context, int port) {
	struct ibv_port_attr attr;

	if (ibv_query_port(context, port, &attr))
		return 0;

	return attr.lid;
}

int pp_get_port_info(struct ibv_context *context, int port,
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
