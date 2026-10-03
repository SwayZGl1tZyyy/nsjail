#include <arpa/inet.h>
#include <assert.h>
#include <stdint.h>

#include "nstun/route.h"

int main() {
	uint32_t loopback4;
	assert(inet_pton(AF_INET, "127.0.0.1", &loopback4) == 1);
	assert(nstun::route_locality4(loopback4) == nstun::RouteLocality::LOCAL);
	return 0;
}
