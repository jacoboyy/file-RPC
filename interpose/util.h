/** Constants **/
#define OFFSET 100000               /* file descriptor offset for RPC */
#define INT_SIZE sizeof(int)        /* size of int type */ 
#define SIZET_SIZE sizeof(size_t)   /* size of size_t type */
#define SSIZET_SIZE sizeof(ssize_t) /* size of ssize_t type */
#define OFFT_SIZE sizeof(off_t)     /* size of off_t type */
#define MODET_SIZE sizeof(mode_t)   /* size of mode_t type */

/** Unique identifier for different RPC operations **/
int OPEN = 0;
int CLOSE = 1;
int WRITE = 2;
int READ = 3;
int LSEEK = 4;
int STAT = 5;
int UNLINK = 6;
int GETDIRENTRIES = 7;
int GETDIRTREE = 8;
int CLOSE_CONNECTION = 9;

/**
 * @brief 	Transmit all contents of a particular size to another socket
 * @param[in] sockfd    a TCP connection socket
 * @param[in] buf       pointer to the memory block holding the contents to be sent
 * @param[in] buf_size  the total number of bytes that need to be sent
 */
void send_all(int sockfd, void *buf, size_t buf_size);

/**
 * @brief 	Receive all contents specified by an input size from a socket 
 * @param[in] sockfd    a TCP connection socket
 * @param[in] buf       pointer to the memory block to hold the received contents
 * @param[in] buf_size  the total number of bytes that need to be received
 */
void recv_all(int sockfd, void *buf, size_t buf_size);