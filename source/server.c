/* Server program for key-value store. */

#include "server.h"

#include <sys/socket.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <unistd.h>
#include "kv.h"
#include "parser.h"
#include "debug.h"
#include "protocol_manager.h"
#include "socket-helper.h"
#include "server_helpers.h"
#include "message_manager.h"
#include "queue.h"

pthread_mutex_t mutex_kill = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_kill = PTHREAD_COND_INITIALIZER;
int server_port_that_wants_to_die = 0;

bool is_shutdown = false;

// The queue for producer and consumers
Queue *worker_queue;

pthread_t worker_threads[NTHREADS];
struct worker_configuration *worker_thread_pool;

/**
 * Like the main function.
 *
 */
int initiate_server(int cport, int dport) {
	pthread_t data_thread = pthread_self(), control_thread = pthread_self();

	struct server_config *data_info = malloc(sizeof(struct server_config));
	struct server_config *control_info = malloc(sizeof(struct server_config));
	data_info->port = dport;
	data_info->type = DATA;
	control_info->port = cport;
	control_info->type = CONTROL;

    // Setup a queue for workers to consume
    worker_queue = make_queue(MAX_QUEUE_SIZE);

    // Start all the workers for data threads
	init_worker_pool();

	int number_of_servers_alive = 2;
	start_server(data_info, data_thread);
	start_server(control_info, control_thread); // TODO enable me

	printf("Server started.\n");

	pthread_mutex_lock(&mutex_kill);
	while(server_port_that_wants_to_die == 0) {
		pthread_cond_wait(&cond_kill, &mutex_kill); // wait on a condition variable

		if(server_port_that_wants_to_die == cport) {
			DEBUG_PRINT(("OK: Killing control server port:%d.\n", cport));
			pthread_join(control_thread, NULL);
			number_of_servers_alive--;

			DEBUG_PRINT(("OK: Killing data server port:%d.\n", dport));
			pthread_join(data_thread, NULL);
			number_of_servers_alive--;

			server_port_that_wants_to_die = 0;
		} else {
			DEBUG_PRINT(("BAD: Oh dear, trying to kill server that we don't have %d, ignore it.\n", server_port_that_wants_to_die));
			exit(1); // TODO remove me
		}
		pthread_mutex_unlock(&mutex_kill);

		if(number_of_servers_alive == 0) {
			printf("Shutting down.\n");
			DEBUG_PRINT(("OK: All servers are dead, stopping main thread num:%d.\n", number_of_servers_alive));
			break;
		}
	}

	return 0;
}

void* worker(void* args) {
	/* Extract the thread arguments */
	int worker_number = *(int*)args;

	pthread_detach(pthread_self());

	printf("Worker %d created, waiting for new tasks...\n", worker_number);
	bool running = true;
	while(running) {

		queue_item current_queue_connection = queue_pop(worker_queue);

		char initial_message[512];
		get_initial_message(current_queue_connection.type, worker_number, initial_message);

		error_handler(
			send_message(current_queue_connection.sock, &initial_message),
			"Send message failure.\n"
		);

		char client_message[MAX_MESSAGE_SIZE];

		while(true)
		{
			memset(client_message, 0, 256);

			int read_size = read_message(current_queue_connection.sock, &client_message);

			if(read_size == 0) {
				DEBUG_PRINT(("Client disconnected, read_size: %d.", read_size));
				break;
			}

			int is_success = run_command(current_queue_connection.type, &client_message);

			// Send message back to the client
			error_handler(
				send_message(current_queue_connection.sock, &client_message),
				"Send message failure.\n"
			);

			if(is_success == R_DEATH) { // They want to die
				close(current_queue_connection.sock);
				break;

			} else if(is_success == R_SHUTDOWN) {
				printf("Shutting down.\n");
				close(current_queue_connection.sock);
				pthread_mutex_lock(&mutex_kill);
				server_port_that_wants_to_die = current_queue_connection.port;
				pthread_cond_signal(&cond_kill);
				pthread_mutex_unlock(&mutex_kill);
				running = false;
				break;
			}
		}
	}

    return 0;
}

void *server_listen(void* args) {

	/* Extract the server config arguments */
	struct server_config *settings = (struct server_config*) args;

	/* Create & Bind a socket, then Listen on the port. */
	int server_socket = setup_socket(settings->port);

	struct sockaddr_in peer_addr;
    socklen_t peer_address;

	int running = true;
	while(running) {
		peer_address = sizeof(struct sockaddr_in);

		// Wait for connection
		int connection = accept(server_socket, (struct sockaddr *) &peer_addr, &peer_address);

		if (connection == -1) {
			perro("Error accepting connection");
			DEBUG_PRINT(("Could not accept a connection, just containing, backlog:%d.\n", LISTEN_BACKLOG));
			continue;
		}

		// Create queue item & add it to the consumer queue
		queue_item i = {.sock = connection, .port = settings->port, .type = settings->type };
		queue_push(worker_queue, i);
	}
	return 0;
}


void init_worker_pool() {
    // Setup a number of workers
	worker_thread_pool = malloc(sizeof(struct worker_configuration) * NTHREADS);

	for(int w = 0; w < NTHREADS; w++) {
    	printf("Creating new thread %d\n", w);

		if (pthread_create(&worker_threads[w], NULL, worker, &w) != 0) {
			perro("Could not create a worker thread");
			break;
		}

		struct worker_configuration newThread;
		newThread.worker_number = w;
		newThread.thread = &worker_threads[w];
    	worker_thread_pool[w] = newThread;
    }
}

