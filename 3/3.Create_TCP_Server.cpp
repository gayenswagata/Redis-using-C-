#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
//#include <winsock2.h>
#include <sys/socket.h>
#include <netinet/ip.h>

static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

static void do_something(int connfd) {
    char rbuf[64] = {};
    // The read function returns the number of bytes read, or -1 if an error occurs.
    ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1); // Read data from the client into the rbuf buffer.
    if (n < 0) { // Check for read errors
        msg("read() error");
        return;
    }
    printf("client says: %s\n", rbuf);

    char wbuf[] = "world";// Message to be sent to the client
    write(connfd, wbuf, strlen(wbuf));
    // Writes the contents of wbuf to the client connected via the socket represented by connfd. It sends the message "world" back to the client.
}

int main(){
	//the socket() syscall allocates and returns a socket fd, which is used later to create a communication channel. fd-file descriptor
	int fd = socket(AF_INET, SOCK_STREAM, 0); // AF_INET-> IPV4 protocol, SOCK_STREAM-> TCP connection, SOCK_DGRAM-UDP Connection
	
	int val = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
	//setsockopt-> configuring tcp connection after socket has been created
	//SOL_SOCKET-> it is a socket;
	//SO_REUSEADDR-> SO_REUSEADDR is enabled (set to 1) for every listening socket. Without it, bind() will fail when you restart your server.
	// setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
	
	// bind, this is the syntax that deals with IPv4 addresses
	//The ntohs() and ntohl() functions convert numbers to the required big endian format.
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(0);    // wildcard address 0.0.0.0
    //binding it with the fd, and address, sizeof(addr) is necessary because it is arbitrary. 
    int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));
    
    //if bind fails return return non zero, here call the die function
    if (rv) {
        die("bind()");
    }
    
    // listen needs a backlog arguement in this case it is SOMAXCONN.
	// SOMAXCONN-> system-defined constant representing the maximum queue length for pending connections.
	// listen, OS will automatically create a TCP handshake, frpm client side it can be retrieved by accept().
    rv = listen(fd, SOMAXCONN);
    if (rv) {
        die("listen()");
    }
    
    //The server enters a loop that accepts and processes each client connection.
    //If an error is coming while connecting it will skip that connection and continue for the next ones.
    while (true) {
        // accept
        struct sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);
        // If a connection is accepted, it returns a new socket file descriptor connfd representing the connection.
        int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
        if (connfd < 0) {
            continue;   // error
        }

        do_something(connfd);
        close(connfd);
    }
    
    return 0;
}
