/*
 * Author: Xu Xiao
 * Copyright (c) 2019 Xu Xiao, China
 *                    All rights reserved
 */

#include <stdarg.h> /* va_list */

void     fatal(const char *, ...) __attribute__((noreturn))
    __attribute__((format(printf, 1, 2)));

void	 cleanup_exit(int) __attribute__((noreturn));
