/*
 * jsonrpc-c.c
 *
 *  Created on: Oct 11, 2012
 *      Author: hmng
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "jsonrpc-c.h"

static int __jrpc_server_start(struct jrpc_server *server);
static void jrpc_procedure_destroy(struct jrpc_procedure *procedure);
static void async_cb (EV_P_ ev_async *w, int revents);

struct ev_loop *loop;

/* An item in the connection queue. */
typedef struct conn_queue_item CQ_ITEM;
struct conn_queue_item {
	int               sfd;
	char szAddr[16];
	int port;
	CQ_ITEM          *next;
	void *data;
};

/* A connection queue. */
typedef struct conn_queue CQ;
struct conn_queue {
	CQ_ITEM *head;
	CQ_ITEM *tail;
	pthread_mutex_t lock;
};

/*
 * Initializes a connection queue.
 */
static void cq_init(CQ *cq) {
	pthread_mutex_init(&cq->lock, NULL);
	cq->head = NULL;
	cq->tail = NULL;
}

/*
 * Looks for an item on a connection queue, but doesn't block if there isn't
 * one.
 * Returns the item, or NULL if no item is available
 */
static CQ_ITEM *cq_pop(CQ *cq) {
	CQ_ITEM *item;

	pthread_mutex_lock(&cq->lock);
	item = cq->head;
	if (NULL != item) {
		cq->head = item->next;
		if (NULL == cq->head)
			cq->tail = NULL;
	}
	pthread_mutex_unlock(&cq->lock);

	return item;
}

/*
 * Adds an item to a connection queue.
 */
static void cq_push(CQ *cq, CQ_ITEM *item) {
	item->next = NULL;


	pthread_mutex_lock(&cq->lock);
	if (NULL == cq->tail)
		cq->head = item;
	else
		cq->tail->next = item;
	cq->tail = item;
	pthread_mutex_unlock(&cq->lock);
}

typedef struct {
	pthread_t thread_id;         /* unique ID of this thread */
	struct ev_loop *loop;     /* libev loop this thread uses */
	struct ev_async async_watcher;   /* async watcher for new connect */
	struct conn_queue *new_conn_queue; /* queue of new connections to handle */
} WORK_THREAD;

/*
 * Each libev instance has a async_watcher, which other threads
 * can use to signal that they've put a new connection on its queue.
 */
static WORK_THREAD *work_threads;
/*
 * Number of worker threads that have finished setting themselves up.
 */
static int init_count = 0;
static pthread_mutex_t init_lock;
static pthread_cond_t init_cond;
static int round_robin = 0;

/*
 * Worker thread: main event loop
 */
static void *worker_libev(void *arg) {
	WORK_THREAD *me = arg;


	/* Any per-thread setup can happen here; thread_init() will block until
	 * all threads have finished initializing.
	 */


	pthread_mutex_lock(&init_lock);
	init_count++;
	pthread_cond_signal(&init_cond);
	pthread_mutex_unlock(&init_lock);


	me->thread_id = pthread_self();
	ev_loop(me->loop, 0);
	return NULL;
}

/*
 * Creates a worker thread.
 */
static void create_worker(void *(*func)(void *), void *arg) {
	pthread_t       thread;
	pthread_attr_t  attr;
	int             ret;


	pthread_attr_init(&attr);


	if ((ret = pthread_create(&thread, &attr, func, arg)) != 0) {
		fprintf(stderr, "Can't create thread: %s\n",
				strerror(ret));
		exit(1);
	}
}

/*
 * Set up a thread's information.
 */
static void setup_thread(WORK_THREAD *me) {
	me->loop = ev_loop_new(0);
	if (! me->loop) {
		fprintf(stderr, "Can't allocate event base\n");
		exit(1);
	}


	me->async_watcher.data = me;
	/* Listen for notifications from other threads */
	ev_async_init(&me->async_watcher, async_cb);
	ev_async_start(me->loop, &me->async_watcher);


	me->new_conn_queue = malloc(sizeof(struct conn_queue));
	if (me->new_conn_queue == NULL) {
		perror("Failed to allocate memory for connection queue\n");
		exit(EXIT_FAILURE);
	}
	cq_init(me->new_conn_queue);
}

void thread_init()
{
	int nthreads = 5;
	pthread_mutex_init(&init_lock, NULL);
	pthread_cond_init(&init_cond, NULL);
	work_threads = calloc(nthreads, sizeof(WORK_THREAD));
	if (! work_threads) {
		perror("Can't allocate thread descriptors\n");
		exit(1);
	}

	int i = 0;
	for (i = 0; i < nthreads; i++) {
		setup_thread(&work_threads[i]);
	}

	/* Create threads after we've done all the libevent setup. */
	for (i = 0; i < nthreads; i++) {
		create_worker(worker_libev, &work_threads[i]);
	}

	/* Wait for all the threads to set themselves up before returning. */
	pthread_mutex_lock(&init_lock);
	while (init_count < nthreads) {
		pthread_cond_wait(&init_cond, &init_lock);
	}
	pthread_mutex_unlock(&init_lock);
}

void dispath_conn(int anewfd,struct sockaddr_in asin, void*data)
{
	// set the new connect item
	CQ_ITEM *lpNewItem = calloc(1,sizeof(CQ_ITEM));
	if (! lpNewItem) {
		perror("Can't allocate connection item\n");
		exit(1);
	}

	lpNewItem->sfd = anewfd;
	lpNewItem->data = data;
	strcpy(lpNewItem->szAddr,(char*)inet_ntoa(asin.sin_addr));
	lpNewItem->port = asin.sin_port;


	// libev default loop, accept the new connection, round-robin 
	// dispath to a work_thread.
	int robin = round_robin%init_count;
	cq_push(work_threads[robin].new_conn_queue,lpNewItem);
	ev_async_send(work_threads[robin].loop, &(work_threads[robin].async_watcher));
	printf("pushed fd:%d to thread:%d\n", anewfd, robin);
	round_robin++;
}

void accept_callback2(struct ev_loop *loop, ev_io *w, int revents)
{
	int newfd;
	struct sockaddr_in sin;
	socklen_t addrlen = sizeof(struct sockaddr);
	while ((newfd = accept(w->fd, (struct sockaddr *)&sin, &addrlen)) < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK) 
		{
			//these are transient, so don't log anything.
			continue; 
		}
		else
		{
			printf("accept error.[%s]\n", strerror(errno));
			break;
		}
	}
	dispath_conn(newfd,sin,w->data);
}

// get sockaddr, IPv4 or IPv6:
static void *get_in_addr(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*) sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*) sa)->sin6_addr);
}

static int send_response(struct jrpc_connection * conn, char *response) {
	int fd = conn->fd;
	if (conn->debug_level > 1)
		printf("JSON Response:\n%s\n", response);
	write(fd, response, strlen(response));
	write(fd, "\n", 1);
	return 0;
}

static int send_error(struct jrpc_connection * conn, int code, char* message,
		cJSON * id) {
	int return_value = 0;
	cJSON *result_root = cJSON_CreateObject();
	cJSON *error_root = cJSON_CreateObject();
	cJSON_AddNumberToObject(error_root, "code", code);
	cJSON_AddStringToObject(error_root, "message", message);
	cJSON_AddItemToObject(result_root, "error", error_root);
	cJSON_AddItemToObject(result_root, "id", id);
	char * str_result = cJSON_Print(result_root);
	return_value = send_response(conn, str_result);
	free(str_result);
	cJSON_Delete(result_root);
	free(message);
	return return_value;
}

static int send_result(struct jrpc_connection * conn, cJSON * result,
		cJSON * id) {
	int return_value = 0;
	cJSON *result_root = cJSON_CreateObject();
	if (result)
		cJSON_AddItemToObject(result_root, "result", result);
	cJSON_AddItemToObject(result_root, "id", id);

	char * str_result = cJSON_Print(result_root);
	return_value = send_response(conn, str_result);
	free(str_result);
	cJSON_Delete(result_root);
	return return_value;
}

static int invoke_procedure(struct jrpc_server *server,
		struct jrpc_connection * conn, char *name, cJSON *params, cJSON *id) {
	cJSON *returned = NULL;
	int procedure_found = 0;
	jrpc_context ctx;
	ctx.error_code = 0;
	ctx.error_message = NULL;
	int i = server->procedure_count;
	while (i--) {
		if (!strcmp(server->procedures[i].name, name)) {
			procedure_found = 1;
			ctx.data = server->procedures[i].data;
			returned = server->procedures[i].function(&ctx, params, id);
			break;
		}
	}
	if (!procedure_found)
		return send_error(conn, JRPC_METHOD_NOT_FOUND,
				strdup("Method not found."), id);
	else {
		if (ctx.error_code)
			return send_error(conn, ctx.error_code, ctx.error_message, id);
		else
			return send_result(conn, returned, id);
	}
}

static int eval_request(struct jrpc_server *server,
		struct jrpc_connection * conn, cJSON *root) {
	cJSON *method, *params, *id;
	method = cJSON_GetObjectItem(root, "method");
	if (method != NULL && method->type == cJSON_String) {
		params = cJSON_GetObjectItem(root, "params");
		if (params == NULL|| params->type == cJSON_Array
				|| params->type == cJSON_Object) {
			id = cJSON_GetObjectItem(root, "id");
			if (id == NULL|| id->type == cJSON_String
					|| id->type == cJSON_Number) {
				//We have to copy ID because using it on the reply and deleting the response Object will also delete ID
				cJSON * id_copy = NULL;
				if (id != NULL)
					id_copy =
						(id->type == cJSON_String) ? cJSON_CreateString(
								id->valuestring) :
						cJSON_CreateNumber(id->valueint);
				if (server->debug_level)
					printf("Method Invoked: %s\n", method->valuestring);
				return invoke_procedure(server, conn, method->valuestring,
						params, id_copy);
			}
		}
	}
	send_error(conn, JRPC_INVALID_REQUEST,
			strdup("The JSON sent is not a valid Request object."), NULL);
	return -1;
}

static void close_connection(struct ev_loop *loop, ev_io *w) {
	ev_io_stop(loop, w);
	close(((struct jrpc_connection *) w)->fd);
	printf("closed fd:%d\n", ((struct jrpc_connection *) w)->fd);
	free(((struct jrpc_connection *) w)->buffer);
	free(((struct jrpc_connection *) w));
}

static void connection_cb(struct ev_loop *loop, ev_io *w, int revents) {
	struct jrpc_connection *conn;
	struct jrpc_server *server = (struct jrpc_server *) w->data;
	size_t bytes_read = 0;
	//get our 'subclassed' event watcher
	conn = (struct jrpc_connection *) w;
	int fd = conn->fd;
	if (conn->pos == (conn->buffer_size - 1)) {
		char * new_buffer = realloc(conn->buffer, conn->buffer_size *= 2);
		if (new_buffer == NULL) {
			perror("Memory error");
			return close_connection(loop, w);
		}
		conn->buffer = new_buffer;
		memset(conn->buffer + conn->pos, 0, conn->buffer_size - conn->pos);
	}
	// can not fill the entire buffer, string must be NULL terminated
	int max_read_size = conn->buffer_size - conn->pos - 1;
	if ((bytes_read = read(fd, conn->buffer + conn->pos, max_read_size))
			== -1) {
		perror("read");
		return close_connection(loop, w);
	}
	if (!bytes_read) {
		// client closed the sending half of the connection
		if (server->debug_level)
			printf("Client closed connection.\n");
		return close_connection(loop, w);
	} else {
		cJSON *root;
		char *end_ptr = NULL;
		conn->pos += bytes_read;

		if ((root = cJSON_Parse_Stream(conn->buffer, &end_ptr)) != NULL) {
			if (server->debug_level > 1) {
				char * str_result = cJSON_Print(root);
				printf("Valid JSON Received:\n%s\n", str_result);
				free(str_result);
			}

			if (root->type == cJSON_Object) {
				eval_request(server, conn, root);
			}
			//shift processed request, discarding it
			memmove(conn->buffer, end_ptr, strlen(end_ptr) + 2);

			conn->pos = strlen(end_ptr);
			memset(conn->buffer + conn->pos, 0,
					conn->buffer_size - conn->pos - 1);

			cJSON_Delete(root);
		} else {
			// did we parse the all buffer? If so, just wait for more.
			// else there was an error before the buffer's end
			if (end_ptr != (conn->buffer + conn->pos)) {
				if (server->debug_level) {
					printf("INVALID JSON Received:\n---\n%s\n---\n",
							conn->buffer);
				}
				send_error(conn, JRPC_PARSE_ERROR,
						strdup(
							"Parse error. Invalid JSON was received by the server."),
						NULL);
				return close_connection(loop, w);
			}
		}
	}

}

// Make the code work with both the old (ev_loop/ev_unloop)
// and new (ev_run/ev_break) versions of libev.
#ifdef EVUNLOOP_ALL
#define EV_RUN ev_loop
#define EV_BREAK ev_unloop
#define EVBREAK_ALL EVUNLOOP_ALL
#else
#define EV_RUN ev_run
#define EV_BREAK ev_break
#endif

static	void
async_cb (EV_P_ ev_async *w, int revents)
{
	CQ_ITEM *item;

	while(
	item = cq_pop(((WORK_THREAD*)(w->data))->new_conn_queue))

	{
		char s[INET6_ADDRSTRLEN];
		struct jrpc_connection *connection_watcher;
		connection_watcher = malloc(sizeof(struct jrpc_connection));
		connection_watcher->fd = item->sfd;
		ev_io_init(&connection_watcher->io, connection_cb,
				connection_watcher->fd, EV_READ);
		//copy pointer to struct jrpc_server
		connection_watcher->io.data = item->data;
		connection_watcher->buffer_size = 1500;
		connection_watcher->buffer = malloc(1500);
		memset(connection_watcher->buffer, 0, 1500);
		connection_watcher->pos = 0;
		//copy debug_level, struct jrpc_connection has no pointer to struct jrpc_server
		connection_watcher->debug_level =
			((struct jrpc_server *) item->data)->debug_level;

		ev_io_start(((WORK_THREAD*)(w->data))->loop,&connection_watcher->io);

		printf("thread[%lu] accept: fd :%d  addr:%s port:%d\n",((WORK_THREAD*)(w->data))->thread_id,item->sfd,item->szAddr,item->port);

		free(item);
		item = NULL;
	}
}


int jrpc_server_init(struct jrpc_server *server, int port_number) {
	loop = EV_DEFAULT;
	return jrpc_server_init_with_ev_loop(server, port_number, loop);
}

int jrpc_server_init_with_ev_loop(struct jrpc_server *server, 
		int port_number, struct ev_loop *loop) {
	memset(server, 0, sizeof(struct jrpc_server));
	server->loop = loop;
	server->port_number = port_number;
	char * debug_level_env = getenv("JRPC_DEBUG");
	if (debug_level_env == NULL)
		server->debug_level = 0;
	else {
		server->debug_level = strtol(debug_level_env, NULL, 10);
		printf("JSONRPC-C Debug level %d\n", server->debug_level);
	}
		//server->debug_level = 5;
	return __jrpc_server_start(server);
}

static int __jrpc_server_start(struct jrpc_server *server) {
	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_in sockaddr;
	unsigned int len;
	int yes = 1;
	int rv;
	char PORT[6];
	sprintf(PORT, "%d", server->port_number);
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol))
				== -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))
				== -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		len = sizeof(sockaddr);
		if (getsockname(sockfd, (struct sockaddr *) &sockaddr, &len) == -1) {
			close(sockfd);
			perror("server: getsockname");
			continue;
		}
		server->port_number = ntohs( sockaddr.sin_port );

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "server: failed to bind\n");
		return 2;
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (listen(sockfd, 5) == -1) {
		perror("listen");
		exit(1);
	}
	if (server->debug_level)
		printf("server: waiting for connections...\n");

#ifdef _MULTITHREAD
	thread_init();
#endif

	ev_io_init(&server->listen_watcher, accept_callback2, sockfd, EV_READ);
	server->listen_watcher.data = server;
	ev_io_start(server->loop, &server->listen_watcher);

	return 0;
}

void jrpc_server_run(struct jrpc_server *server){
	EV_RUN(server->loop, 0);
}

int jrpc_server_stop(struct jrpc_server *server) {
	EV_BREAK(server->loop, EVBREAK_ALL);
	return 0;
}

void jrpc_server_destroy(struct jrpc_server *server){
	/* Don't destroy server */
	int i;
	for (i = 0; i < server->procedure_count; i++){
		jrpc_procedure_destroy( &(server->procedures[i]) );
	}
	free(server->procedures);
}

static void jrpc_procedure_destroy(struct jrpc_procedure *procedure){
	if (procedure->name){
		free(procedure->name);
		procedure->name = NULL;
	}
	if (procedure->data){
		free(procedure->data);
		procedure->data = NULL;
	}
}

int jrpc_register_procedure(struct jrpc_server *server,
		jrpc_function function_pointer, char *name, void * data) {
	int i = server->procedure_count++;
	if (!server->procedures)
		server->procedures = malloc(sizeof(struct jrpc_procedure));
	else {
		struct jrpc_procedure * ptr = realloc(server->procedures,
				sizeof(struct jrpc_procedure) * server->procedure_count);
		if (!ptr)
			return -1;
		server->procedures = ptr;

	}
	if ((server->procedures[i].name = strdup(name)) == NULL)
		return -1;
	server->procedures[i].function = function_pointer;
	server->procedures[i].data = data;
	return 0;
}

int jrpc_deregister_procedure(struct jrpc_server *server, char *name) {
	/* Search the procedure to deregister */
	int i;
	int found = 0;
	if (server->procedures){
		for (i = 0; i < server->procedure_count; i++){
			if (found)
				server->procedures[i-1] = server->procedures[i];
			else if(!strcmp(name, server->procedures[i].name)){
				found = 1;
				jrpc_procedure_destroy( &(server->procedures[i]) );
			}
		}
		if (found){
			server->procedure_count--;
			if (server->procedure_count){
				struct jrpc_procedure * ptr = realloc(server->procedures,
						sizeof(struct jrpc_procedure) * server->procedure_count);
				if (!ptr){
					perror("realloc");
					return -1;
				}
				server->procedures = ptr;
			}else{
				server->procedures = NULL;
			}
		}
	} else {
		fprintf(stderr, "server : procedure '%s' not found\n", name);
		return -1;
	}
	return 0;
}
