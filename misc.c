/*
 * Author: Xu Xiao
 * Copyright (c) 2019 Xu Xiao, China
 *                    All rights reserved
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <netdb.h>
 
#include "xmalloc.h"
#include "misc.h"
#include "upssh.h"

#define INVALID 	1
#define TOOSMALL 	2
#define TOOLARGE 	3

/*
 * Search for next delimiter between hostnames/addresses and ports.
 * Argument may be modified (for termination).
 * Returns *cp if parsing succeeds.
 * *cp is set to the start of the next field, if one was found.
 * The delimiter char, if present, is stored in delim.
 * If this is the last field, *cp is set to NULL.
 */
char * hpdelim2(char **cp, char *delim)
{
	char *s, *old;

	if (cp == NULL || *cp == NULL)
		return NULL;

	old = s = *cp;
	if (*s == '[') {
		if ((s = strchr(s, ']')) == NULL)
			return NULL;
		else
			s++;
	} else if ((s = strpbrk(s, ":/")) == NULL)
		s = *cp + strlen(*cp); /* skip to end (see first case below) */

	switch (*s) {
	case '\0':
		*cp = NULL;	/* no more fields*/
		break;

	case ':':
	case '/':
		if (delim != NULL)
			*delim = *s;
		*s = '\0';	/* terminate */
		*cp = s + 1;
		break;

	default:
		return NULL;
	}

	return old;
}

char * cleanhostname(char *host)
{
	if (*host == '[' && host[strlen(host) - 1] == ']') {
		host[strlen(host) - 1] = '\0';
		return (host + 1);
	} else
		return host;
}

size_t strlcpy(char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;

	/* Copy as many bytes as will fit */
	if (n != 0) {
		while (--n != 0) {
			if ((*d++ = *s++) == '\0')
				break;
		}
	}

	/* Not enough room in dst, add NUL and traverse rest of src */
	if (n == 0) {
		if (siz != 0)
			*d = '\0';		/* NUL-terminate dst */
		while (*s++)
			;
	}

	return(s - src - 1);	/* count does not include NUL */
}

/*
 * Check and optionally lowercase a domain name, also removes trailing '.'
 * Returns 1 on success and 0 on failure, storing an error message in errstr.
 */
int valid_domain(char *name, int makelower, const char **errstr)
{
	size_t i, l = strlen(name);
	u_char c, last = '\0';
	static char errbuf[256];

	if (l == 0) {
		strlcpy(errbuf, "empty domain name", sizeof(errbuf));
		goto bad;
	}
	if (!isalpha((u_char)name[0]) && !isdigit((u_char)name[0])) {
		snprintf(errbuf, sizeof(errbuf), "domain name \"%.100s\" "
		    "starts with invalid character", name);
		goto bad;
	}
	for (i = 0; i < l; i++) {
		c = tolower((u_char)name[i]);
		if (makelower)
			name[i] = (char)c;
		if (last == '.' && c == '.') {
			snprintf(errbuf, sizeof(errbuf), "domain name "
			    "\"%.100s\" contains consecutive separators", name);
			goto bad;
		}
		if (c != '.' && c != '-' && !isalnum(c) &&
		    c != '_') /* technically invalid, but common */ {
			snprintf(errbuf, sizeof(errbuf), "domain name "
			    "\"%.100s\" contains invalid characters", name);
			goto bad;
		}
		last = c;
	}
	if (name[l - 1] == '.')
		name[l - 1] = '\0';
	if (errstr != NULL)
		*errstr = NULL;
	return 1;
bad:
	if (errstr != NULL)
		*errstr = errbuf;
	return 0;
}

long long strtonum(const char *numstr, long long minval, long long maxval,
    const char **errstrp)
{
	long long ll = 0;
	char *ep;
	int error = 0;
	struct errval {
		const char *errstr;
		int err;
	} ev[4] = {
		{ NULL,		0 },
		{ "invalid",	EINVAL },
		{ "too small",	ERANGE },
		{ "too large",	ERANGE },
	};

	ev[0].err = errno;
	errno = 0;
	if (minval > maxval)
		error = INVALID;
	else {
		ll = strtoll(numstr, &ep, 10);
		if (numstr == ep || *ep != '\0')
			error = INVALID;
		else if ((ll == LLONG_MIN && errno == ERANGE) || ll < minval)
			error = TOOSMALL;
		else if ((ll == LLONG_MAX && errno == ERANGE) || ll > maxval)
			error = TOOLARGE;
	}
	if (errstrp != NULL)
		*errstrp = ev[error].errstr;
	errno = ev[error].err;
	if (error)
		ll = 0;

	return (ll);
}

/*
 * Convert ASCII string to TCP/IP port number.
 * Port must be >=0 and <=65535.
 * Return -1 if invalid.
 */
int a2port(const char *s)
{
	struct servent *se;
	long long port;
	const char *errstr;

	port = strtonum(s, 0, 65535, &errstr);
	if (errstr == NULL)
		return (int)port;
	if ((se = getservbyname(s, "tcp")) != NULL)
		return ntohs(se->s_port);
	return -1;
}

/*
 * Converts a two-byte hex string to decimal.
 * Returns the decimal value or -1 for invalid input.
 */
static int hexchar(const char *s)
{
	unsigned char result[2];
	int i;

	for (i = 0; i < 2; i++) {
		if (s[i] >= '0' && s[i] <= '9')
			result[i] = (unsigned char)(s[i] - '0');
		else if (s[i] >= 'a' && s[i] <= 'f')
			result[i] = (unsigned char)(s[i] - 'a') + 10;
		else if (s[i] >= 'A' && s[i] <= 'F')
			result[i] = (unsigned char)(s[i] - 'A') + 10;
		else
			return -1;
	}
	return (result[0] << 4) | result[1];
}

/*
 * Decode an url-encoded string.
 * Returns a newly allocated string on success or NULL on failure.
 */
static char *urldecode(const char *src)
{
	char *ret, *dst;
	int ch;

	ret = xmalloc(strlen(src) + 1);
	for (dst = ret; *src != '\0'; src++) {
		switch (*src) {
		case '+':
			*dst++ = ' ';
			break;
		case '%':
			if (!isxdigit((unsigned char)src[1]) ||
			    !isxdigit((unsigned char)src[2]) ||
			    (ch = hexchar(src + 1)) == -1) {
				free(ret);
				return NULL;
			}
			*dst++ = ch;
			src += 2;
			break;
		default:
			*dst++ = *src;
			break;
		}
	}
	*dst = '\0';

	return ret;
}

/*
 * Parse an (scp|ssh|sftp)://[user@]host[:port][/path] URI.
 * See https://tools.ietf.org/html/draft-ietf-secsh-scp-sftp-ssh-uri-04
 * Either user or path may be url-encoded (but not host or port).
 * Caller must free returned user, host and path.
 * Any of the pointer return arguments may be NULL (useful for syntax checking)
 * but the scheme must always be specified.
 * If user was not specified then *userp will be set to NULL.
 * If port was not specified then *portp will be -1.
 * If path was not specified then *pathp will be set to NULL.
 * Returns 0 on success, 1 if non-uri/wrong scheme, -1 on error/invalid uri.
 */
int parse_uri(const char *scheme, const char *uri, char **userp, char **hostp,
    int *portp, char **pathp)
{
	char *uridup, *cp, *tmp, ch;
	char *user = NULL, *host = NULL, *path = NULL;
	int port = -1, ret = -1;
	size_t len;

       //printf("scheme is [%s] \n", scheme);

	//printf("uri is [%s] \n", uri);

	len = strlen(scheme);
	if (strncmp(uri, scheme, len) != 0 || strncmp(uri + len, "://", 3) != 0)
		return 1;

      printf("xx_debug111 \n");
	
	uri += len + 5;

	if (userp != NULL)
		*userp = NULL;
	if (hostp != NULL)
		*hostp = NULL;
	if (portp != NULL)
		*portp = -1;
	if (pathp != NULL)
		*pathp = NULL;

	uridup = tmp = xstrdup(uri);

	/* Extract optional ssh-info (username + connection params) */
	if ((cp = strchr(tmp, '@')) != NULL) {
		char *delim;

		*cp = '\0';
		/* Extract username and connection params */
		if ((delim = strchr(tmp, ';')) != NULL) {
			/* Just ignore connection params for now */
			*delim = '\0';
		}
		if (*tmp == '\0') {
			/* Empty username */
			goto out;
		}
		if ((user = urldecode(tmp)) == NULL)
			goto out;
		tmp = cp + 1;
	}

	/* Extract mandatory hostname */
	if ((cp = hpdelim2(&tmp, &ch)) == NULL || *cp == '\0')
		goto out;
	host = xstrdup(cleanhostname(cp));
	if (!valid_domain(host, 0, NULL))
		goto out;

	if (tmp != NULL && *tmp != '\0') {
		if (ch == ':') {
			/* Convert and verify port. */
			if ((cp = strchr(tmp, '/')) != NULL)
				*cp = '\0';
			if ((port = a2port(tmp)) <= 0)
				goto out;
			tmp = cp ? cp + 1 : NULL;
		}
		if (tmp != NULL && *tmp != '\0') {
			/* Extract optional path */
			if ((path = urldecode(tmp)) == NULL)
				goto out;
		}
	}

	/* Success */
	if (userp != NULL) {
		*userp = user;
		user = NULL;
	}
	if (hostp != NULL) {
		*hostp = host;
		host = NULL;
	}
	if (portp != NULL)
		*portp = port;
	if (pathp != NULL) {
		*pathp = path;
		path = NULL;
	}
	ret = 0;
 out:
	free(uridup);
	free(user);
	free(host);
	free(path);
	return ret;
}


int parse_ssh_uri(const char *uri, char **userp, char **hostp, int *portp)
{
	char *path;
	int r;

	//printf("uri is %s\n", uri);

	r = parse_uri("upssh", uri, userp, hostp, portp, &path);
	if (r == 0 && path != NULL)
		r = -1;		/* path not allowed */
	return r;
}

