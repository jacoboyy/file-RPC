#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <stdbool.h>
#include <sys/errno.h>
#include <fcntl.h>

extern int errno;

void send_all(int sockfd, void *buf, size_t buf_size) {
	size_t total = 0;
	while (total < buf_size) {
		ssize_t cur = send(sockfd, buf + total, buf_size - total, 0);
		if (cur < 0) err(1, 0);
		total += cur;
	}
	// fprintf(stderr, "server sent %zu bytes\n", buf_size);
}

void recv_all(int sockfd, void *buf, size_t buf_size) {
	size_t total = 0;
	while (total < buf_size) {
		ssize_t cur = recv(sockfd, buf + total, buf_size - total, 0);
		if (cur < 0) err(1, 0);
		total += cur;
	}
	// fprintf(stderr, "server received %zu bytes\n", buf_size);
}

void rpc_open(int sessfd, char* stub) {
	int flags = *(int *)(stub + sizeof(int));
	mode_t mode = *(mode_t *)(stub + 2 * sizeof(int));
	char *pathname = stub + 2 * sizeof(int) + sizeof(mode_t);
	
	// perform a local open
	int fd = open(pathname, flags, mode);
	fprintf(stderr, "rpc_open file %s and fd is %d\n", pathname, fd);
	// send back the file descriptor
	send_all(sessfd, &fd, sizeof(int));
	// send back errno on failure
	if (fd < 0) send_all(sessfd, &errno, sizeof(int));
}

void rpc_close(int sessfd, char* stub) {
	int fd = *(int *)(stub + sizeof(int));
	fprintf(stderr, "rpc_close called for remote fd %d\n", fd);
	// perform a local close
	int res = close(fd);
	// send back the result
	send_all(sessfd, &res, sizeof(int));
	// send back errno on failure
	if (res < 0) {
		send_all(sessfd, &errno, sizeof(int));
		fprintf(stderr, "close fd %d failed!\n", fd);
	} else {
		fprintf(stderr, "close fd %d succeed!\n", fd);
	}
}

void rpc_write(int sessfd, char* stub){
	int fd = *(int *)(stub + sizeof(int));
	size_t count = *(size_t *)(stub + 2 * sizeof(int));
	// fprintf(stderr, "rpc_write %zu bytes to fd %d\n", count, fd);
	void *buf = stub + 2 * sizeof(int) + sizeof(size_t);

	// perform a local write
	ssize_t res = write(fd, buf, count);

	// send back results
	send_all(sessfd, &res, sizeof(ssize_t));
	// send back errno on failure
	if (res < 0) send_all(sessfd, &errno, sizeof(int));
}

int main(int argc, char**argv) {
	char *serverport;
	unsigned short port;
	int sockfd, sessfd, rv;
	struct sockaddr_in srv, cli;
	socklen_t sa_size;
	
	// Get environment variable indicating the port of the server
	serverport = getenv("serverport15440");
	if (serverport) port = (unsigned short)atoi(serverport);
	else port=15440;
	
	// Create socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);	// TCP/IP socket
	if (sockfd<0) err(1, 0);			// in case of error
	
	// setup address structure to indicate server port
	memset(&srv, 0, sizeof(srv));			// clear it first
	srv.sin_family = AF_INET;			// IP family
	srv.sin_addr.s_addr = htonl(INADDR_ANY);	// don't care IP address
	srv.sin_port = htons(port);			// server port

	// bind to our port
	rv = bind(sockfd, (struct sockaddr*)&srv, sizeof(struct sockaddr));
	if (rv<0) err(1,0);
	
	// start listening for connections
	rv = listen(sockfd, 5);
	if (rv<0) err(1,0);
	
	// main server loop, handle clients one at a time
	while (true) {
		
		// wait for next client, get session socket
		sa_size = sizeof(struct sockaddr_in);
		sessfd = accept(sockfd, (struct sockaddr *)&cli, &sa_size);
		if (sessfd<0) err(1,0);
		
		// the first package will alwasy be the package size
		size_t stub_size;
		recv_all(sessfd, &stub_size, sizeof(size_t));

		// receive actual package
		void *stub = malloc(stub_size);
		// char stub[stub_size];
		recv_all(sessfd, stub, stub_size);

		// get messages and send replies to this client, until it goes away
		int type = *(int *)stub;
		if (type == 0)
			rpc_open(sessfd, stub);
		else if (type == 1)
			rpc_close(sessfd, stub);
		else if (type == 2)
			rpc_write(sessfd, stub);

		// either client closed connection, or error
		close(sessfd);
		// clean up memory
		free(stub);
	}
	
	// fprintf(stderr, "server shutting down cleanly\n");
	// close socket
	close(sockfd);

	return 0;
}
