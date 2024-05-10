#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
//#include <winsock2.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <assert.h>

const size_t k_max_msg = 4096;

static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

static int32_t read_full(int fd, char *buf, size_t n) {
    while (n > 0) {//it will only read for n byte exactly
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0) {
            return -1;  // error, or unexpected EOF
        }
        assert((size_t)rv <= n); //, it ensures that the number of bytes read (rv) is less than or equal to the remaining bytes to read (n) using an assert statement
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;//after all successful n read
}

static int32_t write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {//it will only write for n byte exactly
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0) {
            return -1;  // error
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

//handles single request at a time from client
static int32_t one_request(int connfd) {
    // 4 bytes header
    char rbuf[4 + k_max_msg + 1]; //4-byte header, a maximum message size of k_max_msg, and an additional byte for null termination.
    errno = 0;
    int32_t err = read_full(connfd, rbuf, 4);
    if (err) {//if there is an error
        if (errno == 0) {
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf, 4);  // assume little endian, extracts the length of the message body from the first 4 bytes of rbuf by using memcpy() to copy the bytes into the variable len.
    if (len > k_max_msg) {
        msg("too long");
        return -1;
    }

    // request body
    err = read_full(connfd, &rbuf[4], len); // reads the message body (of length len) from the connection socket into rbuf starting from the 5th byte (after the header).
    if (err) {
        msg("read() error");
        return err;
    }

    // do something
    rbuf[4 + len] = '\0';
    printf("client says: %s\n", &rbuf[4]);

    // reply using the same protocol
    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);
    return write_all(connfd, wbuf, 4 + len);
}

int main(){
	int fd=socket(AF_INET, SOCK_STREAM, 0); // AF_INET-> IPV4 protocol, SOCK_STREAM-> TCP connection, SOCK_DGRAM-UDP Connection
	
	int val = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
	
	struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(0);    // wildcard address 0.0.0.0
    //binding it with the fd, and address, sizeof(addr) is necessary because it is arbitrary. 
    int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));
    if(rv){
    	die("bind()");
	}
	rv = listen(fd, SOMAXCONN);
    if (rv) {
        die("listen()");
    }
    while(true){
    	// accept
        struct sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);
        // If a connection is accepted, it returns a new socket file descriptor connfd representing the connection.
        int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
        if (connfd < 0) {
            continue;   // error
        }
        while (true) {
            int32_t err = one_request(connfd);
            if (err) {
                break;
            }
        }
        close(connfd);
	}
	return 0;
}