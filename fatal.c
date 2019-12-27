/*
 * Author: Xu Xiao
 * Copyright (c) 2019 Xu Xiao, China
 *                    All rights reserved
 */

#include <sys/types.h>

#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "log.h"


/* default implementation */
void
cleanup_exit(int i)
{
	_exit(i);
}

/* Fatal messages.  This function never returns. */
void fatal(const char *fmt,...)
{
	va_list args;

	va_start(args, fmt);
	printf(fmt, args);
	va_end(args);
	cleanup_exit(255);
}
