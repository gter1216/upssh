/*==============================================================================
 | Filename: upssh.c                                                    
 | Programmer: Xiao Xu
 | Date: Dec 24, 2019                                                                 
 +------------------------------------------------------------------------------                                                                       
 | Description: This is the client side implementation for upssh
 |
 | Copyright (c) 2019 Xu Xiao.  All rights reserved.
 ==============================================================================*/

#include <pwd.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "upssh.h"
#include "xmalloc.h"
#include "misc.h"


/*
 * General data structure for command line options and options configurable
 * in configuration files.  See readconf.h.
 */
Options options;

/* Prints a help message to the user.  This function never returns. */

static void
usage(void)
{
	fprintf(stderr,
               "usage: upssh user@host\n"
	); 
	exit(255);
}

/*readline函数实现*/
ssize_t readline(int fd, char *vptr, size_t maxlen)
{
	ssize_t	n, rc;
	char	c, *ptr;

	ptr = vptr;
	for (n = 1; n < maxlen; n++) {
		if ( (rc = read(fd, &c,1)) == 1) {
			*ptr++ = c;
			if (c == '\n')
				break;	/* newline is stored, like fgets() */
		} else if (rc == 0) {
			*ptr = 0;
			return(n - 1);	/* EOF, n - 1 bytes were read */
		} else
			return(-1);		/* error, errno set by read() */
	}

	*ptr = 0;	/* null terminate like fgets() */
	return(n);
}


/*
 * Main program for the upssh client.
 */
void main(int ac, char **av)
{
     struct passwd *pw;

     char *p, *cp;

     /* Get user data. */
     pw = getpwuid(getuid());
     if (!pw) {
		printf("No user exists for uid %lu \n", (u_long)getuid());
		exit(255);
     }

     //printf("upssh client startup... \n");

     if(ac==1)
	 	usage();

     av += 1;	 

     /* Parse Argument */
     if (ac > 0) {
		int tport;
		char *tuser;
		switch (parse_ssh_uri(*av, &tuser, &(options.host), &tport)) {
		case -1:
			usage();
			break;
		case 0:
			if (options.user == NULL) {
				options.user = tuser;
				tuser = NULL;
			}
			free(tuser);
			if (options.port == -1 && tport != -1)
				options.port = tport;
			break;
		default:
			p = xstrdup(*av);
			cp = strrchr(p, '@');
			if (cp != NULL) {
				if (cp == p)
					usage();
				if (options.user == NULL) {
					options.user = p;
					p = NULL;
				}
				*cp++ = '\0';
				options.host = xstrdup(cp);
				free(p);
			} else
				options.host = p;
			break;
		}
	}

     /* Check that we got a host name. */
     if (!options.host)
	 	usage();


     options.port = SSH_DEFAULT_PORT;
	 
     printf("host is %s \n", options.host);

     printf("port is %d \n", options.port);

     /*Connect to remote server*/

    int sockfd;
    struct sockaddr_in servaddr;

    if((sockfd = socket(AF_INET , SOCK_STREAM , 0)) == -1)
    {
        printf("socket error \n");
        exit(1);
    }

    bzero(&servaddr , sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(options.port);
    if(inet_pton(AF_INET , options.host , &servaddr.sin_addr) < 0)
    {
        printf("inet_pton error for %s\n", options.host);
        exit(1);
    }
	  
    if(connect(sockfd , (struct sockaddr *)&servaddr , sizeof(servaddr)) < 0)
    {
        // print error number
        perror("connect error");
        exit(1);
    }

    char sendline[MAX_LINE] , recvline[MAX_LINE];

    //fgets(sendline , MAX_LINE , stdin);

    // if (sendline[strlen(sendline)-1] == '\n')
    //        sendline[strlen(sendline)-1] = '\0'; 

    //printf("client want to send [%s] \n", sendline);

   snprintf(sendline, sizeof(sendline), "%s", options.user);
	
    write(sockfd , sendline , strlen(sendline));

    if(readline(sockfd , recvline , MAX_LINE) == 0)
    {
	   printf("server terminated prematurely \n");
	   exit(1);
     }

    if(fputs(recvline , stdout) == EOF)
    {
	    printf("fputs error \n");
	    exit(1);
    }

   printf("$ ");	 

    while(fgets(sendline , MAX_LINE , stdin) != NULL)	
    {
         //printf("client want to send [%s] \n", sendline);
		 
	  write(sockfd , sendline , strlen(sendline));

	  if(readline(sockfd , recvline , MAX_LINE) == 0)
	  {
		printf("server terminated prematurely \n");
		exit(1);
	   }

	  printf("$ ");

	   if(fputs(recvline , stdout) == EOF)
	   {
		 printf("fputs error \n");
		 exit(1);
	    }

	   printf("$ ");	 
	   
	   //memset(sendline, 0, MAX_LINE);
	}

     return;
}

