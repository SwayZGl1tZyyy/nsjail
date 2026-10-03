#include "route.h"

#include <errno.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <netinet/in.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

namespace nstun {

static RouteLocality route_locality(int family, const void* dst, size_t dst_len) {
	int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
	if (fd == -1) {
		return RouteLocality::ERROR;
	}

	struct Request {
		struct nlmsghdr nlh;
		struct rtmsg rtm;
		char attrs[RTA_SPACE(sizeof(struct in6_addr))];
	} req = {};

	req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
	req.nlh.nlmsg_type = RTM_GETROUTE;
	req.nlh.nlmsg_flags = NLM_F_REQUEST;
	req.nlh.nlmsg_seq = 1;
	req.rtm.rtm_family = family;
	req.rtm.rtm_dst_len = family == AF_INET ? 32 : 128;

	struct rtattr* rta = reinterpret_cast<struct rtattr*>(
	    reinterpret_cast<char*>(&req) + NLMSG_ALIGN(req.nlh.nlmsg_len));
	rta->rta_type = RTA_DST;
	rta->rta_len = RTA_LENGTH(dst_len);
	memcpy(RTA_DATA(rta), dst, dst_len);
	req.nlh.nlmsg_len = NLMSG_ALIGN(req.nlh.nlmsg_len) + RTA_LENGTH(dst_len);

	struct sockaddr_nl kernel = {};
	kernel.nl_family = AF_NETLINK;

	if (sendto(fd, &req, req.nlh.nlmsg_len, 0, reinterpret_cast<struct sockaddr*>(&kernel),
		sizeof(kernel)) == -1) {
		close(fd);
		return RouteLocality::ERROR;
	}

	char buf[8192];
	for (;;) {
		ssize_t len = recv(fd, buf, sizeof(buf), 0);
		if (len == -1) {
			if (errno == EINTR) {
				continue;
			}
			close(fd);
			return RouteLocality::ERROR;
		}
		if (len == 0) {
			close(fd);
			return RouteLocality::ERROR;
		}

		for (struct nlmsghdr* nlh = reinterpret_cast<struct nlmsghdr*>(buf);
		    NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
			if (nlh->nlmsg_seq != req.nlh.nlmsg_seq) {
				continue;
			}
			if (nlh->nlmsg_type == NLMSG_ERROR) {
				const struct nlmsgerr* err =
				    reinterpret_cast<const struct nlmsgerr*>(NLMSG_DATA(nlh));
				int code = -err->error;
				close(fd);
				if (err->error == 0 || code == ENETUNREACH ||
				    code == EHOSTUNREACH || code == ESRCH) {
					return RouteLocality::NOT_LOCAL;
				}
				return RouteLocality::ERROR;
			}
			if (nlh->nlmsg_type == RTM_NEWROUTE) {
				const struct rtmsg* route =
				    reinterpret_cast<const struct rtmsg*>(NLMSG_DATA(nlh));
				RouteLocality result = route->rtm_type == RTN_LOCAL
							   ? RouteLocality::LOCAL
							   : RouteLocality::NOT_LOCAL;
				close(fd);
				return result;
			}
			if (nlh->nlmsg_type == NLMSG_DONE) {
				close(fd);
				return RouteLocality::NOT_LOCAL;
			}
		}
	}
}

RouteLocality route_locality4(uint32_t dst) {
	return route_locality(AF_INET, &dst, sizeof(dst));
}

RouteLocality route_locality6(const uint8_t dst[16]) {
	return route_locality(AF_INET6, dst, 16);
}

} /* namespace nstun */
