/*
=====================================
Assignment 4 Submission
Name: Sai Deepak Reddy Mara
Roll number: 22CS10066
FILE: ksocket.c
=====================================
*/

#include "ksocket.h"

struct shared_mem* SM;
struct socket_info* sock_info;

int semid; 
int shmid_sock_info, shmid_SM;


/*
    HELPER FUNCTIONS
*/

int dropMessage(float prob) {
    float this = (float)rand() / (float)RAND_MAX;
    if (this < prob) return 1;
    return 0;
}

void clear_socket_info() {
    sock_info->sock_id = 0;
    sock_info->err_no = 0;
    sock_info->ip_addr[0] = '\0';
    sock_info->port = 0;
}

/*
    -------------------------------INIT--------------------------------
    Attach to shared memory and semaphores createed by initksocket.
*/
void init_() {
    shmid_sock_info = shmget(ftok("/", 'D'), sizeof(struct socket_info), 0666);
    shmid_SM = shmget(ftok("/", 'E'), sizeof(struct shared_mem)*N, 0666);
    semid = semget(ftok("/", 'S'), 4, 0666);


    if (shmid_sock_info == -1 || shmid_SM == -1 || semid == -1) {
        perror("Run initksocket first");
        exit(1);
    }

    sock_info = (struct socket_info*)shmat(shmid_sock_info, NULL, 0);
    SM = (struct shared_mem*)shmat(shmid_SM, NULL, 0);
}

/*
    SOCKET FUNCTIONS
*/

/*
    -------------------------------KSOCKET--------------------------------
    Copy the to sock info shared memory which lets initsocket know to create a socket
    and then wait for initsocket to complete.
    Then initialise shared memory for the socket.
*/
int k_socket(int domain, int type, int protocol) {
    init_();

    if (type != SOCK_KTP) {
        errno = EINVAL;
        return -1;
    }
    
    int idx = -1;

    // lock socket info
    P(semid, SOCK);

    // lock shared memory
    P(semid, SMEM);
    for(int i = 0; i < N; i++) {
        if(SM[i].available) {
            idx = i;
            SM[i].available = 0;  
            break;
        }
    }
    // unlock shared memory
    V(semid, SMEM);

    // no free socket
    if (idx == -1) {
        errno = ENOSPACE;
        clear_socket_info();

        // unlock socket info
        V(semid, SOCK);
        return -1;
    }

    // unlock socket info
    V(semid, SOCK);

    // signal initsocket to start creating socket
    V(semid, SOCK_REQ);

    // wait for socket creation to complete
    P(semid, SOCK_READY);

    // lock socket info
    P(semid, SOCK);

    if(sock_info->sock_id == -1){
        errno = sock_info->err_no;
        clear_socket_info();

        // unlock socket info
        V(semid, SOCK);
        return -1;
    }

    // unlock socket info
    V(semid, SOCK);

    // lock shared memory
    P(semid, SMEM);

    // init shared memory
    SM[idx].available = 0;
    SM[idx].process_id = getpid();
    SM[idx].sock_id = sock_info->sock_id;

    // 10 message buffer
    SM[idx].send_buf_size = 10;  

    // nextindex to read (by application in rw) is 0
    SM[idx].recv_next_idx = 0;

    for (int j = 0; j < 10; j++) SM[idx].is_unique[j] = 0;

    // Initialize swnd and rwnd 
    for (int j = 0; j < 256; j++) {
        SM[idx].swnd.wndw[j] = -1;
        // first window is 1 to 10 and their idx in recv buffer
        if (j > 0 && j < 11) SM[idx].rwnd.wndw[j] = j - 1;  
        else SM[idx].rwnd.wndw[j] = -1;
        SM[idx].last_sent[j] = -1;
    }

    // wndw size is 10 and start seq is 1
    SM[idx].swnd.size=SM[idx].rwnd.size = 10;  
    SM[idx].swnd.start_seq = 1;
    SM[idx].rwnd.start_seq = 1;

    // initially space is available
    SM[idx].nospace = 0;

    // unlock shared memory
    V(semid, SMEM);
 
    // clear socket info
    P(semid, SOCK);
    clear_socket_info();
    V(semid, SOCK);

    return idx;
}

/*
    -------------------------------KBIND--------------------------------
    Copy the src ip and port to sock info shared memory which lets initsocket know to bind
    and then wait for initsocket to complete.
    Then store the dest ip and port in shared memory.
*/
int k_bind(int sockfd, char* src_ip, int src_port, char* dest_ip, int dest_port) {

    // lock shared memory
    P(semid, SMEM);

    // check if sock is not already bound
    if (sockfd < 0 || sockfd >= N || SM[sockfd].available == 1) {
        errno = EBADF;
        V(semid, SMEM);
        return -1;
    }
    // unlock shared memory
    V(semid, SMEM);

    // lock socket info
    P(semid, SOCK);

    // store src ip and port in socket info so initsocket can bind
    sock_info->sock_id = SM[sockfd].sock_id;

    strncpy(sock_info->ip_addr, src_ip, sizeof(sock_info->ip_addr) - 1);
    sock_info->ip_addr[sizeof(sock_info->ip_addr) - 1] = '\0';  

    sock_info->port = src_port;

    // unlock socket info
    V(semid, SOCK);

    // this signals initsocket to start binding
    V(semid, SOCK_REQ);

    // wait for binding to complete    
    P(semid, SOCK_READY);

    // lock socket info
    P(semid, SOCK);
    // if error in binding return error
    if(sock_info->sock_id == -1){
        errno = sock_info->err_no;
        clear_socket_info();
        V(semid, SOCK);
        V(semid, SMEM);
        return -1;
    }
    // unlock socket info
    V(semid, SOCK);

    // save dest ip and port to shared mem
    strncpy(SM[sockfd].ip_addr, dest_ip, sizeof(SM[sockfd].ip_addr) - 1);
    SM[sockfd].ip_addr[sizeof(SM[sockfd].ip_addr) - 1] = '\0';  
    SM[sockfd].port = dest_port;

    // unlock shared memory
    V(semid, SMEM);

    // clear socket info
    P(semid, SOCK);
    clear_socket_info();
    V(semid, SOCK);

    return 0;
}

/*
    -------------------------------KSENDTO--------------------------------
    Copy the message to send buffer in shared memory.
    If no space set errno to ENOSPACE.
*/
int k_sendto(int sockfd, const void *buf, int len, const struct sockaddr *dest_addr, socklen_t addrlen) {
    // lock shared memory
    P(semid, SMEM);

    if (sockfd < 0 || sockfd >= N || SM[sockfd].available == 1) {
        errno = EBADF;
        V(semid, SMEM);
        return -1;
    }

    char* ip;
    int port;

    ip = inet_ntoa(((struct sockaddr_in *)dest_addr)->sin_addr);
    port = ntohs(((struct sockaddr_in *)dest_addr)->sin_port);

    if(strcmp(SM[sockfd].ip_addr, ip) != 0 || SM[sockfd].port != port){    
        errno = ENOTBOUND;
        V(semid, SMEM);
        return -1;
    }

    // check if send buffer is full
    if(SM[sockfd].send_buf_size == 0){      
        errno = ENOSPACE;
        V(semid, SMEM);
        return -1;
    }

    // find the first empty seq num
    int seq_num = SM[sockfd].swnd.start_seq;
    while(SM[sockfd].swnd.wndw[seq_num] != -1){      
        seq_num = (seq_num + 1) % 256;  
    }

    // find the first empty buffer index
    int idx = 0, flag = 1;
    for(idx = 0; idx < 10; idx++){
        flag = 1;
        for(int i = 0; i < 256; i++){
            if(SM[sockfd].swnd.wndw[i] == idx){
                flag = 0;
                break;
            }
        }
        if(flag) break;
    }

    int newlen = len > 512 ? 512 : len; 

    // store in info in shared memory
    SM[sockfd].swnd.wndw[seq_num] = idx;             
    memcpy(SM[sockfd].send_buf[idx], buf, newlen);
    SM[sockfd].last_sent[seq_num] = -1;
    SM[sockfd].send_buf_size--;
    SM[sockfd].send_msg_len[idx] = newlen;

    // unlock shared memory
    V(semid, SMEM);
    return newlen;
}

/*
    -------------------------------KRECVFROM--------------------------------
    Copy the message from recv buffer in shared memory.
    If no message in shared mem set errno to ENOMESSAGE.
*/
int k_recvfrom(int sockfd, void *buf, int len, struct sockaddr *src_addr, socklen_t *addrlen) {
    // lock shared memory
    P(semid, SMEM);

    if (sockfd < 0 || sockfd >= N || SM[sockfd].available == 1) {
        errno = EBADF;
        V(semid, SMEM);
        return -1;
    }

    // if its a new message
    if (SM[sockfd].is_unique[SM[sockfd].recv_next_idx]) {

        int idx = SM[sockfd].recv_next_idx;

        SM[sockfd].is_unique[idx] = 0;
        SM[sockfd].rwnd.size++;

        // find the seq num of the message
        int seq = -1;
        for (int i = 0; i < 256; i++) {
            if (SM[sockfd].rwnd.wndw[i] == idx) seq = i;
        }    

        SM[sockfd].rwnd.wndw[seq] = -1;
        SM[sockfd].rwnd.wndw[(seq + 10) % 256] = idx;

        int sm_length = SM[sockfd].recv_msg_len[idx];
        memcpy(buf, SM[sockfd].recv_buf[idx], (len < sm_length) ? len : sm_length);

        SM[sockfd].recv_next_idx = (idx + 1) % 10; 
        V(semid, SMEM);
        return (len < sm_length) ? len : sm_length;
    }
    errno = ENOMESSAGE;
    V(semid, SMEM);
    return -1;
}

/*
    -------------------------------KCLOSE--------------------------------
    Close the socket and make it available.
*/
int k_close(int sockfd) {
    P(semid, SMEM);
    SM[sockfd].available = 1;
    close(SM[sockfd].sock_id);
    SM[sockfd].sock_id = -1;
    V(semid, SMEM);
    return 0;
}