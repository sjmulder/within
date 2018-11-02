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
 * Based around a kqueue event loop. As many jobs are started as -j allows (1
 * by default), forking, descending into the given directory and executing the
 * command. For each job, both standard output and standard error are
 * redirected to a pipe that's read by a 'piper' which adds the 'directory:'
 * prefixes to the output. These pipers are effectively coroutines.
 *
 * The event loop waits for finished jobs, starting new ones if there are
 * directories left, and for data on the pipes.
 *
 * All kqueue specific code is in main() the register/deregister functions
 * and it should be possible to swap this out for a different system.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <err.h>
#include <getopt.h>

/* stored as .udata in the EVFILT_READ kevents */
struct piper {
	int in_fd;
	FILE *out_file;
	const char *prefix;	/* directory name */
	bool newline;		/* 1 if last character was a newline */
};

static void parse_options(int, char **);
static void usage(void);
static void register_job(pid_t);
static void register_piper(struct piper *);
static void deregister_piper(struct piper *);
static void start_job(const char *);
static void start_piper(int, FILE *, const char *);
static void run_piper(struct piper *, size_t);

/* command line options */
static int max_jobs = 1;
static int num_directories;
static char **directories;
static char **command;

static int queue;

int
main(int argc, char **argv)
{
	int directory = 0;
	int num_jobs = 0;
	struct kevent event;

	parse_options(argc, argv);

	if ((queue = kqueue()) == -1)
		err(1, "kqueue");

	while (num_jobs || directory < num_directories) {
		while (num_jobs < max_jobs && directory < num_directories) {
			start_job(directories[directory]);
			num_jobs++;
			directory++;
		}

		if (kevent(queue, NULL, 0, &event, 1, NULL) == -1)
			err(1, "polling kevent");

		switch (event.filter) {
		case EVFILT_PROC:
			num_jobs--;
			break;

		case EVFILT_READ:
			run_piper(event.udata, (size_t)event.data);
			
			if (event.flags & EV_EOF) {
				deregister_piper(event.udata);
				close(((struct piper *)event.udata)->in_fd);
				free(event.udata);
			}
			
			break;
		}
	}
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
		if (strcmp("--", argv[i]) != 0)
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
	fprintf(stderr, "usage: within [-j jobs] directory [... --] command "
	    "...\n");
	exit(1);
}

static void
register_job(pid_t pid)
{
	struct kevent change;

	memset(&change, 0, sizeof(change));
	change.ident = pid;
	change.filter = EVFILT_PROC;
	change.flags = EV_ADD | EV_ENABLE | EV_ONESHOT;
	change.fflags = NOTE_EXIT;

	if (kevent(queue, &change, 1, NULL, 0, NULL) == -1)
		err(1, "adding exit kevent");
}

static void
register_piper(struct piper *piper)
{
	struct kevent change;
	
	memset(&change, 0, sizeof(change));
	change.ident = piper->in_fd;
	change.filter = EVFILT_READ;
	change.flags = EV_ADD | EV_ENABLE;
	change.udata = piper;
	
	if (kevent(queue, &change, 1, NULL, 0, NULL) == -1)
		err(1, "adding file kevent");
}

static void
deregister_piper(struct piper *piper)
{
	struct kevent change;
	
	memset(&change, 0, sizeof(change));
	change.ident = piper->in_fd;
	change.filter = EVFILT_READ;
	change.flags = EV_DELETE;
	change.udata = piper;
	
	if (kevent(queue, &change, 1, NULL, 0, NULL) == -1)
		err(1, "removing file kevent");
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

		register_job(pid);
			
		start_piper(stdout_pipe[0], stdout, directory);
		start_piper(stderr_pipe[0], stderr, directory);
		break;
	}
}

static void
start_piper(int in_fd, FILE *out_file, const char *prefix)
{
	struct piper *piper;
	
	if (!(piper = malloc(sizeof(*piper))))
		err(1, "malloc");

	memset(piper, 0, sizeof(*piper));
	piper->in_fd = in_fd;
	piper->out_file = out_file;
	piper->prefix = prefix;
	piper->newline = 1;
	
	register_piper(piper);
}

static void
run_piper(struct piper *piper, size_t num_readable)
{
	char *data;
	ssize_t num_read, i;

	if (!(data = malloc(num_readable)))
		err(1, "malloc");
	if ((num_read = read(piper->in_fd, data, num_readable)) == -1)
		err(1, "read");

	for (i = 0; i < num_read; i++) {
		if (piper->newline)
			fprintf(piper->out_file, "%s: ", piper->prefix);
		fputc(data[i], piper->out_file);
		piper->newline = data[i] == '\n';
	}

	free(data);
}
