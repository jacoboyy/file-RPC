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
#include "dirtree.h"
#include "util.h"

/** Global variables **/
extern int errno;
int client_sockfd;       /* global variable for the connection socket */
int deserialize_pos = 0; /* global position counter used in tree de-serialization*/

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

/**
 * @brief 	Make a connection to the remote server
 * @return	A connection socket
 */
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

/**
 * @brief 	Interposed open function from libcs to open a file in the remote server
 * @return	A file desciptor on success, -1 if an error occurs
 */
int open(const char *pathname, int flags, ...) {
	mode_t m=0;
	if (flags & O_CREAT) {
		va_list a;
		va_start(a, flags);
		m = va_arg(a, mode_t);
		va_end(a);
	}
	
	// prepare client stub
	// package = operation (int) + flags (int) + m (mode_t) + pathname (string)
	// stub = package_size (size_t) + package
	size_t package_size = 2 * INT_SIZE + MODET_SIZE + strlen(pathname) + 1;
	size_t stub_size = SIZET_SIZE + package_size;

	void *stub = malloc(stub_size);
	memcpy(stub, &package_size, SIZET_SIZE);
	memcpy(stub + SIZET_SIZE, &OPEN, INT_SIZE);
	memcpy(stub + SIZET_SIZE + INT_SIZE, &flags, INT_SIZE);
	memcpy(stub + SIZET_SIZE + 2 * INT_SIZE, &m, MODET_SIZE);
	memcpy(stub + SIZET_SIZE + 2 * INT_SIZE + MODET_SIZE, pathname, strlen(pathname) + 1);

	// send entire stub
	send_all(client_sockfd, stub, stub_size);
	free(stub); // clean up memory

	// receive client reply
	int fd;
	recv_all(client_sockfd, &fd, INT_SIZE);
	if (fd < 0) 
		recv_all(client_sockfd, &errno, INT_SIZE); // set errno on failure

	fprintf(stderr, "mylib: open called for path %s and remote fd is %d\n", pathname, fd - OFFSET);

	return fd;
}

/**
 * @brief 	Interposed close function from libc to close a file descriptor
 * @return	0 on success, -1 if an error occurs
 */
int close(int fd) {
	// check if local close
	if (fd < OFFSET) 
		return orig_close(fd);
	
	fprintf(stderr, "mylib: close remote fd %d\n", fd - OFFSET);
	
	// package = operation (int) + fd (int)
	size_t package_size = 2 * INT_SIZE;
	size_t stub_size = SIZET_SIZE + package_size;
	void *stub = malloc(stub_size);
	memcpy(stub, &package_size, SIZET_SIZE);
	memcpy(stub + SIZET_SIZE, &CLOSE, INT_SIZE);
	memcpy(stub + SIZET_SIZE + INT_SIZE, &fd, INT_SIZE);
	
	// send stub package
	send_all(client_sockfd, stub, stub_size);
	free(stub); // clean up memory

	int res;
	recv_all(client_sockfd, &res, INT_SIZE);
	if (res < 0) 
		recv_all(client_sockfd, &errno, INT_SIZE); // set errno on failure

	return res;
}

/**
 * @brief 	Interposed write function from libc to write bytes to a file descriptor
 * @return	the actual number of bytes written on success, -1 if an error occurs
 */
ssize_t write(int fd, const void *buf, size_t count) {
	// check if local close
	if (fd < OFFSET) 
		return orig_write(fd, buf, count);

	fprintf(stderr, "mylib: write %zu bytes to remote file descriptor %d\n", count, fd - OFFSET);

	// package = operation (int) + fd (int) + count (size_t) + buf (count)
	size_t package_size = 2 * INT_SIZE + SIZET_SIZE + count;
	size_t stub_size = SIZET_SIZE + package_size;
	void *stub = malloc(stub_size);

	memcpy(stub, &package_size, SIZET_SIZE);
	memcpy(stub + SIZET_SIZE, &WRITE, INT_SIZE);
	memcpy(stub + SIZET_SIZE + INT_SIZE, &fd, INT_SIZE);
	memcpy(stub + SIZET_SIZE + 2 * INT_SIZE, &count, SIZET_SIZE);
	memcpy(stub + 2 * SIZET_SIZE + 2 * INT_SIZE, buf, count);
	
	// send stub package
	send_all(client_sockfd, stub, stub_size);
	free(stub); // clean up memory

	// recv number of bytes
	ssize_t write_size;
	recv_all(client_sockfd, &write_size, SSIZET_SIZE);
	// set errno on failure
	if (write_size < 0) recv_all(client_sockfd, &errno, INT_SIZE);	
	
	return write_size;
}

/**
 * @brief 	Interposed read function from libc to read bytes from a file descriptor
 * @return	the actual number of bytes read on success, -1 if an error occurs
 */
ssize_t read(int fd, void *buf, size_t count) {
	// check if local read
	if (fd < OFFSET)
		return orig_read(fd, buf, count);

	fprintf(stderr, "mylib: read %zu bytes from file descriptor %d\n", count, fd - OFFSET);

	// prepare client stub
	// package = operation (int) + fd (int) + count (size_t)
	size_t package_size = 2 * INT_SIZE + SIZET_SIZE;
	size_t stub_size = SIZET_SIZE + package_size;
	void *stub = malloc(stub_size);
	memcpy(stub, &package_size, SIZET_SIZE);
	memcpy(stub + SIZET_SIZE, &READ, INT_SIZE);
	memcpy(stub + SIZET_SIZE + INT_SIZE, &fd, INT_SIZE);
	memcpy(stub + SIZET_SIZE + 2 * INT_SIZE, &count, SIZET_SIZE);

	// send client stub
	send_all(client_sockfd, stub, stub_size);
	free(stub);

	ssize_t res; // actual bytes read
	recv_all(client_sockfd, &res, SSIZET_SIZE);
	if (res < 0) 
		recv_all(client_sockfd, &errno, INT_SIZE); // set errno on failure
	else 
		recv_all(client_sockfd, buf, res);
	return res;
}

/**
 * @brief 	Interposed lseek function from libc to reposition read/write file offset
 * @return	the resulting file offset on success, -1 if an error occurs
 */
off_t lseek(int fd, off_t offset, int whence) {
	// check if local lseek
	if (fd < OFFSET)
		return orig_lseek(fd, offset, whence);

	fprintf(stderr, "mylib: lseek called on remote fd %d with offset %zd\n", fd - OFFSET, offset);
	
	// prepare client stub
	// package = operation (int) + fd (int) + offset (off_t) + whence (int)
	size_t package_size = 3 * INT_SIZE + OFFT_SIZE;
	size_t stub_size = SIZET_SIZE + package_size;
	void *stub = malloc(stub_size);
	memcpy(stub, &package_size, SIZET_SIZE);
	memcpy(stub + SIZET_SIZE, &LSEEK, INT_SIZE);
	memcpy(stub + SIZET_SIZE + INT_SIZE, &fd, INT_SIZE);
	memcpy(stub + SIZET_SIZE + 2 * INT_SIZE, &offset, OFFT_SIZE);
	memcpy(stub + SIZET_SIZE + 2 * INT_SIZE + OFFT_SIZE, &whence, INT_SIZE);

	// send client stub
	send_all(client_sockfd, stub, stub_size);
	free(stub); // clean up memory

	off_t res; // resulting offset location
	recv_all(client_sockfd, &res, OFFT_SIZE);

	if (res < 0) 
		recv_all(client_sockfd, &errno, INT_SIZE);
	return res;
}


/**
 * @brief 	Interposed stat function from libc to get file status on remote server
 * @param[out] statbuf a stat structure containing properties of a file
 * @return	0 on success, -1 if an error occurs
 */
int stat(const char *pathname, struct stat *statbuf) {
	fprintf(stderr, "mylib: stat called on path %s\n", pathname);
	// prepare client stub
	// package = operation (int) + pathname (string)
	size_t package_size = INT_SIZE + strlen(pathname) + 1;
	size_t stub_size = SIZET_SIZE + package_size;
	void *stub = malloc(stub_size);

	memcpy(stub, &package_size, SIZET_SIZE);
	memcpy(stub + SIZET_SIZE, &STAT, INT_SIZE);
	memcpy(stub + SIZET_SIZE + INT_SIZE, pathname, strlen(pathname) + 1);

	// send client stub
	send_all(client_sockfd, stub, stub_size);
	free(stub); // clean up memory

	// receive response from server
	int res;
	recv_all(client_sockfd, &res, INT_SIZE);
	if (res < 0) 
		recv_all(client_sockfd, &errno, INT_SIZE);
	else 
		recv_all(client_sockfd, statbuf, sizeof(struct stat));
	return res;
}

/**
 * @brief 	Interposed unlink function from libc to unlink a remote file on server
 * @return	0 on success, -1 if an error occurs
 */
int unlink(const char *pathname) {
	fprintf(stderr, "mylib: unlink called on path %s\n", pathname);
	// prepare client stub
	// package = operation (int) + pathname (string)
	size_t package_size = INT_SIZE + strlen(pathname) + 1;
	size_t stub_size = SIZET_SIZE + package_size;
	void *stub = malloc(stub_size);
	memcpy(stub, &package_size, SIZET_SIZE);
	memcpy(stub + SIZET_SIZE, &UNLINK, INT_SIZE);
	memcpy(stub + SIZET_SIZE + INT_SIZE, pathname, strlen(pathname) + 1);

	// send client stub
	send_all(client_sockfd, stub, stub_size);
	free(stub); // clean up

	// receive server response
	int res;
	recv_all(client_sockfd, &res, INT_SIZE);
	// set errno on failure
	if (res < 0) 
		recv_all(client_sockfd, &errno, INT_SIZE);
	
	return res;
}

/**
 * @brief 	Interposed getdirentries function from libc to get directory entries
 * @param[out] buf memory block containing the actual directory entries read
 * @param[out] basep new file offset position after reading
 * @return	number of bytes read on success, -1 if an error occurs
 */
ssize_t getdirentries(int fd, char *buf, size_t nbytes, off_t *basep) {
	if (fd < OFFSET)
		return orig_getdirentries(fd, buf, nbytes, basep);

	fprintf(stderr, "mylib: getdirentries called on fd %d for %zu byes at offset %zu\n", fd - OFFSET, nbytes, *basep);
	// prepare client stub
	// package = operation (int) + fd (int) + nbytes (size_t) + *basep (off_t)
	size_t package_size = 2 * INT_SIZE + SIZET_SIZE + OFFT_SIZE;
	size_t stub_size = SIZET_SIZE + package_size;
	void *stub = malloc(stub_size);
	memcpy(stub, &package_size, SIZET_SIZE);
	memcpy(stub + SIZET_SIZE, &GETDIRENTRIES, INT_SIZE);
	memcpy(stub + SIZET_SIZE + INT_SIZE, &fd, INT_SIZE);
	memcpy(stub + SIZET_SIZE + 2 * INT_SIZE, &nbytes, SIZET_SIZE);
	memcpy(stub + 2 * SIZET_SIZE + 2 * INT_SIZE, basep, OFFT_SIZE);

	// send client stub
	send_all(client_sockfd, stub, stub_size);
	free(stub); // clean up memory

	// receive server response
	ssize_t res;
	recv_all(client_sockfd, &res, SSIZET_SIZE);
	if (res < 0)
		recv_all(client_sockfd, &errno, INT_SIZE);
	else {
		size_t reply_size = res + OFFT_SIZE;
		void *reply = malloc(reply_size);
		recv_all(client_sockfd, reply, reply_size);
		memcpy(buf, reply, res);
		*basep = *(off_t *)(reply + res);
		free(reply);
	}
	fprintf(stderr, "getdirentries read %zd bytes and the new offset is %zd\n", res, *basep);
	return res;
}

/**
 * @brief Recursively deserialize a dirtreenode struct using pre-order traversal
 * @param[in] buf memory block containing the serialized tree
 * @return	a pointer to the deserialized ditreenode
 */
struct dirtreenode* deserialize(void *buf) {
    struct dirtreenode *tree = (struct dirtreenode *)malloc(sizeof(struct dirtreenode));
    // get attributes from buffer
    int name_length = *(int *)(buf + deserialize_pos);
	deserialize_pos += INT_SIZE;
    int num_subdirs = *(int *)(buf + deserialize_pos);
	deserialize_pos += INT_SIZE;
    char *name = (char *)malloc(name_length);
	memcpy(name, buf + deserialize_pos, name_length);
	deserialize_pos += name_length;
    // copy to deserialized tree
    tree->name = name;
    tree->num_subdirs = num_subdirs;
	tree->subdirs = (struct dirtreenode **)malloc(num_subdirs * sizeof(struct dirtreenode *));
    // recusively deserialized subtrees
    for (int idx = 0; idx < num_subdirs; idx++) {
        tree->subdirs[idx] = deserialize(buf);
    }
    return tree;
}

/**
 * @brief 	Interposed getdirtree function from dirtree.h
 * @param[in] path file path at the remote server
 * @return	a pointer to the tree root, NULL if an error occurs
 */
struct dirtreenode* getdirtree(const char *path) {
	fprintf(stderr, "mylib: getdirtree called on path %s\n", path);
	// package = operation (int) + path (string)
	size_t package_size = INT_SIZE + strlen(path) + 1;
	size_t stub_size = SIZET_SIZE + package_size;
	void *stub = malloc(stub_size);
	memcpy(stub, &package_size, SIZET_SIZE);
	memcpy(stub + SIZET_SIZE, &GETDIRTREE, INT_SIZE);
	memcpy(stub + SIZET_SIZE + INT_SIZE, path, strlen(path) + 1);

	// send package
	send_all(client_sockfd, stub, stub_size);
	free(stub); // clean up memory

	// receive package
	size_t tree_size;
	recv_all(client_sockfd, &tree_size, SIZET_SIZE);
	if (tree_size == 0) {
		recv_all(client_sockfd, &errno, INT_SIZE); // set errno on failure
		return NULL;
	} else {
		// receive byte stream of serialized tree
		void *tree_buf = malloc(tree_size);
		recv_all(client_sockfd, tree_buf, tree_size);
		// deserialization
		deserialize_pos = 0; // reset position
		struct dirtreenode *tree = deserialize(tree_buf);
		free(tree_buf);
		return tree;
	}
}

/**
 * @brief 	Interposed freedirtree function from dirtree.h to recursively 
 * free all memory used by the dirtreenode
 * @param[in] dt pointer to the tree to be freed
 */
void freedirtree(struct dirtreenode* dt) {
	// recursively free the subtree in post order traversal
	for (int idx = 0; idx < dt->num_subdirs; idx++) {
		freedirtree(dt->subdirs[idx]);
	}
	// free memory allocated to attributes of the current node
	free(dt->name);
	free(dt->subdirs);
	free(dt);
}

/**
 * @brief 	Send special RPC message to the server to terminate
 * the child server process, invoked before the client exits
 */
void close_connection() {
	fprintf(stderr, "client send signal to terminate child server\n");
	
	// package = operation (int)
	size_t package_size = INT_SIZE;
	size_t stub_size = SIZET_SIZE + INT_SIZE;
	void *stub = malloc(stub_size);
	memcpy(stub, &package_size, SIZET_SIZE);
	memcpy(stub + SIZET_SIZE, &CLOSE_CONNECTION, INT_SIZE);

	// send package
	send_all(client_sockfd, stub, stub_size);
	free(stub); // clean up memory
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
	orig_freedirtree = dlsym(RTLD_NEXT, "freedirtree");

	// connect to the server
	client_sockfd = connect2server();
}

// This function will be called when the program exits
void _fini(void) {
	// send termination signal to server
	close_connection();
	// close connection socket on exit
	orig_close(client_sockfd);
}
