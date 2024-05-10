
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <vector>
#include <assert.h>

static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

static void fd_set_nb(int fd) {
    errno = 0;
    // fcntl function call to set the non blocking mode for the given fd, 
	//F_GETFL-> to retrieve current status for the fd and save it in the flag
    int flags = fcntl(fd, F_GETFL, 0);
    //errno is set after the fcntl()
    if (errno) {
        die("fcntl error");
        return;
    }
	// Updates the flags by bitwise OR operation with O_NONBLOCK, setting the non-blocking mode.
    flags |= O_NONBLOCK;

    errno = 0;
    //Calls fcntl() with F_SETFL command to set the file status flags for the given file descriptor, including the non-blocking mode
    (void)fcntl(fd, F_SETFL, flags);
    if (errno) {
        die("fcntl error");
    }
}

const size_t k_max_msg = 4096;

enum {
    STATE_REQ = 0, // for reading
    STATE_RES = 1,
    STATE_END = 2,  // mark the connection for deletion
};

struct Conn {
    int fd = -1;
    uint32_t state = 0;     // either STATE_REQ or STATE_RES
    // buffer for reading
    size_t rbuf_size = 0;
    uint8_t rbuf[4 + k_max_msg];
    // buffer for writing
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[4 + k_max_msg];
};

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn) {
    if (fd2conn.size() <= (size_t)conn->fd) {
        fd2conn.resize(conn->fd + 1);
    }
    fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd) {
	// accept
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if (connfd < 0) {
        msg("accept() error");
        return -1;  // error
    }

    // set the new connection fd to nonblocking mode
    fd_set_nb(connfd);
    // creating the struct Conn
    struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
    if (!conn) {
        close(connfd);
        return -1;
    }
    conn->fd = connfd;
    conn->state = STATE_REQ;
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;
    conn_put(fd2conn, conn);
    return 0;
}
static void state_req(Conn *conn);
static void state_res(Conn *conn);

// The try_one_request function takes one request from the read buffer, generates a response, then transits to the STATE_RES state.
static bool try_one_request(Conn *conn){
	if (conn->rbuf_size < 4) {
        // not enough data in the buffer. Will retry in the next iteration
        return false;
    }
    uint32_t len=0;
    memcpy(&len,&conn->rbuf[0],4);
    if(len>k_max_msg){
    	msg("too long");
    	conn->state = STATE_END;
    	return false;
	}
	// checks if there's not enough data in the buffer to contain the entire request
	if(len+4>conn->rbuf_size){
		return false;
	}
	// got one request, do something with it
    printf("client says: %.*s\n", len, &conn->rbuf[4]);
    // generating echoing response
    memcpy(&conn->wbuf[0],&len,4);
    memcpy(&conn->wbuf[4],&conn->rbuf[4],len);
    conn->wbuf_size= 4+len;
    
    // remove the request from the buffer.
    // remove the request from the buffer.
    // note: frequent memmove is inefficient.
    // note: need better handling for production code.
    size_t remain = conn->rbuf_size-4-len;
    if(remain){
    	memmove(conn->rbuf,&conn->rbuf[4+len],remain);
	}
	conn->rbuf_size=remain;
	conn->state=STATE_RES;
	//now since the state is response so to handle the response we have to call state_res;
	state_res(conn);
	
	// continue the outer loop if the request was fully processed
	// if the state is still request then it will return true
    return (conn->state == STATE_REQ);
}

// try_fill_buffer() is responsible for attempting to fill the receive buffer of a connection with data from the client. 
// It handles errors, end-of-file conditions, and updates the buffer size accordingly. 
// Additionally, it processes requests from the buffer and returns whether more data needs to be read from the client.
static bool try_fill_buffer(Conn *conn){
	// This line ensures that the current size of the receive buffer (conn->rbuf_size) is less than the total size of the buffer (sizeof(conn->rbuf)). 
	// If this condition is not met, it will trigger an assertion error, indicating a programming error.
	assert(conn->rbuf_size<(sizeof(conn->rbuf)));
	// rv will store the return value of the read() system call
	ssize_t rv=0;
	// as the return value of read() (rv) is less than 0 (indicating an error) and the error is EINTR
	do{
		size_t cap = sizeof(conn->rbuf) - conn->rbuf_size; //remaining capacity (cap) of the receive buffer
		// to read data from the fd associated with the connection (conn->fd) into the receive buffer (conn->rbuf). 
		// It reads up to cap bytes of data into the buffer starting at the position &conn->rbuf[conn->rbuf_size]
		rv = read(conn->fd ,&conn->rbuf[conn->rbuf_size], cap);
	}while(rv<0 && errno==EINTR);
	// EAGAIN (indicating that there is no more data available to read at the moment
	if(rv<0 && errno==EAGAIN){
		return false;
	}
	if(rv<0){
		msg("read() error");
		conn->state=STATE_END;
		return false;
	}
	// (rv) is 0, indicating that the end of the file has been reached (EOF)
	if(rv==0){
		if(conn->rbuf_size>0)
			msg("Unexpected EOF");
		else
			msg("EOF");
		conn->state=STATE_END;
		return false;
	}
	conn->rbuf_size+= (size_t)rv; // updates the size of the receive buffer by adding the number of bytes read (rv) into the buffer.
	assert(conn->rbuf_size<=(sizeof(conn->rbuf))); //buffer size does not exceed the total buffer capacity.
	// For a request/response protocol, clients are not limited to sending one request and waiting for the response at a time, 
	// clients can save some latency by sending multiple requests without waiting for responses in between, this mode of operation is called “pipelining”, so a loop is needed
	while(try_one_request(conn)){}
	return (conn->state==STATE_REQ);
}

static void state_req(Conn *conn){
	//  loop continues as long as the try_fill_buffer() function returns true
	while(try_fill_buffer(conn)){
	
	}
}

static bool try_flush_buffer(Conn *conn){
	ssize_t rv=0; // to store return value of write system call
	do{
		size_t remain = conn->wbuf_size - conn->wbuf_sent; // remaining number of bytes in the write buffer (conn->wbuf) that need to be flushed.
		// This attempts to write the remaining data from the write buffer (conn->wbuf) to the file descriptor
		rv= write(conn->fd, &conn->wbuf[conn->wbuf_sent],remain);
	}while(rv<0 && errno==EINTR); //EINTR (indicating that the operation was interrupted and should be retried).
	
	if(rv<0 && errno==EAGAIN){
		//EAGAIN (indicating that the operation would block and should be retried later)
		return false;
	}
	if(rv<0){
		msg("write() error");
		conn->state=STATE_END;
		return false;
	}
	conn->wbuf_sent+=rv; // updates the number of bytes already sent from the write buffer
	assert(conn->wbuf_sent<=conn->wbuf_size);
	if(conn->wbuf_sent==conn->wbuf_size){
		conn->state= STATE_REQ;
		conn->wbuf_sent=0;
		conn->wbuf_size=0;
		return false;
	}
	return true;
}

static void state_res(Conn *conn){
	while(try_flush_buffer(conn)){
	}
}


// for handling I/O operations on a given connection (Conn object). if sstate of conn is req/res based on that it will call some func.
static void connection_io(Conn *conn){
	if(conn->state==STATE_REQ){
		state_req(conn);
	}else if(conn->state==STATE_RES){
		state_res(conn);
	}else{
		assert(0);
	}
}

int main(){
//	int fd = socket(AF_INET, SOCK_STREAM, 0); // AF_INET-> IPV4 protocol, SOCK_STREAM-> TCP connection, SOCK_DGRAM-UDP Connection
//	
//	int val = 1;
//	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
//	//setsockopt-> configuring tcp connection after socket has been created
//	
//	 // bind
//    struct sockaddr_in addr = {};
//    addr.sin_family = AF_INET;
//    addr.sin_port = ntohs(1234);
//    addr.sin_addr.s_addr = ntohl(0);    // wildcard address 0.0.0.0
//    int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));
//    if (rv) {
//        die("bind()");
//    }
//
//    // listen
//    rv = listen(fd, SOMAXCONN);
//    if (rv) {
//        die("listen()");
//    }
//    // a map of all cient connection, keyed by fd
//    std:: vector<Conn *> fd2conn;
//    
//    //then we have to set the listen fd to non blocking mode
//    fd_set_nb(fd);
//    //event loop
//    std:: vector<struct pollfd> poll_args;
//    while(true){
//    	// clears the poll_args vector, which will hold the file descriptors to be polled.
//    	// poll has 3 parameter, fd,POLLIN/POLLOUT, 0-> setting revents paramete.
//		// revents is an output parameter that will be filled by the poll() function to indicate which events actually occurred on the fd
//    	poll_args.clear();
//    	// for convenience, the listening fd is put in the first position
//    	struct pollfd pfd = {fd,POLLIN,0};
//    	poll_args.push_back(pfd);
//    	// for every client connection we got prepares struct pollfd objects (pfd). so preparing for poll
//		for(Conn *conn: fd2conn){
//			if(!conn)
//				continue;
//			// declaring an empty pollfd and then setting the arguements based on request type
//			struct pollfd pfd={};
//			pfd.fd=conn->fd;
//			pfd.events= (conn->state==STATE_REQ) ? POLLIN:POLLOUT; // if it is a request then we are setting POLLIN otherwise POLLOUT
//			pfd.events = conn->state | POLLERR; //if any error
//			poll_args.push_back(pfd);
//		}
//		//finally calling poll system call with 1000 timeout
//		//nfds_t->An unsigned integer type used for the number of fd
//		//rv indicates the number of file descriptors that have events or an error occurred.
//		// the timeout argument doesn't matter here
//		int rv = poll(poll_args.data(),(nfds_t)poll_args.size(),1000);
//		if(rv)
//			die("poll");
//		//skipping the 1st one because it is a listening socket
//		for(size_t i=1;i<poll_args.size();++i){
//			
//			if(poll_args[i].revents){ //checking if any event is there for aparticular conn
//				Conn *conn = fd2conn[poll_args[i].fd];
//				connection_io(conn);
//				// STATE_END means the client closed the connection or something went wrong, so the connection is cleaned up by setting its entry in fd2conn to NULL,
//				// closing the fd, and freeing the memory allocated for the connection object.
//				if(conn->state==STATE_END){
//					fd2conn[conn->fd] = NULL;
//					close(conn->fd);
//					free(conn);
//				}
//			}
//		}
//		// try to accept a new connection if the listening fd is active
//        if (poll_args[0].revents) {
//            (void)accept_new_conn(fd2conn, fd);
//        }
//	}
//	return 0;
int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket()");
    }

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // bind
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(0);    // wildcard address 0.0.0.0
    int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));
    if (rv) {
        die("bind()");
    }

    // listen
    rv = listen(fd, SOMAXCONN);
    if (rv) {
        die("listen()");
    }

    // a map of all client connections, keyed by fd
    std::vector<Conn *> fd2conn;

    // set the listen fd to nonblocking mode
    fd_set_nb(fd);

    // the event loop
    std::vector<struct pollfd> poll_args;
    while (true) {
        // prepare the arguments of the poll()
        poll_args.clear();
        // for convenience, the listening fd is put in the first position
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);
        // connection fds
        for (Conn *conn : fd2conn) {
            if (!conn) {
                continue;
            }
            struct pollfd pfd = {};
            pfd.fd = conn->fd;
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events = pfd.events | POLLERR;
            poll_args.push_back(pfd);
        }

        // poll for active fds
        // the timeout argument doesn't matter here
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
        if (rv < 0) {
            die("poll");
        }

        // process active connections
        for (size_t i = 1; i < poll_args.size(); ++i) {
            if (poll_args[i].revents) {
                Conn *conn = fd2conn[poll_args[i].fd];
                connection_io(conn);
                if (conn->state == STATE_END) {
                    // client closed normally, or something bad happened.
                    // destroy this connection
                    fd2conn[conn->fd] = NULL;
                    (void)close(conn->fd);
                    free(conn);
                }
            }
        }

        // try to accept a new connection if the listening fd is active
        if (poll_args[0].revents) {
            (void)accept_new_conn(fd2conn, fd);
        }
    }

    return 0;
}