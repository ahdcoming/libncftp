/* monkeys.c
 *
 * This spawns multiple simultaneous instances of the FTP Monkey.
 * I use this to load test an FTP server.
 */

#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
#	define _CRT_SECURE_NO_WARNINGS 1
#endif

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include <ncftp.h>				/* Library header. */
#include <Strn.h>				/* Library header. */

static void
Usage(void)
{
	fprintf(stderr, "FTP Monkey Runner, copyright 1996-2004 by Mike Gleason, NcFTP Software.\n");
	fprintf(stderr, "Usage: ftp_monkeys [flags]\n");

	(void) fprintf(stderr, "\nGeneral Flags:\n\
  -u XX  Use username XX instead of anonymous.\n\
  -p XX  Use password XX with the username.\n\
  -P XX  Use port number XX instead of the default FTP service port (21).\n\
  -a XX  Quit after test has run for XX seconds (default: 7200).\n\
  -C     Send CLNT command with unique session identifier.\n\
  -D XX  Use remote directory XX as base.\n\
  -n XX  Use XX as the number of monkeys to run (default: 5).\n\
  -e XX  Intentionally prematurely timeout or close connections XX%% of sessions.\n\
  -h XX  Have monkeys connect to the host XX (default: Localhost).\n");
	(void) fprintf(stderr, "\nExamples:\n\
  ftp_monkeys -n 7 -a 3600 -h 127.0.0.1\n\
  ftp_monkeys -n 25 -a 600\n\
  ftp_monkeys -n 50\n\n");
#ifdef UNAME
	fprintf(stderr, "System: %s.\n", UNAME);
#endif
	exit(2);
}	/* Usage */



main_void_return_t
main(int argc, char **argv)
{
	int logfd;
	char lname[64];
	char seedstr[32];
	char dstr[32];
	char logstr[64];
	int c, f, i, n, seed, t, k;
	time_t now;
	struct tm *ltp;
	int es, status;
	char *a[32];
	const char *aarg, *Darg, *Parg, *uarg, *parg, *earg, *host;
	int Carg = 0;
	GetoptInfo opt;

	n = 5;
	aarg = "7200";
	Darg = NULL;
	Parg = NULL;
	uarg = NULL;
	parg = NULL;
	earg = NULL;
	host = "Localhost";

	GetoptReset(&opt);
	while ((c = Getopt(&opt, argc, argv, "a:CD:P:u:p:n:h:e:")) > 0) switch(c) {
		case 'a':
			aarg = opt.arg;
			break;
		case 'e':
			earg = opt.arg;
			break;
		case 'C':
			Carg = 1;
			break;
		case 'D':
			Darg = opt.arg;
			break;
		case 'P':
			Parg = opt.arg;
			break;
		case 'u':
			uarg = opt.arg;
			break;
		case 'p':
			parg = opt.arg;
			break;
		case 'n':
			n = atoi(opt.arg);
			break;
		case 'h':
			host = opt.arg;
			break;
		default:
			Usage();
	}

	time(&now);
	ltp = localtime(&now);
	strftime(dstr, sizeof(dstr), "%m/%d %H:%M:%S", ltp);

	a[0] = strdup("ftp_monkey");
	a[1] = strdup("-s");
	a[2] = strdup("0");
	a[3] = strdup("-R");
	a[4] = strdup("-a");
	a[5] = strdup(aarg);
	k = 6;
	if (Carg != 0) {
		a[k] = strdup("-C");
		k++;
	}
	if (earg != NULL) {
		a[k] = strdup("-e");
		k++;
		a[k] = strdup(earg);
		k++;
	}
	if (uarg != NULL) {
		a[k] = strdup("-u");
		k++;
		a[k] = strdup(uarg);
		k++;
	}
	if (parg != NULL) {
		a[k] = strdup("-p");
		k++;
		a[k] = strdup(parg);
		k++;
	}
	if (Parg != NULL) {
		a[k] = strdup("-P");
		k++;
		a[k] = strdup(Parg);
		k++;
	}
	if (Darg != NULL) {
		a[k] = strdup("-D");
		k++;
		a[k] = strdup(Darg);
		k++;
	}
	a[k] = strdup(host);
	a[++k] = NULL;

	for (seed=1; seed<=n; seed++) {
		i = seed;
		sprintf(lname, "out.%02d", i);
		logfd = open(lname, O_WRONLY|O_APPEND|O_TRUNC|O_CREAT, 00666);
		if (logfd < 0) {
			perror(lname);
			exit(1);
		}
		sprintf(seedstr, "%d", seed);
		sprintf(logstr, "%s  seed=%d\n", dstr, seed);
		write(logfd, logstr, strlen(logstr));
		a[2] = seedstr;
		if ((f = fork()) < 0) {
			perror("fork");
			exit(1);
		} else if (f == 0) {
			/* child */
			close(1);
			close(2);
			dup2(logfd, 1);
			dup2(logfd, 2);
			close(logfd);
			execvp(a[0], a);
			perror(a[0]);
			exit(1);
		} else {
			close(logfd);
			printf("%s  ", dstr);
			for (t=0; a[t] != NULL; t++)
				printf("%s ", a[t]);
			printf("> %s  (pid: %u)\n", lname, f);
			sleep(1);
		}
	}

	for ( ; ; ) {
		if ((f = wait(&status)) > 0) {
			if (WIFEXITED(status) == 0)
				continue;
			es = WEXITSTATUS(status);

			time(&now);
			ltp = localtime(&now);
			strftime(dstr, sizeof(dstr), "%m/%d %H:%M:%S", ltp);
			fprintf(stdout, "%s  pid=%d exited=%d.\n", dstr, f, es);
		} else if ((f < 0) && (errno != EINTR)) {
			break;		/* all monkeys done */
		}
	}
	exit(0);
}	/* main */
