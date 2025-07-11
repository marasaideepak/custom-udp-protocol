/*
=====================================
Assignment 4 Submission
Name: Sai Deepak Reddy Mara
Roll number: 22CS10066
FILE: initksocket.c
=====================================
*/

#include <pthread.h>
#include "ksocket.h"
#include <time.h>

// for statistics
int total_transmissions;
int total_messages;
int retrans;

// this ensures that window update is sent every T seconds
int last_wndw_update[N];

/*
    HELPER FUNCTIONS
*/

void handle_SIGINT(int sig){
    shmdt(sock_info);
    shmdt(SM);
    shmctl(shmid_sock_info, IPC_RMID, NULL);
    shmctl(shmid_SM, IPC_RMID, NULL);
    semctl(semid, 0, IPC_RMID);
    printf("Interrupt received, cleaning up and exiting\n");
    printf("Thanks for using KTP\n");
    exit(0);
}

char* curr_time() {
    static char buffer[30];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, sizeof(buffer), "%H:%M:%S", t);
    return buffer;
}

/*
    THREAD R
    HANDLES RECEIVING PACKETS
*/

void* R() {
    printf("[%s] Thread R started\n", curr_time());
    
    fd_set read_fds;
    FD_ZERO(&read_fds);
    int max_fd = 0;
    
    while (1) {
        fd_set temp_fds = read_fds;
        struct timeval tv;
        tv.tv_sec = T;
        tv.tv_usec = 0;
        
        printf("[%s] Thread R: Waiting for activity with timeout %d seconds\n", curr_time(), T);
        
        // timeout 
        if ((select(max_fd + 1, &temp_fds, NULL, NULL, &tv)) <= 0) {
            printf("[%s] Thread R: Select timeout, refreshing socket set\n", curr_time());
            FD_ZERO(&read_fds);
            max_fd = 0;
            P(semid, SMEM);
            // add to read_fds
            for (int i = 0; i < N; i++) {
                if (SM[i].available == 0) {
                    FD_SET(SM[i].sock_id, &read_fds);
                    if (SM[i].sock_id > max_fd) max_fd = SM[i].sock_id;

                    int current_time = time(NULL);
                    // if no space in recv buffer, send an ACK with updated window size
                    //  and if no activity for T seconds ressend the ACK
                    if ((SM[i].nospace || last_wndw_update[i] > 0) && SM[i].rwnd.size > 0 && current_time - last_wndw_update[i] >= T) {

                        last_wndw_update[i] = current_time;
                        SM[i].nospace = 0;
                        
                        // send an ACK with updated window size
                        int lastseq = ((SM[i].rwnd.start_seq + 256 ) - 1) % 256;
                        printf("[%s] Socket %d: Sending window update ACK, seq = %d, rwnd = %d\n", curr_time(), i, lastseq, SM[i].rwnd.size);
                        
                        struct sockaddr_in cliaddr;
                        cliaddr.sin_family = AF_INET;
                        inet_aton(SM[i].ip_addr, &cliaddr.sin_addr);
                        cliaddr.sin_port = htons(SM[i].port);

                        // prep ack message
                        char ack[sizeof(struct ktp_header)];
                        struct ktp_header *ack_header = (struct ktp_header *)ack;
                        ack_header->type = 1;
                        ack_header->seq_num = lastseq;
                        ack_header->wnd_size = SM[i].rwnd.size;
                        ack_header->len = 0;
                        
                        sendto(SM[i].sock_id, ack, sizeof(struct ktp_header), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
                    }
                }
            }
            V(semid, SMEM);
        }
        // socket activity
        else {
            printf("[%s] Thread R: Activity detected on some socket\n", curr_time());
            P(semid, SMEM);
            for (int i = 0; i < N; i++) {
                if (FD_ISSET(SM[i].sock_id, &temp_fds)) {
                    printf("[%s] Socket %d: Ready to read\n", curr_time(), i);
                    
                    char buffer[sizeof(struct ktp_header) + 512];  
                    struct sockaddr_in cliaddr;
                    unsigned int len = sizeof(cliaddr);
                    int n = recvfrom(SM[i].sock_id, buffer, sizeof(buffer), 0, (struct sockaddr *)&cliaddr, &len);
                    
                    if (dropMessage(p)) {
                        printf("[%s] Socket %d: Message dropped (probability p = %.2f)\n", curr_time(), i, p);
                        continue;
                    }
                    
                    if (n < 0) perror("recvfrom failed");
                    else {
                        struct ktp_header *header = (struct ktp_header *)buffer;
                        
                        // if ACK message
                        if (header->type == 1) { 
                            uint8_t seq_num = header->seq_num;
                            int rwnd_size = header->wnd_size;
                            
                            printf("[%s] Socket %d: Received ACK for seq = %d, rwnd = %d\n", curr_time(), i, seq_num, rwnd_size);
                            
                            // handle DUPACK
                            if(SM[i].swnd.wndw[seq_num] == -1) {
                                printf("[%s] Socket %d: DUPACK for seq = %d\n", curr_time(), i, seq_num);
                            }
                            // handle normal ACK
                            else {
                                printf("[%s] Socket %d: Valid ACK for unacknowledged message\n", curr_time(), i);
                                int prev_seq = SM[i].swnd.start_seq;
                                
                                uint8_t curr_seq = SM[i].swnd.start_seq;
                                uint8_t next_seq = (seq_num + 1) % 256;

                                // acknowledge all messages till seq_num
                                while (curr_seq != next_seq) {
                                    printf("[%s] Socket %d: Acknowledging seq = %d\n", curr_time(), i, curr_seq);
                                    SM[i].swnd.wndw[curr_seq] = -1;
                                    SM[i].last_sent[curr_seq] = -1;
                                    SM[i].send_buf_size++;
                                    curr_seq = (curr_seq + 1) % 256;
                                    total_messages++;
                                    total_transmissions++;
                                }

                                // update swnd
                                SM[i].swnd.start_seq = (seq_num + 1) % 256;
                                
                                printf("[%s] Socket %d: swnd moved from %d to %d\n", curr_time(), i, prev_seq, SM[i].swnd.start_seq);
                            }
                            int prev_size = SM[i].swnd.size;
                            SM[i].swnd.size = rwnd_size;
                            printf("[%s] Socket %d: swnd size updated from %d to %d\n", curr_time(), i, prev_size, rwnd_size);
                        }
                        // if DATA message
                        else { 
                            uint8_t seq_num = header->seq_num;
                            int data_len = header->len;
                            
                            printf("[%s] Socket %d: Received DATA seq = %d, len = %d bytes\n", curr_time(), i, seq_num, data_len);

                            uint8_t expected_seq = (uint8_t)SM[i].rwnd.start_seq;       
                            
                            if (seq_num == expected_seq) {
                                printf("[%s] Socket %d: In-order message received (seq = %d)\n", curr_time(), i, seq_num);
     
                                int buf_idx = SM[i].rwnd.wndw[seq_num];
                                if (buf_idx >= 0) {
                                    // Copy just the data part (skip the header) to buffer at buf_idx
                                    memcpy(SM[i].recv_buf[buf_idx], buffer + sizeof(struct ktp_header), data_len);
                                    SM[i].is_unique[buf_idx] = 1;
                                    SM[i].rwnd.size--;
                                    SM[i].recv_msg_len[buf_idx] = data_len;

                                    int prev_seq = SM[i].rwnd.start_seq;
                                    int curr_seq = SM[i].rwnd.start_seq;

                                    // update rwnd till a expecting seq num is found
                                    while(SM[i].rwnd.wndw[curr_seq] >= 0 && SM[i].is_unique[SM[i].rwnd.wndw[curr_seq]] == 1) {
                                        curr_seq = (curr_seq + 1) % 256;
                                    }

                                    SM[i].rwnd.start_seq = curr_seq;
                                    
                                    if (prev_seq != SM[i].rwnd.start_seq) {
                                        printf("[%s] Socket %d: rwnd moved from %d to %d\n", curr_time(), i, prev_seq, SM[i].rwnd.start_seq);
                                    }
                                    
                                    // send ACK for the new expected seq num
                                    int lastseq = (SM[i].rwnd.start_seq + 255) % 256;
                                    printf("[%s] Socket %d: Sending ACK for seq = %d, rwnd = %d\n", curr_time(), i, lastseq, SM[i].rwnd.size);
                                    
                                    // prep the ack packet
                                    char ack[sizeof(struct ktp_header)];
                                    struct ktp_header *ack_header = (struct ktp_header *)ack;
                                    ack_header->type = 1; 
                                    ack_header->seq_num = lastseq;
                                    ack_header->wnd_size = SM[i].rwnd.size;
                                    ack_header->len = 0;
                                    
                                    sendto(SM[i].sock_id, ack, sizeof(struct ktp_header), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
                                }
                            }
                            else {
                                printf("[%s] Socket %d: Out-of-order message received (seq = %d, expected = %d)\n", curr_time(), i, seq_num, SM[i].rwnd.start_seq);
                                
                                /// out of order, keep it in recv buffer but dont send ack
                                if (SM[i].rwnd.wndw[seq_num] >= 0 && SM[i].is_unique[SM[i].rwnd.wndw[seq_num]] == 0) {
                                    int buf_idx = SM[i].rwnd.wndw[seq_num];
                                    printf("[%s] Socket %d: Buffering out-of-order message (seq = %d)\n", curr_time(), i, seq_num);
                                    
                                    memcpy(SM[i].recv_buf[buf_idx], buffer + sizeof(struct ktp_header), data_len);
                                    SM[i].is_unique[buf_idx] = 1;
                                    SM[i].rwnd.size--;
                                    SM[i].recv_msg_len[buf_idx] = data_len;
                                }
                                // if duplicate, discard 
                                else  printf("[%s] Socket %d: Ignoring out-of-order message (seq = %d)\n", curr_time(), i, seq_num);
                                
                                // send an ack for expecting seq num
                                uint8_t lastseq = (SM[i].rwnd.start_seq + 255) % 256;
                                printf("[%s] Socket %d: Sending ACK for seq = %d, rwnd = %d\n", curr_time(), i, lastseq, SM[i].rwnd.size);
                                
                                char ack[sizeof(struct ktp_header)];
                                struct ktp_header *ack_header = (struct ktp_header *)ack;
                                ack_header->type = 1;  
                                ack_header->seq_num = lastseq;  
                                ack_header->wnd_size = SM[i].rwnd.size;
                                ack_header->len = 0;

                                sendto(SM[i].sock_id, ack, sizeof(struct ktp_header), 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
                            }
                            // no space in recv buffer
                            if (SM[i].rwnd.size == 0) {
                                printf("[%s] Socket %d: rwnd full (rwnd = %d)\n", curr_time(), i, SM[i].rwnd.size);
                                SM[i].nospace = 1;
                                last_wndw_update[i] = time(NULL);
                            }
                        }
                    }
                }
            }
            V(semid, SMEM);
        }
    }
}

/*
    THREAD S
    HANDLES SENDING PACKETS
*/

void* S(){
    printf("[%s] Thread S started\n", curr_time());
    
    while(1){
        sleep(T/2);
        printf("[%s] Thread S: Woke up, checking for timeouts and unsent messages\n", curr_time());
        
        P(semid, SMEM);
        for(int i = 0; i < N; i++){
            if(SM[i].available == 0){

                struct sockaddr_in serv_addr;
                serv_addr.sin_family = AF_INET;
                serv_addr.sin_port = htons(SM[i].port);
                inet_aton(SM[i].ip_addr, &serv_addr.sin_addr);

                int timeout = 0;
                uint8_t curr_seq = SM[i].swnd.start_seq;
                uint8_t end_seq = (SM[i].swnd.start_seq + SM[i].swnd.size) % 256;
                
                // find if for any seq, timeout has occured
                while(curr_seq != end_seq){
                    if(SM[i].last_sent[curr_seq] != -1 && time(NULL) - SM[i].last_sent[curr_seq] > T){
                        printf("[%s] Socket %d: TIMEOUT detected for seq = %d, last sent = %ld, now = %ld\n", curr_time(), i, curr_seq, SM[i].last_sent[curr_seq], time(NULL));
                        timeout=1;
                        break;
                    }
                    curr_seq = (curr_seq + 1) % 256;
                }
                
                // if timeout check for all unacknowledged messages
                if(timeout){
                    printf("[%s] Socket %d: Retransmitting all unacknowledged messages\n", curr_time(), i);
                    
                    curr_seq = SM[i].swnd.start_seq;
                    int start = SM[i].swnd.start_seq;
                    // for all in the swnd
                    while(curr_seq != (start + SM[i].swnd.size) % 256){
                        // if unacknowledged, retransmit
                        if(SM[i].swnd.wndw[curr_seq] != -1){
                            char buffer[sizeof(struct ktp_header) + 512];  
                            int buf_idx = SM[i].swnd.wndw[curr_seq];
                            int len = SM[i].send_msg_len[buf_idx];

                            printf("[%s] Socket %d: Retransmitting seq = %d, len = %d bytes\n", curr_time(), i, curr_seq, len);
                            
                            // prep the packet
                            struct ktp_header *header = (struct ktp_header *)buffer;
                            header->type = 0; 
                            header->seq_num = curr_seq;
                            header->wnd_size = SM[i].rwnd.size;
                            header->len = len;
                            
                            memcpy(buffer + sizeof(struct ktp_header), SM[i].send_buf[buf_idx], len);
                            
                            sendto(SM[i].sock_id, buffer, sizeof(struct ktp_header) + len, 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

                            total_transmissions++;
                            retrans++;

                            SM[i].last_sent[curr_seq] = time(NULL);
                        }
                        curr_seq = (curr_seq + 1) % 256;
                    }
                }

                // Send any new messages that haven't been sent yet
                curr_seq = SM[i].swnd.start_seq;
                int start = SM[i].swnd.start_seq;
                int cnt = 0;
                    
                while(curr_seq != (start + SM[i].swnd.size) % 256){
                    // if not sent yet, send
                    if(SM[i].swnd.wndw[curr_seq] != -1 && SM[i].last_sent[curr_seq] == -1){
                        cnt++;
                        char buffer[sizeof(struct ktp_header) + 512]; 
                        int buf_idx = SM[i].swnd.wndw[curr_seq];
                        int len = SM[i].send_msg_len[buf_idx];
                        
                        printf("[%s] Socket %d: Sending new message seq = %d, len = %d bytes\n", curr_time(), i, curr_seq, len);
                            
                        // prep the packet
                        struct ktp_header *header = (struct ktp_header *)buffer;
                        header->type = 0;
                        header->seq_num = curr_seq;
                        header->wnd_size = SM[i].rwnd.size;
                        header->len = len;
                            
                        memcpy(buffer + sizeof(struct ktp_header), SM[i].send_buf[buf_idx], len);
                            
                        sendto(SM[i].sock_id, buffer, sizeof(struct ktp_header) + len, 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

                        SM[i].last_sent[curr_seq] = time(NULL);
                    }
                    curr_seq = (curr_seq + 1) % 256;
                }
                if (cnt > 0) printf("[%s] Socket %d: Sent %d new messages\n", curr_time(), i, cnt);
            }
        }
        V(semid, SMEM);
    }
}

/*
    THREAD G
    GARBAGE COLLECTOR
*/

void* G() {
    printf("[%s] Thread G started\n", curr_time());
    while (1) {
        sleep(T); 
        printf("[%s] Thread G: Performing periodic cleanup...\n", curr_time());

        P(semid, SMEM);
        for (int i = 0; i < N; i++) {
            // skip if available or already terminated
            if (!SM[i].available) continue;
            if(SM[i].process_id == -1) continue;
            
            pid_t pid = SM[i].process_id;
            
            if (kill(pid, 0) == -1 && errno == ESRCH) {
                printf("[%s] Thread G: Process %d terminated, freeing entry %d\n", curr_time(), pid, i);
                SM[i].available = 1;
                SM[i].process_id = -1;

                // print stats for the terminated process
                printf("Total transmissions: %d\n", total_transmissions);
                printf("Total messages: %d\n", total_messages);
                printf("Retransmissions: %d\n", retrans);
                printf("Transmission rate: %f\n", (float)total_transmissions/total_messages);

                // close the socket
                close(SM[i].sock_id);
            }
        }
        V(semid, SMEM);
    }
}

/*
    MAIN FUNCTION
*/

int main() {
    printf("Starting KTP initialization process...\n");
    
    total_transmissions = 0;
    total_messages = 0;
    retrans = 0;

    // handle SIGINT
    signal(SIGINT, handle_SIGINT); 

    srand(time(0));

    // shared memory for socket info
    shmid_sock_info = shmget(ftok("/", 'D'), sizeof(struct socket_info), 0666|IPC_CREAT);

    // shared memory for N sockets
    shmid_SM = shmget(ftok("/", 'E'), sizeof(struct shared_mem) * N, 0666|IPC_CREAT);

    if(shmid_sock_info == -1 || shmid_SM == -1){
        perror("Error creating shared memory");
        exit(1);
    }

    // semaphores
    semid = semget(ftok("/", 'S'), 4, 0666|IPC_CREAT);
    
    if (semid == -1) {
        perror("Error creating semaphores");
        exit(1);
    }

    sock_info = (struct socket_info*)shmat(shmid_sock_info, 0, 0);
    SM = (struct shared_mem*)shmat(shmid_SM, 0, 0);

    // initialising sock_info
    sock_info->sock_id=0;
    sock_info->err_no=0;
    sock_info->ip_addr[0]='\0';
    sock_info->port=0;

    // make all available
    for(int i = 0; i < N; i++) SM[i].available = 1;

    // init semaphores
    unsigned short vals[4] = {1, 1, 0, 0}; 
    if(semctl(semid, 0, SETALL, vals) == -1){
        perror("Error setting semaphore values");
        exit(1);
    }

    for (int i = 0; i < N; i++) last_wndw_update[i] = 0;
    
    // create threads S R G
    printf("Creating threads...\n");

    pthread_t s_thread, r_thread, g_thread;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&s_thread, &attr, S, NULL);
    pthread_create(&r_thread, &attr, R, NULL);
    pthread_create(&g_thread, &attr, G, NULL);


    // this handles the socket creation and binding
    while(1){
        // wait for a socket request
        P(semid, SOCK_REQ);

        P(semid, SOCK);

        // create a new socket if not already created
        if(sock_info->sock_id == 0 && sock_info->ip_addr[0] == '\0' && sock_info->port == 0){
            int sock_id = socket(AF_INET, SOCK_DGRAM, 0);

            if(sock_id != -1) sock_info->sock_id = sock_id;
            else {
                sock_info->err_no = errno;
                sock_info->sock_id = -1;
            } 
        }
        // else bind the socket
        else{
            struct sockaddr_in serv_addr;
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(sock_info->port);
            inet_aton(sock_info->ip_addr, &serv_addr.sin_addr);

            if(bind(sock_info->sock_id, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
                sock_info->sock_id = -1;
                sock_info->err_no = errno;
            }
        }
        V(semid, SOCK);
        // signal socket is ready
        V(semid, SOCK_READY);
    }

    return 0;
}