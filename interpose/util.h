#define OFFSET 100000 /* file descriptor offset for RPC */
#define INT_SIZE sizeof(int)
#define SIZET_SIZE sizeof(size_t)
#define SSIZET_SIZE sizeof(ssize_t)
#define OFFT_SIZE sizeof(off_t)

void send_all(int sockfd, void *buf, size_t buf_size);
void recv_all(int sockfd, void *buf, size_t buf_size);