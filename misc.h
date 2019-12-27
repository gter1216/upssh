/*
 * Author: Xu Xiao
 * Copyright (c) 2019 Xu Xiao, China
 *                    All rights reserved
 */

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

typedef struct {
       int     port;		/* Port to connect. */
	char   *user;		/* User to log in as. */
	char   *host;         /* Remote host. */
}       Options;
