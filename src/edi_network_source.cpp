/*
    DABlin - capital DAB experience

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "edi_network_source.h"

#include <cerrno>
#include <cstring>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>


// --- EDITCPSource -----------------------------------------------------------------
EDITCPSource::EDITCPSource(std::string host, int port, EnsembleSourceObserver *observer) :
		EDISource("tcp://" + host + ":" + std::to_string(port), observer),
		host(host), port(port) {
}

void EDITCPSource::Init() {
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo *res = nullptr;
	int gai_result = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
	if(gai_result) {
		fprintf(stderr, "EDITCPSource: error resolving '%s': %s\n", host.c_str(), gai_strerror(gai_result));
		return;
	}

	int sock = -1;
	for(struct addrinfo *rp = res; rp; rp = rp->ai_next) {
		sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if(sock == -1)
			continue;
		if(connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
			break;
		close(sock);
		sock = -1;
	}
	freeaddrinfo(res);

	if(sock == -1) {
		fprintf(stderr, "EDITCPSource: error connecting to '%s:%d': %s\n", host.c_str(), port, strerror(errno));
		return;
	}

	input_file = fdopen(sock, "rb");
	if(!input_file) {
		perror("EDITCPSource: error wrapping socket");
		close(sock);
	}
}

void EDITCPSource::PrintSource() {
	fprintf(stderr, "EDITCPSource: reading EDI from tcp://%s:%d\n", host.c_str(), port);
}


// --- EDIUDPSource -----------------------------------------------------------------
EDIUDPSource::EDIUDPSource(std::string bind_addr, int port, EnsembleSourceObserver *observer) :
		EnsembleSource("udp://" + (bind_addr.empty() ? std::string("0.0.0.0") : bind_addr) + ":" + std::to_string(port), observer, "EDI", 65536),
		bind_addr(bind_addr), port(port) {
}

int EDIUDPSource::Main() {
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if(sock == -1) {
		perror("EDIUDPSource: error creating socket");
		return 1;
	}

	int reuse = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	bool is_multicast = false;
	struct in_addr mcast_test_addr;
	if(!bind_addr.empty() && inet_pton(AF_INET, bind_addr.c_str(), &mcast_test_addr) == 1)
		is_multicast = IN_MULTICAST(ntohl(mcast_test_addr.s_addr));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if(bind(sock, (struct sockaddr*) &addr, sizeof(addr)) == -1) {
		fprintf(stderr, "EDIUDPSource: error binding to UDP port %d: %s\n", port, strerror(errno));
		close(sock);
		return 1;
	}

	if(is_multicast) {
		struct ip_mreq mreq;
		memset(&mreq, 0, sizeof(mreq));
		inet_pton(AF_INET, bind_addr.c_str(), &mreq.imr_multiaddr);
		mreq.imr_interface.s_addr = htonl(INADDR_ANY);
		if(setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == -1) {
			fprintf(stderr, "EDIUDPSource: error joining multicast group %s: %s\n", bind_addr.c_str(), strerror(errno));
			close(sock);
			return 1;
		}
	}

	fprintf(stderr, "EDIUDPSource: receiving EDI from udp://%s:%d%s\n",
			bind_addr.empty() ? "0.0.0.0" : bind_addr.c_str(), port, is_multicast ? " (multicast)" : "");

	int old_flags = fcntl(sock, F_GETFL);
	fcntl(sock, F_SETFL, (old_flags == -1 ? 0 : old_flags) | O_NONBLOCK);

	std::vector<uint8_t> buf(65536);

	fd_set fds;
	timeval select_timeval;

	while(!do_exit) {
		observer->EnsembleDoRegularWork();

		FD_ZERO(&fds);
		FD_SET(sock, &fds);
		select_timeval.tv_sec = 0;
		select_timeval.tv_usec = 100 * 1000;

		int ready_fds = select(sock + 1, &fds, nullptr, nullptr, &select_timeval);
		if(ready_fds == -1) {
			if(errno != EINTR)
				perror("EDIUDPSource: error while select");
			continue;
		}
		if(!(ready_fds && FD_ISSET(sock, &fds)))
			continue;

		ssize_t bytes = recv(sock, &buf[0], buf.size(), 0);
		if(bytes <= 0) {
			if(bytes == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
				perror("EDIUDPSource: error while recv");
			continue;
		}

		// a datagram is either one complete "AF"-prefixed AF packet, or a "PF"-prefixed
		// PFT fragment (ETSI TS 102 821 Annex B) that needs reassembling across datagrams
		if(bytes >= 2 && buf[0] == 'P' && buf[1] == 'F') {
			PFTFragment frag;
			if(!PFTFragment::Parse(&buf[0], bytes, frag)) {
				fprintf(stderr, "EDIUDPSource: ignored malformed PFT fragment\n");
				continue;
			}

			std::vector<uint8_t> af_packet;
			if(pft.PushFragment(frag, af_packet)) {
				ensemble_frames_count++;
				observer->EnsembleProcessFrame(&af_packet[0]);
			}
			continue;
		}

		if(bytes < 10 || !(buf[0] == 'A' && buf[1] == 'F')) {
			fprintf(stderr, "EDIUDPSource: ignored %zd byte datagram without AF/PF sync\n", bytes);
			continue;
		}

		size_t len = buf[2] << 24 | buf[3] << 16 | buf[4] << 8 | buf[5];
		if(10 + len + 2 != (size_t) bytes) {
			fprintf(stderr, "EDIUDPSource: ignored AF packet with unexpected size (expected %zu, got %zd bytes)\n", 10 + len + 2, bytes);
			continue;
		}

		ensemble_frames_count++;
		observer->EnsembleProcessFrame(&buf[0]);
	}

	close(sock);
	return 0;
}
