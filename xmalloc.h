/*
 * Author: Xu Xiao
 * Copyright (c) 2019 Xu Xiao, China
 *                    All rights reserved
 */

void	*xmalloc(size_t);
void	*xcalloc(size_t, size_t);
void	*xreallocarray(void *, size_t, size_t);
void	*xrecallocarray(void *, size_t, size_t, size_t);
char	*xstrdup(const char *);
int	 xasprintf(char **, const char *, ...)
                __attribute__((__format__ (printf, 2, 3)))
                __attribute__((__nonnull__ (2)));
