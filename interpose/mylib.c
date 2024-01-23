#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <arpa/inet.h>
#include <err.h>

extern int errno;

// The following line declares a function pointer with the same prototype as the open function.  
int (*orig_open)(const char *pathname, int flags, ...);  // mode_t mode is needed when flags includes O_CREAT
int (*orig_close)(int fd);
ssize_t (*orig_read)(int fd, void *buf, size_t count);
ssize_t (*orig_write)(int fd, const void *buf, size_t count);
off_t (*orig_lseek)(int fd, off_t offset, int whence);
int (*orig_stat)(const char *pathname, struct stat *statbuf);
int (*orig_unlink)(const char *pathname);
ssize_t (*orig_getdirentries)(int fd, char *buf, size_t nbytes , off_t *basep);
struct dirtreenode* (*orig_getdirtree)(const char *path );
void (*orig_freedirtree)(struct dirtreenode* dt );


int connect2server() {
	char *serverip;
	char *serverport;
	unsigned short port;
	int sockfd, rv;
	struct sockaddr_in srv;

	// Get server ip address from environment variable
	serverip = getenv("server15440");
	if (!serverip) serverip = "127.0.0.1";

	// Get port number of environment variable
	// Get environment variable indicating the port of the server
	serverport = getenv("serverport15440");
	if (!serverport) serverport = "15440";
	port = (unsigned short)atoi(serverport);

	// Create socket
	sockfd = socket(AF_INET, SOCK_STREAM, 0);	// TCP/IP socket
	if (sockfd<0) err(1, 0);			// in case of error
	
	// setup address structure to point to server
	memset(&srv, 0, sizeof(srv));			// clear it first
	srv.sin_family = AF_INET;			// IP family
	srv.sin_addr.s_addr = inet_addr(serverip);	// IP address of server
	srv.sin_port = htons(port);			// server port

	// connect to the server
	rv = connect(sockfd, (struct sockaddr*)&srv, sizeof(struct sockaddr));
	if (rv<0) err(1,0);
	return sockfd;
}

void send_all(int sockfd, void *buf, size_t buf_size) {
	size_t total = 0;
	while (total < buf_size) {
		ssize_t cur = send(sockfd, buf + total, buf_size - total, 0);
		if (cur < 0) err(1, 0);
		total += cur;
	}
	// fprintf(stderr, "client sent %zu bytes\n", buf_size);
}

void recv_all(int sockfd, void *buf, size_t buf_size) {
	size_t total = 0;
	while (total < buf_size) {
		ssize_t cur = recv(sockfd, buf + total, buf_size - total, 0);
		if (cur < 0) err(1, 0);
		total += cur;
	}
	// fprintf(stderr, "client received %zu bytes\n", buf_size);
}

// This is our replacement for the open function from libc.
int open(const char *pathname, int flags, ...) {
	int type = 0;
	mode_t m=0;
	if (flags & O_CREAT) {
		va_list a;
		va_start(a, flags);
		m = va_arg(a, mode_t);
		va_end(a);
	}

	fprintf(stderr, "mylib: open called for path %s\n", pathname);
	
	// prepare client stub
	int sockfd = connect2server();
	// stub = type (int) + flags (int) + m (mode_t) + pathname (string)
	size_t stub_size = 2 * sizeof(int) + sizeof(mode_t) + strlen(pathname) + 1;
	char stub[stub_size];
	memcpy(stub, &type, sizeof(int));
	memcpy(stub + sizeof(int), &flags, sizeof(int));
	memcpy(stub + 2 * sizeof(int), &m, sizeof(mode_t));
	memcpy(stub + 2 * sizeof(int) + sizeof(mode_t), pathname, strlen(pathname) + 1);

	// send package size first
	send_all(sockfd, &stub_size, sizeof(size_t));
	// send actual package
	send_all(sockfd, stub, stub_size);

	// receive client reply
	int fd;
	recv_all(sockfd, &fd, sizeof(int));
	fprintf(stderr, "remote fd is %d\n", fd);
	if (fd < 0) recv_all(sockfd, &errno, sizeof(int)); // set errno on failure

	// close the connection socket
	orig_close(sockfd);

	return fd;
}

int close(int fd) {
	fprintf(stderr, "mylib: close remote fd %d\n", fd);
	
	int sockfd = connect2server();

	// prepare client stub
	int type = 1;
	// sub = type (int) + fd (int)
	size_t stub_size = 2 * sizeof(int);
	char stub[stub_size];
	memcpy(stub, &type, sizeof(int));
	memcpy(stub + sizeof(int), &fd, sizeof(int));
	
	// send package size before actual package
	send_all(sockfd, &stub_size, sizeof(size_t));
	send_all(sockfd, stub, stub_size);

	int res;
	recv_all(sockfd, &res, sizeof(int));
	if (res < 0) {
		recv_all(sockfd, &errno, sizeof(int)); // set errno on failure
		fprintf(stderr, "close fd %d failed!\n", fd);
	} else {
		fprintf(stderr, "close fd %d succeed!\n", fd);
	}

	// close connection socket
	orig_close(sockfd);

	return res;
}

ssize_t read(int fd, void *buf, size_t count) {
	// fprintf(stderr, "mylib: try to read %zu bytes from file descriptor %d\n", count, fd);
	return orig_read(fd, buf, count);
}

ssize_t write(int fd, const void *buf, size_t count) {
	// fprintf(stderr, "mylib: try to write %zu bytes to file descriptor %d\n", count, fd);

	int sockfd = connect2server();
	int type = 2;
	// stub = type (int) + fd (int) + count (size_t) + buf (count)
	size_t stub_size = 2 * sizeof(int) + sizeof(size_t) + count;
	char stub[stub_size];

	memcpy(stub, &type, sizeof(int));
	memcpy(stub + sizeof(int), &fd, sizeof(int));
	memcpy(stub + 2 * sizeof(int), &count, sizeof(size_t));
	memcpy(stub + 2 * sizeof(int) + sizeof(size_t), buf, count);
	
	// send package size first
	send_all(sockfd, &stub_size, sizeof(size_t));
	// send actual package
	send_all(sockfd, stub, stub_size);

	// recv number of bytes
	ssize_t write_size;
	recv_all(sockfd, &write_size, sizeof(ssize_t));
	// set errno on failure
	if (write_size < 0) recv_all(sockfd, &errno, sizeof(int));

	// close connection socket
	orig_close(sockfd);
	
	return write_size;
}

off_t lseek(int fd, off_t offset, int whence) {
	// fprintf(stderr, "mylib: lseek called");
	return orig_lseek(fd, offset, whence);
}

int stat(const char *pathname, struct stat *statbuf) {
	// fprintf(stderr, "mylib: stat called");
	return orig_stat(pathname, statbuf);
}

int unlink(const char *pathname) {
	// fprintf(stderr, "mylib: unlink called");
	return orig_unlink(pathname);
}

ssize_t getdirentries(int fd, char *buf, size_t nbytes , off_t *basep) {
	return orig_getdirentries(fd, buf, nbytes, basep);
}

struct dirtreenode* getdirtree(const char *path ) {
	return orig_getdirtree(path);
}

void freedirtree(struct dirtreenode* dt ) {
}


// This function is automatically called when program is started
void _init(void) {
	// set function pointer orig_open to point to the original open function
	orig_open = dlsym(RTLD_NEXT, "open");
	orig_close = dlsym(RTLD_NEXT, "close");
	orig_read = dlsym(RTLD_NEXT, "read");
	orig_write = dlsym(RTLD_NEXT, "write");
	orig_lseek = dlsym(RTLD_NEXT, "lseek");
	orig_stat = dlsym(RTLD_NEXT, "stat");
	orig_unlink = dlsym(RTLD_NEXT, "unlink");
	orig_getdirentries = dlsym(RTLD_NEXT, "getdirentries");
	orig_getdirtree = dlsym(RTLD_NEXT, "getdirtree");
	orig_freedirtree = dlsym(RTLD_NEXT, "reedirtree");

	// fprintf(stderr, "Init mylib\n");
}


