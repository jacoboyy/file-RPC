#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <stdbool.h>
#include <fcntl.h>
#include <dirent.h>
#include "dirtree.h"
#include "util.h"

extern int errno;
int server_sessfd; 	   /* global variable for connection socket */
int serialize_pos = 0; /* position in the current tree buffer used in tree serialization */

/**
 * @brief Perform a local open and send back result to client
 * @param[in] sessfd  a TCP connection socket
 * @param[in] stub	  client package with serialized function arguments
 */
void rpc_open(int sessfd, char* stub) {
	// unmarshall
	int flags = *(int *)(stub + INT_SIZE);
	mode_t mode = *(mode_t *)(stub + 2 * INT_SIZE);
	char *pathname = (char *)(stub + 2 * INT_SIZE + sizeof(mode_t));
	
	// perform a local open
	int fd = open(pathname, flags, mode);
	fprintf(stderr, "rpc_open file %s and fd is %d\n", pathname, fd);

	if (fd < 0) {
		// send back errno on failure
		int reply_buf[2] = {fd, errno};
		send_all(sessfd, (void *)reply_buf, 2 * INT_SIZE);
	} else {
		// add offset to distinguish between local and remote fds on the client side
		int fd_client = fd + OFFSET; 
		send_all(sessfd, &fd_client, INT_SIZE);
	}
}

/**
 * @brief Perform a local close and send back result to client
 * @param[in] sessfd  a TCP connection socket
 * @param[in] stub	  client package with serialized function arguments
 */
void rpc_close(int sessfd, char* stub) {
	// unmarshall, need to offset the file descriptor
	int fd = *(int *)(stub + INT_SIZE) - OFFSET;
	fprintf(stderr, "rpc_close called for remote fd %d\n", fd);
	// perform a local close
	int res = close(fd);

	// send back result
	if (res == 0)
		send_all(sessfd, &res, INT_SIZE);
	else {
		// send back errno on failure
		int reply_buf[2] = {res, errno};
		send_all(sessfd, (void *)reply_buf, 2 * INT_SIZE);
	}
}

/**
 * @brief Perform local write and send back result to client
 * @param[in] sessfd  a TCP connection socket
 * @param[in] stub	  client package with serialized function arguments
 */
void rpc_write(int sessfd, char* stub) {
	// unmarshall, need to offset the file descriptor
	int fd = *(int *)(stub + INT_SIZE) - OFFSET;
	size_t count = *(size_t *)(stub + 2 * INT_SIZE);
	void *buf = stub + 2 * INT_SIZE + SIZET_SIZE;

	// perform a local write
	fprintf(stderr, "rpc_write %zu bytes to remote fd %d\n", count, fd);
	ssize_t res = write(fd, buf, count);

	// send back results
	if (res >= 0) 
		send_all(sessfd, &res, SSIZET_SIZE);
	else {
		// also send back errno on failure
		size_t reply_size = SSIZET_SIZE + INT_SIZE;
		void *reply_buf = malloc(reply_size);
		memcpy(reply_buf, &res, SSIZET_SIZE);
		memcpy(reply_buf + SSIZET_SIZE, &errno, INT_SIZE);
		send_all(sessfd, reply_buf, reply_size);
		free(reply_buf);
	}
}

/**
 * @brief Perform local read and send back result to client
 * @param[in] sessfd  a TCP connection socket
 * @param[in] stub	  client package with serialized function arguments
 */
void rpc_read(int sessfd, void* stub) {
	// unmarshall
	int fd = *(int *)(stub + INT_SIZE) - OFFSET;
	size_t count = *(size_t *)(stub + 2 * INT_SIZE);
	void *read_buf = malloc(count);

	fprintf(stderr, "rpc_read %zu bytes from fd %d\n", count, fd);

	// perform a local read
	ssize_t res = read(fd, read_buf, count);

	// send back errno on failture, otherwise actual contents read
	size_t reply_size = res < 0 ? SSIZET_SIZE + INT_SIZE : SSIZET_SIZE + res;
	void *reply_buf = malloc(reply_size);
	memcpy(reply_buf, &res, SSIZET_SIZE);
	if (res < 0) 
		memcpy(reply_buf + SSIZET_SIZE, &errno, INT_SIZE);
	else 
		memcpy(reply_buf + SSIZET_SIZE, read_buf, res);
	send_all(sessfd, reply_buf, reply_size);
	
	// clean up memory
	free(reply_buf);
	free(read_buf); 
}

/**
 * @brief Perform local lseek and send back result to client
 * @param[in] sessfd  a TCP connection socket
 * @param[in] stub	  client package with serialized function arguments
 */
void rpc_lseek(int sessfd, void* stub) {
	// unmarsall and offset file descriptor
	int fd = *(int *)(stub + INT_SIZE) - OFFSET;
	off_t offset = *(off_t *)(stub + 2 * INT_SIZE);
	int whence = *(int *)(stub + 2 * INT_SIZE + OFFT_SIZE);

	// perform a local lseek
	fprintf(stderr, "rpc_lseek called on remote fd %d with offset %zd\n", fd, offset);
	off_t res = lseek(fd, offset, whence);

	// send back rpc result
	if (res >= 0)
		send_all(sessfd, &res, OFFT_SIZE);
	else {
		// send back errno on failure
		size_t reply_size = OFFT_SIZE + INT_SIZE;
		void *reply_buf = malloc(reply_size);
		memcpy(reply_buf, &res, OFFT_SIZE);
		memcpy(reply_buf + OFFT_SIZE, &errno, INT_SIZE);
		send_all(sessfd, reply_buf, reply_size);
		free(reply_buf);
	}
}

/**
 * @brief Perform local stat call and send back result to client
 * @param[in] sessfd  a TCP connection socket
 * @param[in] stub	  client package with serialized function arguments
 */
void rpc_stat(int sessfd, void* stub) {
	// unmarshall
	char *pathname = (char *)(stub + INT_SIZE);
	struct stat *statbuf = (struct stat *)malloc(sizeof(struct stat));
	
	// perform local call
	fprintf(stderr, "rpc_stat called on path %s\n", pathname);
	int res = stat(pathname, statbuf);

	// send back result
	size_t reply_size = res < 0 ? 2 * INT_SIZE : INT_SIZE + sizeof(struct stat);
	void *reply_buf = malloc(reply_size);
	memcpy(reply_buf, &res, INT_SIZE);
	if (res < 0) 
		memcpy(reply_buf + INT_SIZE, &errno, INT_SIZE);
	else 
		memcpy(reply_buf + INT_SIZE, statbuf, sizeof(struct stat));
	send_all(sessfd, reply_buf, reply_size);

	// clean up memory
	free(statbuf);
	free(reply_buf);
}

/**
 * @brief Perform local unlink call and send back result to client
 * @param[in] sessfd  a TCP connection socket
 * @param[in] stub	  client package with serialized function arguments
 */
void rpc_unlink(int sessfd, void* stub) {
	// unmarshall
	char *pathname = (char *)(stub + INT_SIZE);
	
	// perform local call
	fprintf(stderr, "rpc_unlink called on path %s\n", pathname);
	int res = unlink(pathname);

	// send back result
	if (res == 0)
		send_all(sessfd, &res, INT_SIZE);
	else {
		// send back errno on failure
		int reply_buf[2] = {res, errno};
		send_all(sessfd, (void *)reply_buf, 2 * INT_SIZE);
	}
}

/**
 * @brief Perform local getdirentries call and send back result to client
 * @param[in] sessfd  a TCP connection socket
 * @param[in] stub	  client package with serialized function arguments
 */
void rpc_getdirentries(int sessfd, void* stub) {
	// unmarshall
	int fd = *(int *)(stub + INT_SIZE) - OFFSET;
	size_t nbytes = *(size_t *)(stub + 2 * INT_SIZE);
	off_t *basep = malloc(OFFT_SIZE);
	*basep = *(off_t *)(stub + 2 * INT_SIZE + SIZET_SIZE);
	char *buf = (char *)malloc(nbytes);
	
	// perform local fuction call
	fprintf(stderr, "rpc_getdirentries called on fd %d for %zu byes at offset %zu\n", fd, nbytes, *basep);
	ssize_t res = getdirentries(fd, buf, nbytes, basep);
	fprintf(stderr, "rpc_getdirentries read %zd bytes and the new offset is %zd\n", res, *basep);

	// prepare reply buffer
	size_t reply_size = res < 0 ? SSIZET_SIZE + INT_SIZE : SSIZET_SIZE + res + OFFT_SIZE;
	void *reply_buf = malloc(reply_size);
	memcpy(reply_buf, &res, SSIZET_SIZE);
	if (res < 0)
		memcpy(reply_buf + SSIZET_SIZE, &errno, INT_SIZE);
	else {
		memcpy(reply_buf + SSIZET_SIZE, buf, res);
		memcpy(reply_buf + SSIZET_SIZE + res, basep, OFFT_SIZE);
	}
	// send back result
	send_all(sessfd, reply_buf, reply_size);

	// clean up memory
	free(reply_buf);
	free(basep);
	free(buf);
}

/**
 * @brief Get the size in bytes of the serialized dirtreenode recusively
 * @param[in] tree a pointer to a dirtreenode
 * @return the size of the serialized dirtreenode
 */
size_t get_tree_size(struct dirtreenode* tree){
	if (tree == NULL) 
		return 0;
	// current node size
	size_t size = 2 * INT_SIZE + strlen(tree->name) + 1;
	// recursively get the subnodes size
    for (int idx = 0; idx < tree->num_subdirs; idx++) {
        size += get_tree_size(tree->subdirs[idx]);
    }
    return size;
}

/**
 * @brief Recursively serialize a dirtreenode struct using pre-order traversal.
 * Each serialized tree node contains the strlen of the node name, the number
 * of subdirs and the actual node name.
 * @param[in] tree  a pointer to the dirtreenode to be serialized
 * @param[out] buf  memory block containing the serialized tree
 */
void serialize(void *buf, struct dirtreenode *tree){
    // copy name length
    int name_length = strlen(tree->name) + 1;
    memcpy(buf + serialize_pos, &name_length, INT_SIZE);
	serialize_pos += INT_SIZE;
    // copy subdirs size
    memcpy(buf + serialize_pos, &(tree->num_subdirs), INT_SIZE);
	serialize_pos += INT_SIZE;
    // copy name
    memcpy(buf + serialize_pos, tree->name, name_length);
    serialize_pos += name_length;
	// recursion
    for (int idx = 0; idx < tree->num_subdirs; idx++) {
        serialize(buf, tree->subdirs[idx]);
    }
}

/**
 * @brief Perform local getdirtree call and send back the serialized dirtreenode to client
 * @param[in] sessfd  a TCP connection socket
 * @param[in] stub	  client package with serialized function arguments
 */
void rpc_getdirtree(int sessfd, void* stub) {
	// unmarshall
	char *path = (char *)(stub + INT_SIZE);

	// perform local getdirtree
	fprintf(stderr, "rpc_getdirtree called on path %s\n", path);
	struct dirtreenode *tree = getdirtree(path);

	// get size of the tree to prepare memory buffer
	size_t tree_size = get_tree_size(tree);
	// prepare reply
	size_t reply_size = tree_size == 0 ? SIZET_SIZE + INT_SIZE : SIZET_SIZE + tree_size;
	void *reply_buf = malloc(reply_size);
	memcpy(reply_buf, &tree_size, SIZET_SIZE);
	// getdirtree failed, send back errno
	if (tree_size == 0) 
		memcpy(reply_buf + SIZET_SIZE, &errno, INT_SIZE);
	else {
		// reset buffer pointer position and serialize the tree
		serialize_pos = 0;
		serialize(reply_buf + SIZET_SIZE, tree);		
	}
	// send back reply
	send_all(sessfd, reply_buf, reply_size);
	free(reply_buf); // clean up memory

	// dirtree no longer needed on the server side
	if (tree_size > 0)
		freedirtree(tree); 
}


int main(int argc, char**argv) {
	char *serverport;
	unsigned short port;
	int sockfd, rv;
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

	// main server loop, handle one client at a time
	while (true) {
		// connect to a client
		sa_size = sizeof(struct sockaddr_in);
		server_sessfd = accept(sockfd, (struct sockaddr *)&cli, &sa_size);
		if (server_sessfd<0) err(1,0);

		// fork and handle client in child process
		pid_t p = fork();
		if (p == 0) {
			// child no longer needs listening socket
			close(sockfd); 

			// handle all RPC calls from the client
			while (true) {
				// check package size first
				size_t stub_size;
				recv_all(server_sessfd, &stub_size, SIZET_SIZE);

				// receive actual package
				void *stub = malloc(stub_size);
				recv_all(server_sessfd, stub, stub_size);

				// check operation and perform local call accordingly
				int operation = *(int *)stub;
				if (operation == OPEN)
					rpc_open(server_sessfd, stub);
				else if (operation == CLOSE)
					rpc_close(server_sessfd, stub);
				else if (operation == WRITE)
					rpc_write(server_sessfd, stub);
				else if (operation == READ)
					rpc_read(server_sessfd, stub);
				else if (operation == LSEEK)
					rpc_lseek(server_sessfd, stub);
				else if (operation == STAT)
					rpc_stat(server_sessfd, stub);
				else if (operation == UNLINK)
					rpc_unlink(server_sessfd, stub);
				else if (operation == GETDIRENTRIES)
					rpc_getdirentries(server_sessfd, stub);
				else if (operation == GETDIRTREE)
					rpc_getdirtree(server_sessfd, stub);
				else if (operation == CLOSE_CONNECTION) {
					// terminate child server
					fprintf(stderr, "terminate child server\n");
					exit(0);
				} else
					err(1, 0); // unsupported operation

				// clean up memory
				free(stub);
			}
		}
		// parent no longer need connection socket
		close(server_sessfd);
	}
	
	fprintf(stderr, "server shutting down cleanly\n");
	// close listening socket
	close(sockfd);

	return 0;
}
