#include <stdio.h>
#include <stdint.h>
#include <byteswap.h>

uint32_t fill_ip(uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4) {
  uint32_t tmp = 0;
  tmp |= b1;
  tmp |= b2 << 8;
  tmp |= b3 << 16;
  tmp |= b4 << 24;
  return tmp;
}

int main(int argc, const char **argv) {
	if (argc < 3) {
		printf("usage: %s <ipv4 dot not addr> <port>\n", argv[0]);
		return 1;
	}
	uint8_t o1, o2, o3, o4;
	uint16_t port;
	sscanf(argv[1], "%hhu.%hhu.%hhu.%hhu", &o1, &o2, &o3, &o4);
	sscanf(argv[2], "%hu", &port);
	uint32_t ip = fill_ip(o1, o2, o3, o4);
	port = bswap_16(port);
	printf("-DADDR=0x%X -DPORT=%u", ip, port);
	return 0;
}
