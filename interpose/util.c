#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <err.h>

void send_all(int sockfd, void *buf, size_t buf_size) {
	size_t total = 0;
	// keep sending until all bytes are sent
	while (total < buf_size) {
		ssize_t cur = send(sockfd, buf + total, buf_size - total, 0);
		if (cur < 0) err(1, 0); // exit on error
		total += cur;
	}
}

void recv_all(int sockfd, void *buf, size_t buf_size) {
	size_t total = 0;
	// keep receiving until all bytes are received
	while (total < buf_size) {
		ssize_t cur = recv(sockfd, buf + total, buf_size - total, 0);
		if (cur < 0) err(1, 0); // exit on error
		total += cur;
	}
}