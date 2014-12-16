#define _GNU_SOURCE
#include <endian.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

/* Sample UDP client */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>

unsigned int send_pkg = 0;
unsigned int recv_ack = 0;

void create_w_header(char *b, unsigned int address, unsigned int length)
{
    b[0] = 'w';
    b[1]=(address>>0)&0x0FF;
    b[2]=(address>>8)&0x0FF;
    b[3]=(address>>16)&0x0FF;
    b[4]=(address>>24)&0x0FF;
    b[5]=(length>>0)&0x0FF;
    b[6]=(length>>8)&0x0FF;
}
void create_r_header(char *b, unsigned int address, unsigned int length)
{
    b[0] = 'r';
    b[1]=(address>>0)&0x0FF;
    b[2]=(address>>8)&0x0FF;
    b[3]=(address>>16)&0x0FF;
    b[4]=(address>>24)&0x0FF;
    b[5]=(length>>0)&0x0FF;
    b[6]=(length>>8)&0x0FF;
}

/**
 * Receive the response from the BOARD.
 * This should be an ACK (or NACK)
 */
void receiving_response ( int sockfd) 
{
    size_t t;
    struct sockaddr_in cliaddr;
    char mesg[64];
    socklen_t len = sizeof(cliaddr);

    // Receive 
    t = recvfrom(sockfd,
            mesg,
            8,
            0,
            (struct sockaddr *)&cliaddr,
            &len);
    // Terminate the message.
    mesg[t] = '\0';
    recv_ack++;
}

/**
 * Send packet
 */
size_t send_packet ( int sockfd, struct sockaddr_in *in, char *msg, int length)
{
    size_t t = sendto(sockfd,msg,length,0,
            (struct sockaddr *)in,sizeof(struct sockaddr_in));
    send_pkg++;
    return t;
}


void recv_data ( int sockfd, char *msg, size_t *length)
{
    struct sockaddr_in cliaddr;
    socklen_t len = sizeof(cliaddr);

    // Receive 
    *length = recvfrom(sockfd,
            msg,
            1024,
            0,
            (struct sockaddr *)&cliaddr,
            &len);
    // Terminate the message.
    msg[*length] = '\0';
} 

void command_read(const char *ip, int argc, char **argv)
{
    int sockfd;
    struct sockaddr_in servaddr;
    unsigned int address = 0;
    unsigned int length = 0;

    if(argc != 2) {
        fprintf(stderr, "Pass an address and length.\n");
        exit(EXIT_FAILURE);
    }
    address = strtol(argv[0], NULL, 16);
    length  = strtol(argv[1], NULL, 16);

    fprintf(stderr, "Read 0x%08X bytes from 0x%08X\n", length, address);
    // Open socket
    sockfd=socket(AF_INET,SOCK_DGRAM,0);
    if(sockfd <  0) {
        fprintf(stderr, "Failed to open socket: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Listen on the port, set destination. 
    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr=inet_addr(ip);
    servaddr.sin_port=htons(5000);
    // Start listening.
    bind(sockfd,(struct sockaddr *)&servaddr,sizeof(servaddr));
    // Create read message.
    fprintf(stderr, "Reading: % 10u", length);
    do {
        int m_l= (length > 1024)?1024:length;
        char msg[7];
        create_r_header(msg, address, m_l);
        // Send header.
        size_t t = send_packet(sockfd, &servaddr, msg, 7);
        if( t == -1 )  {
            fprintf(stderr, "Failed to send message: %s\n", strerror(errno));
            close(sockfd);
            exit(EXIT_FAILURE);
        } else if ( t == 7 ) {
            size_t m_r = 0;
            // we can read package
            char package[1025];
            recv_data ( sockfd, package, &m_r);
            fprintf(stderr, "\rReading: % 10u", length);
            fwrite(package, m_r, 1, stdout);
            length-=m_r;
            address+=m_r;
        }
    }while(length >0);

    // new line
    fprintf(stderr, "\rReading: % 10u", length);
    fprintf(stderr, "\n");
    // Close
    close(sockfd);
}

void command_write(const char *ip, int argc, char **argv)
{
    int sockfd;
    struct sockaddr_in servaddr;
    unsigned int address_start = 0, address = 0;

    if(argc != 1) {
        fprintf(stderr, "Pass an address.\n");
        exit(EXIT_FAILURE);
    }
    address_start = address = strtol(argv[0], NULL, 16);

    fprintf(stderr, "Write to 0x%08X\n", address);
    // Open socket
    sockfd=socket(AF_INET,SOCK_DGRAM,0);
    if(sockfd <  0) {
        fprintf(stderr, "Failed to open socket: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Listen on the port, set destination. 
    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr=inet_addr(ip);
    servaddr.sin_port=htons(5000);
    // Start listening.
    bind(sockfd,(struct sockaddr *)&servaddr,sizeof(servaddr));
    // Create read message.
    size_t l = 0;
    char buffer[1024+7];
    fprintf(stderr, "Writing: % 10u", address - address_start);
    while((l = fread(&buffer[7], 1, 1024, stdin)) > 0)
    {
        create_w_header(buffer, address, l);

        send_packet(sockfd, &servaddr, buffer, l+7);
        receiving_response(sockfd);
        address+=l;
        fprintf(stderr, "\rWriting: % 10u", address - address_start);
    }

    fprintf(stderr, "\rWriting: % 10u\n", address - address_start);
    // Close
    close(sockfd);
}

/**
 * Set a single word at memory.
 */
void command_set (const char *ip, int argc, char **argv)
{
    int sockfd;
    struct sockaddr_in servaddr;
    unsigned int address = 0;

    if(argc != 2) {
        fprintf(stderr, "Pass an address.\n");
        exit(EXIT_FAILURE);
    }
    address = strtol(argv[0], NULL, 16);
    unsigned int value = strtol(argv[1], NULL, 16);


    fprintf(stderr, "Write to 0x%08X value: %08X\n", address,value);

    // Convert to right format.
    value = htobe32(value);
    // Open socket
    sockfd=socket(AF_INET,SOCK_DGRAM,0);
    if(sockfd <  0) {
        fprintf(stderr, "Failed to open socket: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Listen on the port, set destination. 
    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr=inet_addr(ip);
    servaddr.sin_port=htons(5000);
    // Start listening.
    bind(sockfd,(struct sockaddr *)&servaddr,sizeof(servaddr));
    // Create read message.
    char buffer[4+7];
    create_w_header(buffer, address, 4);
    buffer[7] = (value>>0)&0x0FF;
    buffer[8] = (value>>8)&0x0FF;
    buffer[9] = (value>>16)&0x0FF;
    buffer[10]= (value>>24)&0x0FF;

    send_packet(sockfd, &servaddr, buffer, 4+7);
    receiving_response(sockfd);
    // Close
    close(sockfd);
}

/**
 * Get a single word from the memory.
 */
void command_get(const char *ip, int argc, char **argv)
{
    int sockfd;
    struct sockaddr_in servaddr;
    unsigned int address_start = 0, address = 0;

    if(argc != 1) {
        fprintf(stderr, "Pass an address.\n");
        exit(EXIT_FAILURE);
    }
    address_start = address = strtol(argv[0], NULL, 16);

    fprintf(stderr, "Write to 0x%08X\n", address);
    // Open socket
    sockfd=socket(AF_INET,SOCK_DGRAM,0);
    if(sockfd <  0) {
        fprintf(stderr, "Failed to open socket: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Listen on the port, set destination. 
    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr=inet_addr(ip);
    servaddr.sin_port=htons(5000);
    // Start listening.
    bind(sockfd,(struct sockaddr *)&servaddr,sizeof(servaddr));
    // Create read message.
    size_t l = 0;
    char buffer[7];
    create_r_header(buffer, address, 4);

    send_packet(sockfd, &servaddr, buffer, 7);
    char package[1025];
    size_t m_r;
    recv_data ( sockfd, package, &m_r);
    unsigned int value = (package[0] | package[1] << 8 | package[2] << 16 | package[3] << 24);
    value = be32toh(value);
    printf("Value: 0x%08X\n", value);
    // Close
    close(sockfd);
}

int main(int argc, char**argv)
{
    int sockfd,n;
    struct sockaddr_in servaddr,cliaddr;

    if ( argc < 3 )  {
        fprintf(stderr, "Insufficient command.\n");
        exit (1);
    }
    const char *ip = argv[1];
    const char *command = argv[2];

    // Write from stdin to memory
    if( strcmp(command, "write") == 0 ) {
        command_write (ip, argc-3, &argv[3] );
    }
    // Read from memory to stdout
    else if ( strcmp(command, "read") == 0 ) {
        command_read (ip, argc-3, &argv[3] );
    }
    // Set memory address to value
    else if ( strcmp(command, "set")  == 0 ) {
        command_set ( ip, argc-3, &argv[3] );
    }
    // Get memory address
    else if ( strcmp(command, "get")  == 0 ) {
        command_get ( ip, argc-3, &argv[3] );
    }else { 
        fprintf(stderr,"Select either read/write command.\n"); 
        exit(1);
    }

    return EXIT_SUCCESS;
}

