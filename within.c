/*
 * within.c
 * Copyright (c) 2018, Sijmen J. Mulder <ik@sjmulder.nl>
 *
 * within is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * within is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with within. If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Implementation notes
 *
 * Based around a select() event loop. As many jobs are started as -j allows
 * (1 by default), forking, descending into the given directory and executing
 * the command. For each job, both standard output and standard error are
 * redirected to a pipe that's read by a 'piper' which adds the 'directory:'
 * prefixes to the output. These pipers are effectively coroutines.
 *
 * The event loop waits for finished jobs, starting new ones if there are
 * directories left, and for data on the pipes.
 *
 * The select() specific code should be fairly easy to swap out. Point in
 * case, originally kqueue was used.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>

#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <err.h>
#include <getopt.h>
#include <signal.h>
#include <fcntl.h>

#define MAX(a,b) ((a)>(b)?(a):(b))

struct piper {
	int in_fd;
	FILE *out_file;
	const char *prefix;	/* directory name */
	bool newline;		/* 1 if last character was a newline */
	struct piper *next;	/* 't is but a linked list! */
};

static void parse_options(int, char **);
static void usage(void);
static void start_job(const char *);
static void start_piper(int, FILE *, const char *);
static void run_piper(struct piper *);
static void remove_piper(struct piper *);
static void sig_chld(int);

/* command line options */
static int max_jobs = 1;
static int num_directories;
static char **directories;
static char **command;

struct piper *pipers;

int
main(int argc, char **argv)
{
	int directory = 0;
	int num_jobs = 0;
	fd_set piper_fds;
	struct piper *piper;
	struct piper *piper_next;
	int nfds;
	int status = 0;
	int child_status;
	pid_t child_pid;

#if defined(__OpenBSD__)
	pledge("stdio fork exec", NULL);
#endif

	parse_options(argc, argv);

	signal(SIGCHLD, sig_chld);

	while (num_jobs || pipers || directory < num_directories) {
		/* start new jobs */

		while (num_jobs < max_jobs && directory < num_directories) {
			start_job(directories[directory]);
			num_jobs++;
			directory++;
		}

		/* wait for data or SIGCHLD interrupt */

		FD_ZERO(&piper_fds);
		nfds = 0;
		for (piper = pipers; piper; piper = piper->next) {
			FD_SET(piper->in_fd, &piper_fds);
			nfds = MAX(nfds, piper->in_fd+1);
		}

		if (select(nfds, &piper_fds, NULL, NULL, NULL) != -1) {
			for (piper = pipers; piper; piper = piper_next) {
				/* dereference now, prevent use after free */
				piper_next = piper->next;

				if (FD_ISSET(piper->in_fd, &piper_fds))
					run_piper(piper);
			}
		} else if (errno != EINTR)
			err(1, "select");

		/* collect child exits */

		while (num_jobs) {
			child_pid = waitpid(-1, &child_status, WNOHANG);
			if (child_pid == -1)
				err(1, "waitpid");
			if (child_pid == 0)
				break;
			num_jobs--;
			if (child_status)
				status = 1; /* safer than child_status */
		}
	}

	return status;
}

static void
parse_options(int argc, char **argv)
{
	int c, i;

	while ((c = getopt(argc, argv, "j:")) != -1) {
		switch (c) {
		case 'j':
			max_jobs = (int)strtol(optarg, NULL, 10);
			if (max_jobs < 1)
				errx(1, "invalid -j: %s", optarg);
			break;
		default:
			exit(1);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 2)
		usage();

	for (i = 0; i < argc; i++) {
		/* directories separated from command with - or -- */
		if (strcmp("-", argv[i]) && strcmp("--", argv[i]))
			continue;
		if (i < 1 || i+1 >= argc)
			usage();
		num_directories = i;
		directories = argv;
		command = argv + i + 1;
		break;
	}

	if (i == argc) {
		num_directories = 1;
		directories = argv;
		command = argv+1;
	}
}

static void
usage(void)
{
	fprintf(stderr, "usage: within [-j jobs] directory [... -] command "
	    "...\n");
	exit(1);
}

static void
start_job(const char *directory)
{
	int stdout_pipe[2];
	int stderr_pipe[2];
	pid_t pid;
	
	if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1)
		err(1, "pipe");

	switch ((pid = fork())) {
	case -1:
		err(1, "fork");

	case 0:
		if (dup2(stdout_pipe[1], STDOUT_FILENO) == -1)
			err(1, "dup2 stdout");
		if (dup2(stderr_pipe[1], STDERR_FILENO) == -1)
			err(1, "dup2 stderr");

		close(stdout_pipe[0]);
		close(stdout_pipe[1]);
		close(stderr_pipe[0]);
		close(stderr_pipe[1]);

		if (chdir(directory) == -1)
			err(1, "chdir");

		execvp(command[0], command);
		err(1, "%s", command[0]);

	default:
		close(stdout_pipe[1]);
		close(stderr_pipe[1]);

		start_piper(stdout_pipe[0], stdout, directory);
		start_piper(stderr_pipe[0], stderr, directory);
		break;
	}
}

static void
start_piper(int in_fd, FILE *out_file, const char *prefix)
{
	struct piper *piper;
	int flags;
	
	if (!(piper = malloc(sizeof(*piper))))
		err(1, "malloc");

	memset(piper, 0, sizeof(*piper));
	piper->in_fd = in_fd;
	piper->out_file = out_file;
	piper->prefix = prefix;
	piper->newline = 1;

	if ((flags = fcntl(in_fd, F_GETFL)) == -1)
		err(1, "F_GETFL");
	if (fcntl(in_fd, F_SETFL, flags | O_NONBLOCK) == -1)
		err(1, "F_SETFL");

	/* add to list */
	piper->next = pipers;
	pipers = piper;
}

static void
run_piper(struct piper *piper)
{
	char buf[4096];
	ssize_t num_read, i;

	while ((num_read = read(piper->in_fd, buf, sizeof(buf))) > 0) {
		for (i = 0; i < num_read; i++) {
			if (piper->newline)
				fprintf(piper->out_file, "%s: ",
				    piper->prefix);
			fputc(buf[i], piper->out_file);
			piper->newline = buf[i] == '\n';
		}
	}

	if (num_read == 0)
		remove_piper(piper);
	else if (num_read == -1 && errno != EAGAIN)
		err(1, "read");
}

static void
remove_piper(struct piper *piper)
{
	struct piper **pp;

	for (pp = &pipers; *pp; pp = &(*pp)->next) {
		if (*pp == piper) {
			*pp = piper->next;
			free(piper);
			break;
		}
	}
}

static void
sig_chld(int sig)
{
	(void)sig;

	/* nothing, we just want select() interrupted */
}
