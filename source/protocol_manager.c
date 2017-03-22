#include "protocol_manager.h"

#include <stdbool.h>
#include "kv.h"
#include "parser.h"
#include "server.h"

// Lock to make the KVS threadsafe.
pthread_mutex_t mutex_kvs = PTHREAD_MUTEX_INITIALIZER;

/**
 * Parses the message based on the CONTROL protocol.
 *
 * COUNT
 * 		� Gets number of items in store, same as DATA protocol.
 *  	- Returns:
 *  		- "<int>", e.g. "2"
 *
 *  SHUTDOWN
 *  	- Stop accepting any new connections on the data port.
 *  	- Terminates as soon as current connections have ended.
 *  	- Returns:
 *  		- "Shutting down."
 *
 */
int parse_message_with_control_protocol(void *message) {

	// Parse a Control command
	enum CONTROL_CMD cmd = parse_c(message);

	// Error parsing the command
	if(cmd == C_ERROR) {

		// If a user types a wrong command, show error,
		// otherwise if they hit return then close their connection
		 if(strlen(message) == 0) {
			 sprintf(message, "Goodbye.\n");
			 return R_DEATH;  // They want to exit

		 } else {
			 // Just show a command not found error
		     sprintf(message, "Command not found.\n");
		 }

	// If the user has issued the SHUTDOWN command.
	} else if(cmd == C_SHUTDOWN) {

		// Send them back a shutdown message.
		sprintf(message, "Shutting down.\n");
		return R_SHUTDOWN;

	// If the user issued COUNT command
	} else if(cmd == C_COUNT) {

		// countItems() should never fail.
		pthread_mutex_lock(&mutex_kvs);
		sprintf(message, "%d\n", countItems());
		pthread_mutex_unlock(&mutex_kvs);

	} else {
		DEBUG_PRINT(("ERROR, should never happen. cmd %d", cmd));
	}

	return R_SUCCESS;
}

/**
 * Parses the message based on the DATA protocol.
 *
 *  The data protocol:
 *
 *  PUT key value
 *  	- Store value under key, should succeed whether or not the key already exists, if it does overwrite old value.
 *  	- Returns:
 *  		- "Stored key successfully.", if success
 *  		- "Error storing key.", if error
 *
 *	GET key
 *		� Gets value from key
 *		- Returns:
 *			- "<value>", if key exists
 *			- "No such key.", if doesn't
 *
 *  COUNT
 *  	� Gets number of items in store
 *  	- Returns:
 *  		- "<int>", e.g. "2".
 *
 *	DELETE key
 *		� Deletes key
 *		- Returns:
 *			- "Success.", if key exists
 *			- "Error, no key found.", if not.
 *
 *	EXISTS key
 *		� Check if key exists in store
 *		- Returns:
 *			- "1", if key exists,
 *			- "0", otherwise.
 *
 *	(empty line)
 *		- Close connection.
 *	    - Returns:
 *			- "Goodbye."
 *
 */
int parse_message_with_data_protocol(void* message) {

	// Parse the command, storing the results in key and text.
	enum DATA_CMD cmd;
	char *key[512];
	char *text = (char*) malloc(LINE * sizeof(char));

	int is_success = parse_d(message, &cmd, key, &text);
	if(is_success == 2 || is_success == 3) {
		DEBUG_PRINT(("WARNING parse_d() in protocol_manger.c: Is_success %d\n", is_success));
	}

	// If user issued COUNT command
	if(cmd == D_COUNT) {

		// countItems should never fail
		pthread_mutex_lock(&mutex_kvs);
		int itemCount = countItems();
		pthread_mutex_unlock(&mutex_kvs);

		sprintf(message, "%d\n", itemCount);

	// If user issued EXISTS command
	} else if(cmd == D_EXISTS) {

		// itemExists should never fail
		pthread_mutex_lock(&mutex_kvs);
		int does_exist = itemExists(*key);
		pthread_mutex_unlock(&mutex_kvs);

		sprintf(message, "%d\n", does_exist);

	// If user issued GET command
	} else if(cmd == D_GET) {

		// findValue could return NULL
		pthread_mutex_lock(&mutex_kvs);
		char* result = findValue(*key);
		pthread_mutex_unlock(&mutex_kvs);

		// Check if the key exists
		if(result == NULL) {
			sprintf(message, "No such key.\n");
		} else {
			sprintf(message, "%s\n", result);
		}

	// If user issued PUT command
	} else if(cmd == D_PUT) {

		// Create the text to store on the heap
		char* copytext = malloc(strlen(text) + 1);
		strncpy(copytext, text, strlen(text) + 1);

		// If item exists, or table is full then -1
		pthread_mutex_lock(&mutex_kvs);
		int is_error = createItem(*key, copytext);
		pthread_mutex_unlock(&mutex_kvs);

		// Check if result exists
		if(is_error == 0) {
			sprintf(message, "Success.\n");
		} else {
			sprintf(message, "Error storing key.\n");
		}

	// If user issued DELETE command
	} else if(cmd == D_DELETE) {

		// -1 if item doesn't exist.
		pthread_mutex_lock(&mutex_kvs);
		int is_error = deleteItem(*key, false);
		pthread_mutex_unlock(&mutex_kvs);

		// Check if item exists
		if(is_error == 0) {
			sprintf(message, "Success.\n");
		} else {
			sprintf(message, "Error, no key found.\n");
		}

	// If user just hit return
	} else if(cmd == D_END)	{
		sprintf(message, "Goodbye.\n");
		return R_DEATH; // They want to exit

	// Error messages
	} else if(cmd == D_ERR_OL) {
		sprintf(message, "Error, can't find EOL, line too long.\n");

	} else if(cmd == D_ERR_INVALID) {
		sprintf(message, "Error, command not found.\n");

	} else if(cmd == D_ERR_SHORT) {
		sprintf(message, "Error, command too short.\n");

	} else if(cmd == D_ERR_LONG) {
		sprintf(message, "Error, command too long.\n");

	} else {
		sprintf(message, "Command not found.\n");
	}

	return R_SUCCESS;
}


/**
 * Parses the message based on the socket protocol, either DATA or CONTROL,
 * stores the result to give back to the client in the message parameter,
 * returns an enum about what to do next.
 */
enum RETURN_TYPE run_command(int type, void* message) {

	if(type == DATA) {
		return parse_message_with_data_protocol(message);

	} else if (type == CONTROL) {
    	return parse_message_with_control_protocol(message);
	}

	return R_ERROR;
}
