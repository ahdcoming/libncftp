/* minincftp.c
 *
 * Example of a command-line shell interface to FTP using the library.
 */

#define VERSION "3.1.0"

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
#	define _CRT_SECURE_NO_WARNINGS 1
#endif

#include <ncftp.h>				/* Library header. */
#include <Strn.h>				/* Library header. */

#define kKilobyte 1024
#define kMegabyte (kKilobyte * 1024)
#define kGigabyte ((long) kMegabyte * 1024L)
#define kTerabyte ((double) kGigabyte * 1024.0)

#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
#	define sleep WinSleep
#endif

FTPLibraryInfo li;
FTPConnectionInfo fi;

static void
Usage(void)
{
	fprintf(stderr, "MiniNcFTP, copyright 1995 by Mike Gleason, NcFTP Software.\n");
	fprintf(stderr, "Usage: minincftp [-P port] [-u user] [-p password] hostname\n");
	fprintf(stderr, "Library version: %s.\n", gLibNcFTPVersion + 5);
#ifdef UNAME
	fprintf(stderr, "System: %s.\n", UNAME);
#endif
	DisposeWinsock();
	exit(2);
}	/* Usage */




static void
Abort(int sigNum)
{
	signal(sigNum, Abort);

	/* Hopefully the I/O operation in progress
	 * will complete, and we'll abort before
	 * it starts a new block.
	 */
	fi.cancelXfer++;

	/* If the user appears to be getting impatient,
	 * restore the default signal handler so the
	 * next ^C abends the program.
	 */
	if (fi.cancelXfer >= 2)
		signal(sigNum, SIG_DFL);
}	/* Abort */



/* This will abbreviate a string so that it fits into max characters.
 * It will use ellipses as appropriate.  Make sure the string has
 * at least max + 1 characters allocated for it.
 */
static void
AbbrevStr(char *dst, const char *src, size_t max, int mode)
{
	int len;

	len = (int) strlen(src);
	if (len > (int) max) {
		if (mode == 0) {
			/* ...Put ellipses at left */
			(void) strcpy(dst, "...");
			(void) Strncat(dst, src + len - (int) max + 3, max + 1);
		} else {
			/* Put ellipses at right... */
			(void) Strncpy(dst, src, max + 1);
			(void) strcpy(dst + max - 3, "...");
		}
	} else {
		(void) Strncpy(dst, src, max + 1);
	}
}	/* AbbrevStr */



static double
FileSize(double size, const char **uStr0, double *uMult0)
{
	double uMult, uTotal;
	const char *uStr;

	/* The comparisons below may look odd, but the reason
	 * for them is that we only want a maximum of 3 digits
	 * before the decimal point.  (I.e., we don't want to
	 * see "1017.2 kB", instead we want "0.99 MB".
	 */
	if (size > (999.5 * kGigabyte)) {
		uStr = "TB";
		uMult = kTerabyte;
	} else if (size > (999.5 * kMegabyte)) {
		uStr = "GB";
		uMult = kGigabyte;
	} else if (size > (999.5 * kKilobyte)) {
		uStr = "MB";
		uMult = kMegabyte;
	} else if (size > 999.5) {
		uStr = "kB";
		uMult = 1024;
	} else {
		uStr = "B";
		uMult = 1;
	}
	if (uStr0 != NULL)
		*uStr0 = uStr;
	if (uMult0 != NULL)
		*uMult0 = uMult;
	uTotal = size / ((double) uMult);
	if (uTotal < 0.0)
		uTotal = 0.0;
	return (uTotal);
}	/* FileSize */




static void
PrSizeAndRateMeter(const FTPCIPtr cip, int mode)
{
	double rate;
	const char *rStr;
	char localName[32];
	char line[128];
	size_t i;

	switch (mode) {
		case kPrInitMsg:
			if (cip->lname == NULL) {
				localName[0] = '\0';
			} else {
				AbbrevStr(localName, cip->lname, sizeof(localName) - 2, 0);
				(void) STRNCAT(localName, ":");
			}

			(void) fprintf(stderr, "%-32s", localName);
			break;

		case kPrUpdateMsg:
			rate = FileSize(cip->kBytesPerSec * 1024.0, &rStr, NULL);

			if (cip->lname == NULL) {
				localName[0] = '\0';
			} else {
				AbbrevStr(localName, cip->lname, sizeof(localName) - 2, 0);
				(void) STRNCAT(localName, ":");
			}

#ifdef PRINTF_LONG_LONG_LLD
			(void) sprintf(line, "%-32s  %10lld bytes  %6.2f %s/s",
				localName,
				(longest_int) (cip->bytesTransferred + cip->startPoint),
				rate,
				rStr
			);
#elif defined(PRINTF_LONG_LONG_QD)
			(void) sprintf(line, "%-32s  %10qd bytes  %6.2f %s/s",
				localName,
				(longest_int) (cip->bytesTransferred + cip->startPoint),
				rate,
				rStr
			);
#else
			(void) sprintf(line, "%-32s  %10ld bytes  %6.2f %s/s",
				localName,
				(long) (cip->bytesTransferred + cip->startPoint),
				rate,
				rStr
			);
#endif

			/* Pad the rest of the line with spaces, to erase any
			 * stuff that might have been left over from the last
			 * update.
			 */
			for (i=strlen(line); i < 80 - 2; i++)
				line[i] = ' ';
			line[i] = '\0';

			/* Print the updated information. */
			(void) fprintf(stderr, "\r%s", line);
			break;

		case kPrEndMsg:
			(void) fprintf(stderr, "\n\r");
			break;
	}
}	/* PrSizeAndRateMeter */



static int
Ls(const FTPCIPtr cip, char *cmdstr, char *lsflag)
{
	int cmd;
	int result;

	cmd = (cmdstr[0] == 'd') ? 1 : 0;
	fflush(stdout);
	result = FTPList(cip, STDOUT_FILENO, cmd, lsflag);
	return result;
}	/* Ls */



static void
Shell(const FTPCIPtr cip)
{
	char line[256];
	char argv[10][80], *cp, *token;
	char s1[256];
	int i, c1, code;
	int argc;
	time_t mdtm;
	longest_int size;

	if (fi.dataPortMode != kPassiveMode) {
		printf("Using PORT (non-passive) mode for data connections.\n");
	} else {
		printf("Using PASV (passive) mode for data connections.\n");
	}
	printf("Type \"passive\" to toggle between PORT and PASV.\n");
	printf("\n");

	for (;;) {
nextcmd:
		code = kNoErr;
		printf("MiniNcFTP> ");
		memset(line, 0, sizeof(line));
		memset(argv, 0, sizeof(argv));
		if (fgets(line, sizeof(line), stdin) == NULL)
			break;
		cp = line;
		for (argc=0; argc<10; argc++) {
			argv[argc][0] = '\0';
			token = strtok(cp, " \n\t\r");
			if (token == NULL)
				break;
			STRNCPY(argv[argc], token);
			if (argv[argc][sizeof(argv[0]) - 1] != '\0') {
				fprintf(stderr, "parameter too long\n");
				goto nextcmd;
			}
			cp = NULL;
		}
		printf("\n");
		c1 = argv[0][0];
		cip->errNo = 0;

#define USAGECHECK(a,n) if ((argc - 1) < n) { fprintf(stderr, "Error: \"%s\" requires %d parameter%s.\n", #a, n, "s"); goto nextcmd; }
#define USAGECHECK1(a) if ((argc - 1) < 1) { fprintf(stderr, "Error: \"%s\" requires %d parameter%s.\n", #a, 1, ""); goto nextcmd; }
		switch (c1) {
			case 'c':
				if (strcasecmp(argv[0], "cd") == 0) {
					/* chdir, cd */
					USAGECHECK1(cd)
					code = FTPChdir(cip, argv[1]);
				} else if (strcasecmp(argv[0], "chmod") == 0) {
					USAGECHECK(chmod, 2)
					code = FTPChmod(cip, argv[2], argv[1], kGlobYes);
				} else goto bad;
				break;
			case 'd':
				if (strcasecmp(argv[0], "dir") == 0) {
					code = Ls(cip, argv[0], argv[1]); /* dir */
				} else if (strcasecmp(argv[0], "delete") == 0) {
					USAGECHECK1(delete)
					code = FTPDelete(cip, argv[1], kRecursiveNo, kGlobYes);
				} else goto bad;
				break;
			case 'g':
				if (strcasecmp(argv[0], "get1") == 0) {
					USAGECHECK(get1, 2)
					(void) signal(SIGINT, Abort);
					code = FTPGetOneFile3(cip, argv[1], argv[2], kTypeBinary, (-1), kResumeYes, kAppendNo, kDeleteNo, kNoFTPConfirmResumeDownloadProc, 0);
					(void) signal(SIGINT, SIG_DFL);
					if (cip->bytesTransferred > 0L)
						printf("%lu bytes transferred, %.2f kB/sec.\n", (unsigned long) cip->bytesTransferred, cip->kBytesPerSec);
				} else if (strcasecmp(argv[0], "get") == 0) {
					USAGECHECK1(get)
					(void) signal(SIGINT, Abort);
					code = FTPGetFiles3(cip, argv[1], argv[2], kRecursiveNo, kGlobYes, kTypeBinary, kResumeYes, kAppendNo, kDeleteNo, kTarNo, kNoFTPConfirmResumeDownloadProc, 0);
					(void) signal(SIGINT, SIG_DFL);
					if (cip->bytesTransferred > 0L)
						printf("%lu bytes transferred, %.2f kB/sec.\n", (unsigned long) cip->bytesTransferred, cip->kBytesPerSec);
				} else goto bad;
				break;
			case 'l':
				if (strcasecmp(argv[0], "ls") == 0) {
					(void) signal(SIGINT, Abort);
					code = Ls(cip, argv[0], argv[1]);
					(void) signal(SIGINT, SIG_DFL);
					if (cip->bytesTransferred > 0L)
						printf("%lu bytes transferred, %.2f kB/sec.\n", (unsigned long) cip->bytesTransferred, cip->kBytesPerSec);
				} else if (strcasecmp(argv[0], "lcd") == 0) {
					USAGECHECK1(lcd)
#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
					if (_chdir(argv[1]) < 0)
#else
					if (chdir(argv[1]) < 0)
#endif
					{
						perror(argv[1]);
					} else {
						if (FTPGetLocalCWD(line, sizeof(line)) != NULL) {
							printf("Current local directory is: %s\n", line);
						}
					}
				} else goto bad;
				break;
			case 'm':
				if (strcasecmp(argv[0], "mkdir") == 0) {
					USAGECHECK1(mkdir)
					code = FTPMkdir(cip, argv[1], kRecursiveYes);
				} else if (strcasecmp(argv[0], "modtime") == 0) {
					USAGECHECK1(modtime)
					code = FTPFileModificationTime(cip, argv[1], &mdtm);
					printf("%s modtime = %lu.\n",
						argv[1], (unsigned long) mdtm);
				} else goto bad;
				break;
			case 'p':
				if (strcasecmp(argv[0], "passive") == 0) {
					if (fi.dataPortMode == kPassiveMode) {
						fi.dataPortMode = kSendPortMode;
						printf("Using PORT (non-passive) mode for data connections.\n");
					} else {
						fi.dataPortMode = kPassiveMode;
						printf("Using PASV (passive) mode for data connections.\n");
					}
				} else if (strcasecmp(argv[0], "pwd") == 0) {
					code = FTPGetCWD(cip, s1, sizeof(s1));	/* pwd */
					printf("cwd = [%s]\n", s1);
				} else if (strcasecmp(argv[0], "put1") == 0) {
					USAGECHECK(put1, 2)
					(void) signal(SIGINT, Abort);
					code = FTPPutOneFile(cip, argv[1], argv[2]);
					(void) signal(SIGINT, SIG_DFL);
					if (cip->bytesTransferred > 0L)
						printf("%lu bytes transferred, %.2f kB/sec.\n", (unsigned long) cip->bytesTransferred, cip->kBytesPerSec);
				} else if (strcasecmp(argv[0], "put") == 0) {
					USAGECHECK1(put)
					(void) signal(SIGINT, Abort);
					code = FTPPutFiles(cip, argv[1], argv[2], kRecursiveNo, kGlobYes);
					(void) signal(SIGINT, SIG_DFL);
					if (cip->bytesTransferred > 0L)
						printf("%lu bytes transferred, %.2f kB/sec.\n", (unsigned long) cip->bytesTransferred, cip->kBytesPerSec);
				} else goto bad;
				break;
			case 'q':
			case 'Q':
				if (strcasecmp(argv[0], "quote") == 0) {
					USAGECHECK1(quote)
					STRNCPY(s1, argv[1]);
					for (i=2; i<10; i++) {
						if (argv[i][0] == '\0') break;
						STRNCAT(s1, " ");
						STRNCAT(s1, argv[i]);
					}
					FTPCmd(cip, "%s", s1);
				} else if (strcasecmp(argv[0], "quit") == 0) {
					return;
				} else goto bad;
				break;
			case 'r':
				if (strcasecmp(argv[0], "rename") == 0) {
					USAGECHECK(rename, 2)
					code = FTPRename(cip, argv[1], argv[2]);
				} else if (strcasecmp(argv[0], "rmdir") == 0) {
					USAGECHECK1(rmdir)
					code = FTPRmdir(cip, argv[1], kRecursiveNo, kGlobYes);
				} else goto bad;
				break;
			case 's':
				if (strcasecmp(argv[0], "size") == 0) {
					USAGECHECK1(size)
#ifdef PRINTF_LONG_LONG
					code = FTPFileSize(cip, argv[1], &size, 'I');
					printf("%s size = " PRINTF_LONG_LONG ".\n", argv[1], size);
#else
					code = FTPFileSize(cip, argv[1], &size, 'I');
					printf("%s size = %lu.\n", argv[1], (unsigned long) size);
#endif
				} else goto bad;
				break;
			case 'u':
				if (strcasecmp(argv[0], "umask") == 0) {
					USAGECHECK1(umask)
					code = FTPUmask(cip, argv[1]);
				} else goto bad;
				break;
			case 'X':
			case 'x':
				return;
			case '\0':
				break;
			default:
			bad:
				printf("Commands are:  cd      dir    ls      passive quit   rmdir\n");
				printf("               chmod   get    mkdir   put     quote  size\n");
				printf("               delete  lcd    modtime pwd     rename umask\n");
		}
		if (code != kNoErr) {
			fprintf(stderr, "Return code for %s was %d (%s).\n", argv[0], code, FTPStrError(code));
		}
	}
}	/* Shell */





main_void_return_t
main(int argc, char **argv)
{
	int result, c;
	GetoptInfo opt;

	InitWinsock();

	result = FTPInitLibrary(&li);
	if (result < 0) {
		fprintf(stderr, "Init library error %d.\n", result);
		DisposeWinsock();
		exit(1);
	}
	result = FTPInitConnectionInfo(&li, &fi, kDefaultFTPBufSize);
	if (result < 0) {
		fprintf(stderr, "Init connection info error %d.\n", result);
		DisposeWinsock();
		exit(1);
	}

	fi.debugLog = stdout;
	fi.errLog = stderr;
	fi.progress = PrSizeAndRateMeter;
	fi.dataPortMode = kSendPortMode;
	STRNCPY(fi.user, "anonymous");

	GetoptReset(&opt);
	while ((c = Getopt(&opt, argc, argv, "d:e:P:p:u:F")) > 0) switch(c) {
		case 'P':
			fi.port = atoi(opt.arg);	
			break;
		case 'u':
			STRNCPY(fi.user, opt.arg);
			break;
		case 'p':
			STRNCPY(fi.pass, opt.arg);	/* Don't recommend doing this! */
			break;
		case 'e':
			fi.errLog = fopen(opt.arg, "a");
			break;
		case 'd':
			fi.debugLog = fopen(opt.arg, "a");
			break;
		case 'F':
			if (fi.dataPortMode == kPassiveMode)
				fi.dataPortMode = kSendPortMode;
			else
				fi.dataPortMode = kPassiveMode;
			break;
		default:
			Usage();
	}
	if (opt.ind > argc - 1)
		Usage();

	if ((strcmp(fi.user, "anonymous") != 0) && (strcmp(fi.user, "ftp") != 0) && (fi.pass[0] == '\0'))
		(void) GetPass("Password: ", fi.pass, sizeof(fi.pass));

	STRNCPY(fi.host, argv[opt.ind]);
	if ((result = FTPOpenHost(&fi)) < 0) {
		fprintf(stderr, "Cannot open host: %s.\n", FTPStrError(result));
		DisposeWinsock();
		exit(1);
	}
	Shell(&fi);
	FTPCloseHost(&fi);
	DisposeWinsock();
	exit(0);
}	/* main */
