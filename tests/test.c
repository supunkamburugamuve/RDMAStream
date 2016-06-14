
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct test {
	uint8_t one;
	uint16_t two;
	uint32_t three;
};

int main() {
	uint8_t *buf = (uint8_t *)malloc(sizeof(struct test));
	struct test t1 = {
			.one = 1,
			.two = 2,
			.three = 3,
	};
	memcpy(buf, (uint8_t *)&t1, sizeof (struct test));

	struct test t2;

	memcpy(&t2, (struct test *)buf, sizeof (struct test));

	printf("one %d t %d thre %d\n", t2.one, t2.two, t2.three);
	return 0;
}

