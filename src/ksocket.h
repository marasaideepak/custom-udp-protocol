#ifndef KSOCKET_H
#define KSOCKET_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h> 
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>

#define SOCK_KTP 66

// timeout
#define T 5
// probability
#define p 0.05
// max sockets
#define N 10

// semaphore functions
#define P(s, i) { struct sembuf op = {i, -1, 0}; semop(s, &op, 1); }
#define V(s, i) { struct sembuf op = {i, 1, 0}; semop(s, &op, 1); }

// semahore idxes
#define SOCK 0
#define SMEM 1
#define SOCK_REQ 2
#define SOCK_READY 3

// error codes
#define ENOSPACE 55     
#define ENOTBOUND 57    
#define ENOMESSAGE 91     

// helper struct for window
struct wnd {
    int wndw[256];  
    int size;
    uint8_t start_seq; 
};

// shared memory
struct shared_mem {
    // socket status
    int available;
    pid_t process_id;          
    int sock_id;
    
    // dest IP & port
    char ip_addr[16];       
    int port;       
    
    // send buffer
    char send_buf[10][512]; 
    int send_buf_size;
    int send_msg_len[10];

    // recv buffer
    char recv_buf[10][512]; 
    int recv_next_idx;   
    int is_unique[10]; 
    int recv_msg_len[10];

    // send and receive windows
    struct wnd swnd;        
    struct wnd rwnd;   
    
    // nospace flag
    int nospace;   
    
    // last sent time
    time_t last_sent[256];  
};

// meesage header - (type, seq_num, rwnd size and length of msg)
struct ktp_header {
    int type;           
    uint8_t seq_num;  
    int wnd_size;
    int len;        
};

// socket info
struct socket_info {
    int sock_id;
    char ip_addr[16];
    int port;
    int err_no;
};

extern struct shared_mem* SM;
extern struct socket_info* sock_info;
extern int shmid_sock_info, shmid_SM;
extern int semid;

int k_socket(int domain, int type, int protocol);
int k_bind(int sockfd, char* src_ip, int src_port, char* dest_ip, int dest_port);
int k_sendto(int sockfd, const void *buf, int len, const struct sockaddr *dest_addr, socklen_t addrlen);
int k_recvfrom(int sockfd, void *buf, int len, struct sockaddr *src_addr, socklen_t *addrlen);
int k_close(int sockfd);
int dropMessage(float pr);

#endif  // KSOCKET_H