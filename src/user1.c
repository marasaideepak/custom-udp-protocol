/*
=====================================
Assignment 4 Submission
Name: Sai Deepak Reddy Mara
Roll number: 22CS10066
FILE: user1.c
=====================================
*/

#include <stdio.h>
#include "ksocket.h"
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

int sockfd; 

void handle_SIGINT(int signo) {
    k_close(sockfd);
    printf("Socket closed due to interrupt\n");
    exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        printf("Usage: %s <src_IP> <src_port> <dest_IP> <dest_port>\n", argv[0]);
        return 1;
    }

    // Handle SIGINT
    signal(SIGINT, handle_SIGINT);

    struct sockaddr_in serverAddr;
    char buffer[512];

    // socket creation
    if ((sockfd = k_socket(AF_INET, SOCK_KTP, 0)) < 0) {
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
    if (k_bind(sockfd, src_ip, src_port, dest_ip, dest_port) < 0) {
        perror("Error in binding");
        k_close(sockfd);
        return 1;
    }
    printf("Socket binding successful\n");

    // Open input file
    FILE *fp = fopen("input.txt", "r");
    if (!fp) {
        perror("Error opening input file");
        k_close(sockfd);
        return 1;
    }

    // set the dest address
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(dest_port);

    if (inet_pton(AF_INET, dest_ip, &serverAddr.sin_addr) <= 0) {
        perror("Error in inet_pton");
        fclose(fp);
        k_close(sockfd);
        return 1;
    }

    printf("Reading file and sending data...\n");
    int len, n, r_cnt;
    buffer[0] = '0';  
    int cnt = 0;

    while ((len = fread(buffer + 1, 1, 511, fp)) > 0) {
        printf("Sending message #%d with %d bytes\n", cnt + 1, len + 1);
        r_cnt = 0;

        while (1) {
            n = k_sendto(sockfd, buffer, len + 1, (struct sockaddr*)&serverAddr, sizeof(serverAddr));

            if (n < 0) {
                if (errno == ENOSPACE) {
                    printf("Send buffer full, retrying (attempt %d)...\n", ++r_cnt);
                    sleep(1);
                    continue;
                } else {
                    perror("Error in sending");
                    fclose(fp);
                    k_close(sockfd);
                    return 1;
                }
            }
            break;
        }

        printf("Successfully sent message #%d: %d bytes\n", ++cnt, n);
    }

    fclose(fp);  

    printf("Sending EOF...\n");
    buffer[0] = '1';  
    r_cnt = 0;

    while (1) {
        n = k_sendto(sockfd, buffer, 1, (struct sockaddr*)&serverAddr, sizeof(serverAddr));

        if (n < 0) {
            if (errno == ENOSPACE) {
                printf("Send buffer full, retrying EOF send (attempt %d)...\n", ++r_cnt);
                sleep(1);
                continue;
            } else {
                perror("Error in sending EOF");
                k_close(sockfd);
                return 1;
            }
        }
        break;
    }

    printf("File transfer complete. Total messages sent (excluding EOF) = %d\n", cnt);
    
    // might need to increase sleep time for larger files
    // also if multiple users are running the program
    // also if probability is higher than 0.5
    sleep(300 * p);

    printf("Closing KTP socket...\n");
    if (k_close(sockfd) < 0) {
        perror("Error in closing socket");
        return 1;
    }
    printf("Socket closed successfully\n");

    return 0;
}
