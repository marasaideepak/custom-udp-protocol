/*
=====================================
Assignment 4 Submission
Name: Sai Deepak Reddy Mara
Roll number: 22CS10066
FILE: user2.c
=====================================
*/


#include <stdio.h>
#include "ksocket.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <signal.h>

int sockfd;

void handle_SIGINT(int sockfd) {
    k_close(sockfd);
    printf("Socket closed due to interrupt\n");
    exit(0);
}

int main(int argc, char *argv[]){
    if(argc!=5){
        printf("Usage: %s <src_IP> <src_port> <dest_IP> <dest_port>\n", argv[0]);
        return 1;
    }

    // to handle SIGINT
    signal(SIGINT, handle_SIGINT);
    
    struct sockaddr_in serverAddr;
    char buffer[512]; 
    socklen_t len;

    // socket creation
    if((sockfd = k_socket(AF_INET, SOCK_KTP, 0)) < 0){
        perror("Error in socket creation");
        return 1;
    }
    printf("Socket created with id: %d\n", sockfd);
    
    char src_ip[16];
    strcpy(src_ip, argv[1]);
    char dest_ip[16];
    strcpy(dest_ip, argv[3]);

    int src_port = atoi(argv[2]);
    int dest_port = atoi(argv[4]);

    // binding
    if(k_bind(sockfd, src_ip, src_port, dest_ip, dest_port)<0){
        perror("Error in binding");
        return 1;
    }
    printf("Socket binding successful\n");

    char fp[100];
    sprintf(fp, "output_%d_to_%d.txt", src_port, dest_port);
    FILE *file = fopen(fp, "w");

    // set the dest address
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(dest_port);

    if(inet_pton(AF_INET, dest_ip, &serverAddr.sin_addr) <= 0) {
        perror("Error in inet_pton");
        k_close(sockfd);
        return 1;
    }

    printf("Receiving data...\n");
    int recvlen;
    int cnt = 0;
    len = sizeof(serverAddr);

    while (1) {
        printf("Waiting to receive message #%d...\n", cnt + 1);
        while ((recvlen = k_recvfrom(sockfd, buffer, sizeof(buffer), (struct sockaddr*)&serverAddr, &len)) <= 0) {
            if (errno != ENOMESSAGE) {
                printf("Error in k_recvfrom: %s\n", strerror(errno));
                sleep(1);
            } else {
                printf("No message available, waiting...\n");
                sleep(1);
            }
        }
        
        cnt++;
        printf("Received message #%d with length %d\n", cnt, recvlen - 1);
        
        if (buffer[0] == '1') {
            printf("EOF received\n");
            break;
        }

        printf("Writing %d bytes to file\n", recvlen - 1);
        fwrite(buffer + 1, 1, recvlen - 1, file);
        fflush(file);
        printf("Successfully wrote message #%d to file\n", cnt);
    }

    printf("File transmission complete. Total messages received: %d\n", cnt);
    fclose(file);

    // might need to increase sleep time for larger files
    // also if multiple users are running the program
    // also if probability is higher than 0.5
    sleep(300 * p);
    printf("Closing KTP socket...\n");
    if(k_close(sockfd) < 0){
        perror("Error in closing socket");
        return 1;
    }
    printf("Socket closed successfully\n");

    return 0;
}