#include <assert.h>
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
#include <string>
#include <vector>
// proj
#include "hashtable.h"


#define container_of(ptr, type, member) ({                  \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
    (type *)( (char *)__mptr - offsetof(type, member) );})


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
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno) {
        die("fcntl error");
        return;
    }

    flags |= O_NONBLOCK;

    errno = 0;
    (void)fcntl(fd, F_SETFL, flags);
    if (errno) {
        die("fcntl error");
    }
}

const size_t k_max_msg = 4096;

enum {
    STATE_REQ = 0,
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

const size_t k_max_args = 1024;

//for interpreting and extracting structured information from raw binary data.

static int32_t parse_req(const uint8_t *data, size_t len, std::vector<std::string> &out){
	//	data: A pointer to an array of uint8_t, representing the data to be parsed.
	//	len: The length of the data array.
	//	out: A reference to a vector of strings, where the parsed components will be stored.
	if (len < 4) {
    	return -1;
	}
	uint32_t n =0;
	memcpy(&n,&data[0],4);
	if(n>k_max_args){
		return -1;
	}
	size_t pos = 4; // indicating the starting position to read the arguments from the data array after header.
    while(n--){
    	if(pos+4>len){// there are not enough bytes remaining in the input data to read the size of the next argument.
    		return -1;
		}
		uint32_t sz = 0;
		memcpy(&sz,&data[pos],4);
		if (pos + 4 + sz > len) { //it implies that there are insufficient bytes remaining in the input data array to fully parse the next argument.
			return -1;
		}
		out.push_back(std::string((char *)&data[pos+4],sz));
		pos+=4+sz;
	}
	//After parsing all arguments, this checks if there's any remaining data in the array (pos should be equal to len).
	if(pos!=len)
		return -1;
	return 0;
}

// The data structure for the key space.
static struct {
    HMap db;
} g_data;

// the structure for the key
struct Entry {
    struct HNode node;
    std::string key;
    std::string val;
};

static bool entry_eq(HNode *lhs, HNode *rhs) {
    struct Entry *le = container_of(lhs, struct Entry, node);
    struct Entry *re = container_of(rhs, struct Entry, node);
    return le->key == re->key;
}

// jenkins hash function
static uint64_t str_hash(const uint8_t *data, size_t len) {
	//good balance between distribution and simplicity. It's commonly used as an initial hash value in many hash functions.
    uint32_t h = 0x811C9DC5; // 32-bit unsigned integer h with the value 0x811C9DC5. This value is often used as an initial hash value in various hashing algorithms.
    for (size_t i = 0; i < len; i++) {
    	//  to mix the bits of the hash value. It helps in achieving a more even distribution of hash values across the range of possible hash values.
        h = (h + data[i]) * 0x01000193;
    }
    return h;
}

enum {
    ERR_UNKNOWN = 1,
    ERR_2BIG = 2,
};

//as per serialization protocol
enum {
    SER_NIL = 0,    // Like `NULL`
    SER_ERR = 1,    // An error code and message
    SER_STR = 2,    // A string
    SER_INT = 3,    // A int64
    SER_ARR = 4,    // Array
};

static void out_nil(std::string &out) {
    out.push_back(SER_NIL);
}

static void out_str(std::string &out, const std::string &val) {
    out.push_back(SER_STR);
    uint32_t len = (uint32_t)val.size();
    out.append((char *)&len, 4);
    out.append(val);
}

static void out_int(std::string &out, int64_t val) {
    out.push_back(SER_INT);
    out.append((char *)&val, 8);
}

static void out_arr(std::string &out, uint32_t n){
	// out: A reference to a string, which will hold the output generated by this function.
	// n: An unsigned 32-bit integer representing the number of elements in the array.
	
	//This line appends a special character (SER_ARR) to the out string. This character likely serves as an identifier for indicating that the following data represents an array.
	out.push_back(SER_ARR);
	// (char *)&n casts the integer n to a pointer to a character array, allowing it to be treated as a sequence of bytes.
	out.append((char *)&n,4); //4 specifies the number of bytes to append, assuming that n is represented as a 32-bit integer.
}

static void out_err(std::string &out, int32_t code, const std::string &msg) {
    out.push_back(SER_ERR);
    out.append((char *)&code, 4);
    uint32_t len = (uint32_t)msg.size();
    out.append((char *)&len, 4);
    out.append(msg);
}

static void do_get(std::vector<std::string> &cmd, std::string &out){
	Entry key;
	key.key.swap(cmd[1]);
	//generated jenkins hashcode for the key
	key.node.hcode = str_hash((uint8_t *)key.key.data(),key.key.size());
	
	HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
	if(!node){
		return out_nil(out);
	}
	const std::string &val = container_of(node, Entry, node)->val;
    out_str(out, val);
}

static void do_set(std::vector<std::string> &cmd, std::string &out){
	Entry key;
	key.key.swap(cmd[1]);
	key.node.hcode = str_hash((uint8_t *)key.key.data(),key.key.size());
	
	HNode *node = hm_lookup(&g_data.db,&key.node,&entry_eq);
	if(node){
		container_of(node,Entry,node)->val.swap(cmd[2]);
	}else{
		Entry *ent = new Entry();
		ent->key.swap(key.key);// since using an pointer object that is why ->.
		ent->val.swap(cmd[2]);
		ent->node.hcode = key.node.hcode;
		hm_insert(&g_data.db, &ent->node);
	}
	return out_nil(out);
}

static void do_del(std::vector<std::string> &cmd, std::string &out){
	Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
    
	HNode *node = hm_pop(&g_data.db, &key.node, &entry_eq);
    if (node) {
        delete container_of(node, Entry, node);
    }
    return out_int(out, node ? 1 : 0);
}
static void h_scan(HTab *tab, void (*f)(HNode *, void *), void *arg){
	// tab: A pointer to an HTab struct, representing the hash table to be scanned.
	// arg: A pointer to a void, which serves as an additional argument passed to the callback function  f
	
	if (tab->size == 0) {
        return;
    } //If the size is zero, it means that the hash table is empty, so there's no need to perform any scanning. In such case, the function simply returns early.
    // tab->mask is likely a bit mask used to determine the size of the hash table.
    for(size_t i=0;i<tab->mask;++i){
    	HNode *node = tab->tab[i]; //  initializes a pointer node to the first node in the current slot i of the hash table tab.
    	while(node){ // as long as there is data in the linked list node
    		f(node,arg);
    		node=node->next;
		}
	}
}

static void cb_scan(HNode *node, void *arg){
	//node: A pointer to a HNode struct, representing the current node being scanned.
	//arg: A pointer to a void, which serves as an additional argument passed to the function. In this case, it is assumed to be a pointer to a std::string object.
	
	//This line casts the arg pointer to a pointer to a std::string object and dereferences it to obtain the actual std::string object
	// this is done to avoide repeatedly casting of arg
	std::string &out = *(std::string *)arg;
	out_str(out, container_of(node, Entry, node)->key);
}

// function effectively generates an output containing information about the keys in the database by appending the number of keys and 
// performing scans on the hash tables to retrieve additional key-value pairs.
static void do_keys(std::vector<std::string> &cmd, std::string &out){
	(void)cmd;
	// generates an array output (out_arr) containing the number of keys in the database.
	// (uint32_t)hm_size(&g_data.db) retrieves the number of keys in the database (g_data.db) using the hm_size function,
	out_arr(out,(uint32_t)hm_size(&g_data.db));
	// These lines perform a scan operation on two hash tables (ht1 and ht2) within the database (g_data.db).
	h_scan(&g_data.db.ht1,&cb_scan,&out);
	h_scan(&g_data.db.ht2,&cb_scan,&out);
}

static bool cmd_is(const std::string &word, const char *cmd) {
    return 0 == strcasecmp(word.c_str(), cmd);
}

static void do_request(std::vector<std::string> &cmd, std::string &out){
	if(cmd.size()==1 && cmd_is(cmd[0],"keys")){
		do_keys(cmd,out);
	}else if(cmd.size()==2 && cmd_is(cmd[0],"get")){
		do_get(cmd,out);
	}else if(cmd.size()==2 && cmd_is(cmd[0],"del")){
		do_del(cmd,out);
	}else if(cmd.size()==3 && cmd_is(cmd[0],"set")){
		do_set(cmd,out);
	}else{
		// cmd is not recognized
        out_err(out, ERR_UNKNOWN, "Unknown cmd");
	}
}

static bool try_one_request(Conn *conn){
	if (conn->rbuf_size < 4) {
        // not enough data in the buffer. Will retry in the next iteration
        return false;
    }
    uint32_t len=0;
    memcpy(&len, &conn->rbuf[0],4);
    if(len>k_max_args){
    	msg("too long");
    	conn->state= STATE_END;
    	return false;
	}
	// checks if there's not enough data in the buffer to contain the entire request
	if(len+4>conn->rbuf_size){
		return false;
	}
	// parse the request
    std::vector<std::string> cmd;
    if(0!=parse_req(&conn->rbuf[4],len,cmd)){
    	msg("bad req");
    	conn->state=STATE_END;
    	return false;
	}
	// cmd will keep whatever was received from the server
	// got one request, generate the response.
    std::string out;
    // If the request was successfully parsed,it generates response using the parsed command (cmd) and stores it in a string variable out using the do_request function.
    do_request(cmd,out);
    // pack the response into the buffer
    if (4 + out.size() > k_max_msg){
    	out.clear();
        out_err(out, ERR_2BIG, "response is too big");
	}
    uint32_t wlen = 0;
    wlen = (uint32_t)out.size();
	memcpy(&conn->wbuf[0],&wlen,4);
	memcpy(&conn->wbuf[4],out.data(),out.size());
	conn->wbuf_size = 4+wlen; // it updates wbuf_Size 4 is header and wlen in the length of the data.
	
	// remove the request from the buffer.
    // note: frequent memmove is inefficient.
    // note: need better handling for production code.
    size_t remain = conn->rbuf_size - 4 - len;
    // Removes the parsed request from the receive buffer (rbuf) by shifting the remaining data to the beginning of the buffer. 
	// It calculates the size of the remaining data (remain) and updates the receive buffer size accordingly.
	if(remain){
		memmove(conn->rbuf,&conn->rbuf[4+len],remain);
	}
	conn->rbuf_size=remain;
	// indicating that a response is ready) and calls the state_res function. Finally, it returns true if the state of the connection is still STATE_REQ.
	conn->state = STATE_RES;
    state_res(conn);

    // continue the outer loop if the request was fully processed
    return (conn->state == STATE_REQ);
}
static bool try_fill_buffer(Conn * conn){
	assert(conn->rbuf_size<(sizeof(conn->rbuf)));
	// rv will store the return value of the read() system call
	ssize_t rv=0;
	do{
		size_t cap = sizeof(conn->rbuf) - conn->rbuf_size; //remaining capacity (cap) of the receive buffer
		// to read data from the fd associated with the connection (conn->fd) into the receive buffer (conn->rbuf). 
		// It reads up to cap bytes of data into the buffer starting at the position &conn->rbuf[conn->rbuf_size]
		rv = read(conn->fd ,&conn->rbuf[conn->rbuf_size], cap);
	}while(rv<0 && errno==EINTR);
	if(rv<0 && errno==EAGAIN){
		return false;
	}
	if(rv<0){
		msg("read() error");
		conn->state=STATE_END;
		return false;
	}
	if(rv == 0){
		if(conn->rbuf_size>0){
			msg("unexpecte EOF");
		}
		else
			msg("EOF");
		conn->state=STATE_END;
		return false;
	}
	conn->rbuf_size += (size_t)rv;
	assert(conn->rbuf_size<=(sizeof(conn->rbuf))); //buffer size does not exceed the total buffer capacity.
	// For a request/response protocol, clients are not limited to sending one request and waiting for the response at a time, 
	// clients can save some latency by sending multiple requests without waiting for responses in between, this mode of operation is called “pipelining”, so a loop is needed
	while(try_one_request(conn)){}
	return (conn->state==STATE_REQ);	
}
static void state_req(Conn *conn){
	while(try_fill_buffer(conn)){}
}

static bool try_flush_buffer(Conn *conn){
	ssize_t rv = 0;
	do{
		size_t remain = conn->wbuf_size - conn->wbuf_sent;
		rv= write(conn->fd,&conn->wbuf[conn->wbuf_sent],remain);
	}while(rv<0 && errno==EINTR);
	if(rv<0 && errno==EAGAIN){
		return false;
	}
	if(rv<0){
		msg("write() error");
		conn->state = STATE_END;
		return false;
	}
	conn->wbuf_sent += (size_t)rv;
	assert(conn->wbuf_sent <= conn->wbuf_size);
	if(conn->wbuf_sent==conn->wbuf_size){
		//response was fully sent change the state.
		conn->state= STATE_REQ;
		conn->wbuf_sent=0;
		conn->wbuf_size=0;
		return false;
	}
	// still got some data in wbuf, could try to write again
    return true;
}

static void state_res(Conn *conn){
	while(try_flush_buffer(conn)){}
}
static void connection_io(Conn *conn){
	if(conn->state==STATE_REQ){
		state_req(conn);
	}else if(conn->state == STATE_RES){
		state_res(conn);
	}else{
		assert(0);
	}
}

int main(){
	int fd = socket(AF_INET,SOCK_STREAM,0);
	if(fd<0){
		die("socket()");
	}
	int val =1;
	setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&val, sizeof(val));
	
	//bind
	struct sockaddr_in addr={};
	addr.sin_family=AF_INET,
	addr.sin_port = ntohs(1234);
	addr.sin_addr.s_addr = ntohl(0);
	
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
    while(true){
    	// prepare the arguments of the poll()
        poll_args.clear();
        // for convenience, the listening fd is put in the first position
        struct pollfd pfd= {fd,POLLIN,0};
        poll_args.push_back(pfd);
        for(Conn *conn:fd2conn){
        	if(!conn)
        		continue;
        	struct pollfd pfd={};
        	pfd.fd = conn->fd;
        	pfd.events = (conn->state==STATE_REQ)?POLLIN:POLLOUT;
        	pfd.events = pfd.events | POLLERR;
        	poll_args.push_back(pfd);
		}
		// poll for active fds
        // the timeout argument doesn't matter here
        int rv = poll(poll_args.data(),(nfds_t)poll_args.size(),1000);
        if(rv<0)
        	die("poll()");
        
         // process active connections
        for(size_t i=1; i<poll_args.size();i++){
        	if(poll_args[i].revents){
				Conn *conn = fd2conn[poll_args[i].fd];
				connection_io(conn);
				if(conn->state==STATE_END){
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
//  g++ Server.cpp -o 9Server hashtable.cpp