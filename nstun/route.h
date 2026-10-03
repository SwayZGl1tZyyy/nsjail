#ifndef NSTUN_ROUTE_H_
#define NSTUN_ROUTE_H_

#include <stdint.h>

namespace nstun {

enum class RouteLocality : uint8_t {
	NOT_LOCAL,
	LOCAL,
	ERROR,
};

RouteLocality route_locality4(uint32_t dst);
RouteLocality route_locality6(const uint8_t dst[16]);

} /* namespace nstun */

#endif /* NSTUN_ROUTE_H_ */
