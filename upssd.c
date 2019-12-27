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
#include <sys/types.h>
#include <stdarg.h>
#include <security/pam_appl.h>
#include <security/pam_misc.h>
#include <stdbool.h>
#include <err.h>

#include "xmalloc.h"
#include "misc.h"
#include "upssh.h"

#define is_pam_failure(_rc)	((_rc) != PAM_SUCCESS)
#define _PATH_BSHELL "/bin/sh"
#define errx(E, FMT...) errmsg(1, E, 0, FMT)
#define DEFAULT_SHELL "/bin/sh"

/* Don't print PAM info messages (Last login, etc.). */
static int suppress_pam_info;

static bool _pam_session_opened;
static bool _pam_cred_established;
static pam_handle_t *pamh = NULL;

static int su_pam_conv(int num_msg, const struct pam_message **msg,
                       struct pam_response **resp, void *appdata_ptr)
{
	if (suppress_pam_info
	    && num_msg == 1
	    && msg
	    && msg[0]->msg_style == PAM_TEXT_INFO)
		return PAM_SUCCESS;

	return misc_conv(num_msg, msg, resp, appdata_ptr);
}

static struct pam_conv conv =
{
	su_pam_conv,
	NULL
};

static void usage(void)
{
	fprintf(stderr,
               "usage: upsshd 0 or 1\n0 denotes no pty, 1 denotes use pty!\n"
	); 
	exit(255);
}

int setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
	int ret = 0;

	if (rgid != sgid) {
		return -1;
	}
	
	if (setregid(rgid, egid) < 0) {
		printf("setregid %lu: %.100s\n", (u_long)rgid, strerror(errno));
		ret = -1;
	}
	
	if (setegid(egid) < 0) {
		printf("setegid %lu: %.100s\n", (u_long)egid, strerror(errno));
		ret = -1;
	}
	
	if (setgid(rgid) < 0) {
		printf("setgid %lu: %.100s\n", (u_long)rgid, strerror(errno));
		ret = -1;
	}
	
	return ret;
}


int setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
	int ret = 0;

	if (ruid != suid) {
		return -1;
	}

	if (setreuid(ruid, euid) < 0) {
		error("setreuid %lu: %.100s", (u_long)ruid, strerror(errno));
		ret = -1;
	}

	if (seteuid(euid) < 0) {
		error("seteuid %lu: %.100s", (u_long)euid, strerror(errno));
		ret = -1;
	}

	if (setuid(ruid) < 0) {
		error("setuid %lu: %.100s", (u_long)ruid, strerror(errno));
		ret = -1;
	}

	return ret;
}

/*
 * Sets the value of the given variable in the environment.  If the variable
 * already exists, its value is overridden.
 */
void child_set_env(char ***envp, u_int *envsizep, const char *name,
	const char *value)
{
	char **env;
	u_int envsize;
	u_int i, namelen;

	if (strchr(name, '=') != NULL) {
		error("Invalid environment variable \"%.100s\"", name);
		return;
	}

	/*
	 * If we're passed an uninitialized list, allocate a single null
	 * entry before continuing.
	 */
	if (*envp == NULL && *envsizep == 0) {
		*envp = xmalloc(sizeof(char *));
		*envp[0] = NULL;
		*envsizep = 1;
	}

	/*
	 * Find the slot where the value should be stored.  If the variable
	 * already exists, we reuse the slot; otherwise we append a new slot
	 * at the end of the array, expanding if necessary.
	 */
	env = *envp;
	namelen = strlen(name);
	for (i = 0; env[i]; i++)
		if (strncmp(env[i], name, namelen) == 0 && env[i][namelen] == '=')
			break;
	if (env[i]) {
		/* Reuse the slot. */
		free(env[i]);
	} else {
		/* New variable.  Expand if necessary. */
		envsize = *envsizep;
		if (i >= envsize - 1) {
			if (envsize >= 1000)
				fatal("child_set_env: too many env vars");
			envsize += 50;
			env = (*envp) = xreallocarray(env, envsize, sizeof(char *));
			*envsizep = envsize;
		}
		/* Need to set the NULL pointer at end of array beyond the new slot. */
		env[i + 1] = NULL;
	}

	/* Allocate space and format the variable in the appropriate slot. */
	/* XXX xasprintf */
	env[i] = xmalloc(strlen(name) + 1 + strlen(value) + 1);
	snprintf(env[i], strlen(name) + 1 + strlen(value) + 1, "%s=%s", name, value);
}


static char ** do_setup_env(struct passwd *pw, const char *shell)
{
	char buf[256];
	size_t n;
	u_int i, envsize;
	char *ocp, *cp, *value, **env, *laddr;

	/* Initialize the environment. */
	envsize = 100;
	env = xcalloc(envsize, sizeof(char *));
	env[0] = NULL;

	/* Set basic environment. */
	child_set_env(&env, &envsize, "USER", pw->pw_name);
	child_set_env(&env, &envsize, "LOGNAME", pw->pw_name);

	child_set_env(&env, &envsize, "HOME", pw->pw_dir);


	/* Normal systems set SHELL by default. */
	child_set_env(&env, &envsize, "SHELL", shell);

	return env;
}

do_child(struct passwd *pw)
{
     //printf("we are in child process, prepare to exec shell \n");

     //execute bash	 
     const char *shell, *shell0;
     char *argv[10];
     char ** env;
     u_int envsize;

     shell = (pw->pw_shell[0] == '\0') ? _PATH_BSHELL : pw->pw_shell;

     /*
       * Make sure $SHELL points to the shell from the password file,
	* even if shell is overridden from login.conf
     */
     //env = do_setup_env(pw, shell);

     /* Get the last component of the shell name. */
     if ((shell0 = strrchr(shell, '/')) != NULL)
	     shell0++;
     else
	     shell0 = shell;

     char argv0[256];

      /* Start the shell.  Set initial character to '-'. */
      argv0[0] = '-';

	if (strlcpy(argv0 + 1, shell0, sizeof(argv0) - 1)
		    >= sizeof(argv0) - 1) 
	{
	       errno = EINVAL;
		perror(shell);
		_exit(1);
	}

       /* Execute the shell. */	
	argv[0] = argv0;
	argv[1] = NULL;
	execve(shell, argv, env);

	/* Executing the shell failed. */
	perror(shell);
	_exit(1);
	  
}

static struct passwd *
current_getpwuid(void)
{
  uid_t ruid;

  /* GNU Hurd implementation has an extension where a process can exist in a
   * non-conforming environment, and thus be outside the realms of POSIX
   * process identifiers; on this platform, getuid() fails with a status of
   * (uid_t)(-1) and sets errno if a program is run from a non-conforming
   * environment.
   *
   * http://austingroupbugs.net/view.php?id=511
   */
  errno = 0;
  ruid = getuid ();

  return errno == 0 ? getpwuid (ruid) : NULL;
}

static void authenticate (const struct passwd *pw)
{
  const struct passwd *lpw = NULL;
  const char *cp, *srvname = NULL;
  int retval;

  srvname = "su";

  retval = pam_start (srvname, pw->pw_name, &conv, &pamh);
  if (is_pam_failure(retval))
    goto done;


  lpw = current_getpwuid ();
  if (lpw && lpw->pw_name)
    {
      retval = pam_set_item (pamh, PAM_RUSER, (const void *) lpw->pw_name);
      if (is_pam_failure(retval))
	goto done;
    }

  retval = pam_authenticate (pamh, 0);
  if (is_pam_failure(retval))
    goto done;

  retval = pam_acct_mgmt (pamh, 0);
  if (retval == PAM_NEW_AUTHTOK_REQD)
    {
      /* Password has expired.  Offer option to change it.  */
      retval = pam_chauthtok (pamh, PAM_CHANGE_EXPIRED_AUTHTOK);
    }

done:

  if (is_pam_failure(retval))
    {
      const char *msg;

      msg  = pam_strerror(pamh, retval);
      pam_end(pamh, retval);
      
      //errx (EXIT_FAILURE, "%s", msg?msg:_("incorrect password"));
      printf("failure, msg is %s, or incorrect password\n", msg);
      exit(EXIT_FAILURE);
    }
}

static void cleanup_pam (int retcode)
{
  int saved_errno = errno;

  if (_pam_session_opened)
    pam_close_session (pamh, 0);

  if (_pam_cred_established)
    pam_setcred (pamh, PAM_DELETE_CRED | PAM_SILENT);

  pam_end(pamh, retcode);

  errno = saved_errno;
}

void connection_process_no_pty(connfd)
{
     ssize_t n;
     char buf[MAX_LINE];
     char pipe_buf[MAX_LINE];
     char ack[MAX_LINE];
     char user[64];
     struct passwd *pw = NULL;
     struct passwd pw_copy;
     pid_t pid;

     //1. PAM auth
     n = read(connfd , buf , MAX_LINE);
     if(n > 0)
     {
        snprintf(user, sizeof(user), "%s", buf);
	  printf("user is  [%s] \n", user);
	  snprintf(ack, sizeof(ack), "%s", "ok!connected!\n");
	  write(connfd , ack , strlen(ack));	 
     }
     else
	  _exit(0);

     if ((pw = getpwnam(user)) == NULL)
     {
	   printf("user %s not exist\n", user);
	   _exit(0);
     }

     /* Make a copy of the password information and point pw at the local
          copy instead.  Otherwise, some systems (e.g. Linux) would clobber
          the static data through the getlogin call from log_su.
          Also, make sure pw->pw_shell is a nonempty string.
          It may be NULL when NEW_USER is a username that is retrieved via NIS (YP),
          but that doesn't have a default shell listed.  */
      pw_copy = *pw;
      pw = &pw_copy;
      pw->pw_name = xstrdup (pw->pw_name);
      pw->pw_passwd = xstrdup (pw->pw_passwd);
      pw->pw_dir = xstrdup (pw->pw_dir);
      pw->pw_shell = xstrdup (pw->pw_shell && pw->pw_shell[0]
	       	 	               ? pw->pw_shell
			                      : DEFAULT_SHELL);
      endpwent ();

     //before set to user id and user group id, use pam to do auth
     // and get user envrionment such as ulimit. if do this after set,
     // we need input password for the user.

     // auth
     authenticate (pw);

      int retval;
     // open pam session
      retval = pam_open_session (pamh, 0);
      if (is_pam_failure(retval))
      {
          cleanup_pam (retval);
	    printf("cannot opensession");
           exit(EXIT_FAILURE);
          //errx (EXIT_FAILURE, _("cannot open session: %s"),
	    //pam_strerror (pamh, retval);
       }
       else
            _pam_session_opened = 1;	 

     //2. change root to user
	 
     if (setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) == -1)
	   printf("setresgid %u: %.100s", (u_int)pw->pw_gid, strerror(errno));

     if (setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) == -1)
	   printf("setresuid %u: %.100s", (u_int)pw->pw_uid, strerror(errno));

     //3. create pipe 
     int pin[2], pout[2], perr[2];
	 
     if (pipe(pin) == -1) {
		error("%s: pipe in: %.100s", __func__, strerror(errno));
		_exit(0);
     }
     if (pipe(pout) == -1) {
		error("%s: pipe out: %.100s", __func__, strerror(errno));
		close(pin[0]);
		close(pin[1]);
		_exit(0);
     }
     if (pipe(perr) == -1) {
		error("%s: pipe err: %.100s", __func__,
		    strerror(errno));
		close(pin[0]);
		close(pin[1]);
		close(pout[0]);
		close(pout[1]);
		_exit(0);
     }
    
     //4.  fork child process and redirect stdin std out to pipe
    switch ((pid = fork())) {
	case -1:
		error("%s: fork: %.100s", __func__, strerror(errno));
		close(pin[0]);
		close(pin[1]);
		close(pout[0]);
		close(pout[1]);
		close(perr[0]);
		close(perr[1]);
		_exit(0);
	case 0:

             	/*
		 * Create a new session and process group since the 4.4BSD
		 * setlogin() affects the entire process group.
		 */
		if (setsid() == -1)
			error("setsid failed: %.100s", strerror(errno));
		
		/*
		 * Redirect stdin.  We close the parent side of the socket
		 * pair, and make the child side the standard input.
		 */
		close(pin[1]);
		if (dup2(pin[0], 0) == -1)
			perror("dup2 stdin");
		close(pin[0]);

		/* Redirect stdout. */
		close(pout[0]);
		if (dup2(pout[1], 1) == -1)
			perror("dup2 stdout");
		close(pout[1]);

		/* Redirect stderr. */
		close(perr[0]);
		if (dup2(perr[1], 2) == -1)
			perror("dup2 stderr");
		close(perr[1]);

		/* Do processing for the child (exec command etc). */
		do_child(pw);
	default:
		break;
	}

      /* We are the parent.  Close the child sides of the pipes. */
	close(pin[0]);
	close(pout[1]);
	close(perr[1]);

       while((n = read(connfd , buf , MAX_LINE)) > 0)
       {
	     //printf("server side recv [%s] \n", buf);

	     write(pin[1], buf, strlen(buf)); 

	     read(pout[0], pipe_buf, MAX_LINE);

	     //printf("read data form pipe is %s \n", pipe_buf);
		 
            write(connfd , pipe_buf , strlen(pipe_buf));	 
        }	 

        _exit(0);
}

void connection_process_pty(connfd)
{
}


/*
 * Main program for the upsshd server.
 */
void main(int ac, char **av)
{
	struct sockaddr_in servaddr , cliaddr;

	char pty_use_flag =  'n';
	
	int listenfd , connfd;
	pid_t childpid;

	char buf[MAX_LINE];

	socklen_t client;


       if(ac!=2)
	 	usage();

       if(strcmp(av[1], "0")==0)
       {
             pty_use_flag = 'n';
	      printf("not use pty\n");
       }else if(strcmp(av[1], "1")==0)
       {
             pty_use_flag = 'y';
	      printf("use pty\n");
       }else
       {
             usage();
       }

	if((listenfd = socket(AF_INET , SOCK_STREAM , 0)) < 0)
	{
		printf("socket error \n");
		exit(1);
	}

	bzero(&servaddr , sizeof(servaddr));

	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(SSH_DEFAULT_PORT);

       // avoid error: 
       // bind error : Address already in use
       int on=1;  
       if((setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))<0)  
       {  
           perror("setsockopt failed");  
           exit(1);  
       }  

	if(bind(listenfd , (struct sockaddr*)&servaddr , sizeof(servaddr)) < 0)
	{
		perror("bind error");
		exit(1);
	}

	if(listen(listenfd , 1000) < 0)
	{
		perror("listen error");
		exit(1);
	}

	for( ; ; )
	{
		client = sizeof(cliaddr);
		if((connfd = accept(listenfd , (struct sockaddr *)&cliaddr , &client)) < 0 )
		{
			perror("accept error");
			exit(1);
		}

		if((childpid = fork()) == 0) 
		{

		    // child process   
			close(listenfd);

                    if(pty_use_flag == 'n')
                    {
                        connection_process_no_pty(connfd);
                    }
			else  if(pty_use_flag == 'y')
			{
			     connection_process_pty(connfd);
			}
		}
		close(connfd);
	}

	close(listenfd);
       return;
}

