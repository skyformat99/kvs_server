

#ifndef _sync_handler_h_
#define _sync_handler_h_

#include "server.h"

enum RETURN_TYPE { R_SUCCESS, R_ERROR, R_SHUTDOWN, R_DEATH };
enum RETURN_TYPE run_command(int type, void* message);

#endif
