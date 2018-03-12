/* monkey.c
 *
 * The FTP Monkey can be used to simulate a FTP user.  It opens a host,
 * randomly changes directories and fetches files.
 */

#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
#	define _CRT_SECURE_NO_WARNINGS 1
#endif

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include <ncftp.h>				/* Library header. */
#include <Strn.h>				/* Library header. */
#include <sio.h>				/* Library header. */

#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
#	define sleep WinSleep
#endif

#define kYesCloseYesQuit 0
#define kYesCloseNoQuit 1
#define kNoCloseNoQuit 2

FTPLineList gDir;
int gHaveDir = 0;
int gQuitMode = kYesCloseYesQuit;
int gPrintLists = 0;
int gLoops = 0;
int gGotSig = 0;
int gMaxLoops = 0;
int gCloseAndReconnect = 0;
int gNoMlst = 1;
int gErrOutOnPurpose = 0;
int gErrOutType = 0;
int gErrOutTypeToUse = 0;
char gSessionIDStr[32];
unsigned int gSeed = 1;
int gCLNT = 0;
struct timeval gSessionStart, gMonkeyStart;
time_t gAlarmClock = 0;
const char *gMainDir = "/pub";
char gPasswd[64];
FTPLibraryInfo li;
FTPConnectionInfo fi;

#define kErrStream stdout

#define c_quit 0
#define c_cd 1
#define c_pwd 2
#define c_dir 3
#define c_get 4
#define c_max 5

int gWeights[c_max] = {
	10,	/* quit */
	20,	/* cd */
	5,	/* pwd */
	15,	/* dir */
	50,	/* get */
};

#ifdef O_S
const char gOS[] = O_S;
#elif defined(WIN32) || defined(_WINDOWS)
const char gOS[] = "Windows";
#else
const char gOS[] = "UNIX";
#endif

#define kErrOutType_TimeoutAfterChdir 1
#define kErrOutType_TimeoutDataTransfer 2
#define kErrOutType_CloseDataTransfer 3
#define kErrOutType_ShutdownDataTransfer 4
#define kErrOutType_NumTypes 4

static void
MsgStart(const char *const fmt, ...)
#if (defined(__GNUC__)) && (__GNUC__ >= 2)
__attribute__ ((format (printf, 1, 2)))
#endif
;

static void
Usage(void)
{
	fprintf(kErrStream, "FTP Monkey, copyright 1996-2004 by Mike Gleason, NcFTP Software.\n");
	fprintf(kErrStream, "Usage: ftp_monkey [flags] hostname\n");

	(void) fprintf(kErrStream, "\nGeneral Flags:\n\
  -u XX  Use username XX instead of anonymous.\n\
  -p XX  Use password XX with the username.\n\
  -P XX  Use port number XX instead of the default FTP service port (21).\n\
  -d XX  Use the file XX for debug logging.\n");
	(void) fprintf(kErrStream, "\nMonkey Flags:\n\
  -a XX  Quit after test has run for XX seconds.\n\
  -m XX  Quit after test has run for XX operations.\n\
  -s XX  Set random seed to XX.\n\
  -C     Send CLNT command with unique session identifier.\n\
  -D XX  Use remote directory XX as base.\n\
  -e XX  Intentionally prematurely timeout or close connections XX%% of sessions.\n\
  -l     Print the output from directory listings.\n\
  -Q     Monkey may not end FTP session nor quit.\n\
  -R     Monkey may end FTP session, but must reconnect and start a new one.\n\
  -S     Monkey may end FTP session and quit (default).\n");
	(void) fprintf(kErrStream, "\nNotes:\n\
  The default behavior (-S) is to have Monkey simulate exactly one FTP session\n\
  and quit after a random number of operations.  To have Monkey simulate exactly\n\
  one FTP session and stay logged in forever (or until the limits specified by\n\
  -a or -m are met), use -Q mode.  To have Monkey continually simulate FTP\n\
  sessions of random number of operations, use -R.\n\n");
	fprintf(kErrStream, "Library version: %s.\n", gLibNcFTPVersion + 5);
#ifdef UNAME
	fprintf(kErrStream, "System: %s.\n", UNAME);
#endif
	DisposeWinsock();
	exit(2);
}	/* Usage */




static char *
GetPass2(const char *const prompt)
{
#ifdef HAVE_GETPASS
	return getpass(prompt);
#elif defined(_CONSOLE) && (defined(WIN32) || defined(_WINDOWS))
	static char pwbuf[128];
	char *dst, *dlim;
	int c;

	(void) memset(pwbuf, 0, sizeof(pwbuf));
	if (! _isatty(_fileno(stdout)))
		return (pwbuf);
	(void) fputs(prompt, stdout);
	(void) fflush(stdout);

	for (dst = pwbuf, dlim = dst + sizeof(pwbuf) - 1;;) {
		c = _getch();
		if ((c == 0) || (c == 0xe0)) {
			/* The key is a function or arrow key; read and discard. */
			(void) _getch();
		}
		if ((c == '\r') || (c == '\n'))
			break;
		if (dst < dlim)
			*dst++ = (char) c;
	}
	*dst = '\0';

	(void) fflush(stdout);
	(void) fflush(stdin);
	return (pwbuf);
#else
	static char pwbuf[128];

	(void) memset(pwbuf, 0, sizeof(pwbuf));
#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
	if (! _isatty(_fileno(stdout)))
		return (pwbuf);
#else
	if (! isatty(STDERR_FILENO))
		return (pwbuf);
#endif
	(void) fputs(prompt, stdout);
	(void) fflush(stdout);
	(void) FGets(pwbuf, sizeof(pwbuf), stdin);
	(void) fflush(stdout);
	(void) fflush(stdin);
	return (pwbuf);
#endif
}	/* GetPass2 */



static int
Ri(int a)
{
	int b;

	b = rand() % a;
	return (b);
}	/* Ri */


static int
Rp(int a)
{
	int b;

	b = rand() % 100;
	if (b < a)
		return (1);
	return (0);
}	/* Rp */



/*VARARGS*/
static void
MsgStart(const char *const fmt, ...)
{
	va_list ap;
	char buf[256];

	va_start(ap, fmt);
#ifdef HAVE_VSNPRINTF
	(void) vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
	buf[sizeof(buf) - 1] = '\0';
#else
	(void) vsprintf(buf, fmt, ap);
#endif

	(void) printf("%-5d %-20s  ", gLoops, buf);
	(void) fflush(stdout);
	va_end(ap);
}	/* MsgStart */



static int
MonkeyOpenHost(const FTPCIPtr cip)
{
	int result;
	time_t now;
	struct timeval t0;
	double dura;
	struct tm *ltp;
	char dstr[32];

	gettimeofday(&gSessionStart, NULL);
	time(&now);
	ltp = localtime(&now);
	strftime(dstr, sizeof(dstr), "%Y-%m-%d %H:%M:%S", ltp);
	printf("\n-- Opening %s as %s on %s --\n", cip->host, cip->user, dstr);

	STRNCPY(cip->pass, gPasswd);
	++gLoops;
	MsgStart("open");
	sprintf(gSessionIDStr, "(%d:%lu)", gSeed, (unsigned long) now);

	gErrOutType = 0;
	if ((gErrOutOnPurpose > 0) && (Rp(gErrOutOnPurpose))) {
		/* We're going to errout on purpose, now decide
		 * what it is.
		 */
		if (gErrOutTypeToUse == 0)
			gErrOutType = 1 + (rand() % kErrOutType_NumTypes);
		else
			gErrOutType = gErrOutTypeToUse;
	}


	gettimeofday(&t0, NULL);
	result = FTPOpenHost(cip);

	if ((result == kNoErr) && (gCLNT != 0))
		(void) FTPCmd(&fi, "CLNT ftp_monkey %.5s %s SessionID=%.31s", kLibraryVersion + 14, gOS, gSessionIDStr);

	dura = FTPDuration(&t0);
	printf("%7.3f sec   ID = %s\n", dura, gSessionIDStr);
	fflush(stdout);

	if (result < 0) {
		(void) fprintf(kErrStream, "Cannot open %s: %s.\n", cip->host, FTPStrError(cip->errNo));
		DisposeWinsock();
		exit(5);
	}

	if (gNoMlst != 0) {
		cip->hasMLST = cip->hasMLSD = 0;
	}
	return (result);
}	/* MonkeyOpenHost */



static void
MonkeyCloseHost(const FTPCIPtr cip)
{
	struct timeval t0;
	double dura;

	if (cip->connected == 0)
		return;

	MsgStart("close");
	gettimeofday(&t0, NULL);
	if ((FTPCloseHost(cip)) < 0) {
		dura = FTPDuration(&t0);
		printf("%7.3f sec   Session total: %.1f sec\n", dura,
			FTPDuration(&gSessionStart));
		fflush(stdout);
		fprintf(kErrStream, "Cannot close host.\n");
		DisposeWinsock();
		exit(5);
	}
	dura = FTPDuration(&t0);
	printf("%7.3f sec   (session total: %.1f sec)\n", dura,
		FTPDuration(&gSessionStart));
}	/* MonkeyCloseHost */



static void
MonkeyStallOut(const FTPCIPtr cip)
{
	int c, nr;
	struct timeval t0;
	double dura;

	printf(">>>>> Waiting for remote host to give up:\n");
	fflush(stdout);
	gettimeofday(&t0, NULL);
	errno = 0;
	for (;;) {
		nr = read(cip->ctrlSocketR, &c, 1);
		if (nr == 0) {
			printf(">>>>> Remote host gave up.\n");
			fflush(stdout);
			break;
		} else if ((nr < 0) && (errno != EINTR)) {
#ifdef HAVE_STRERROR
			printf(">>>>> Remote host gave up? (readerr: %s)\n", strerror(errno));
#else
			printf(">>>>> Remote host gave up? (readerr: %d)\n", (errno));
#endif
			fflush(stdout);
			break;
		}
#ifdef HAVE_USLEEP
		usleep(100000);		/* 0.1 sec */
#else
		sleep(1);
#endif
	}
	dura = FTPDuration(&t0);
	printf(">>>>> Error duration: %7.3f sec\n", dura);
}	/* MonkeyStallOut */



static int
MonkeyChdir(const FTPCIPtr cip, const char *const cddir)
{
	int result;
	struct timeval t0;
	double dura;

	MsgStart("cd %s", cddir);
	gettimeofday(&t0, NULL);
	result = FTPChdir(cip, cddir);
	dura = FTPDuration(&t0);
	printf("%7.3f sec\n", dura);

	if ((gErrOutType == kErrOutType_TimeoutAfterChdir) && ((result == 0) || (result == kErrCWDFailed))) {
		printf(">>>>> Intentional Error: keeping connection open, but inactive.\n");
		fflush(stdout);
		MonkeyStallOut(cip);
	}

	return (result);
}	/* MonkeyChdir */



static int
FTPReadOneFile(const FTPCIPtr cip, const char *file)
{
	char *buf;
	size_t bufSize;
	int tmpResult, result;
	int nread;
	longest_int erroutpos = (longest_int) (-1);

	if ((gErrOutType == kErrOutType_TimeoutDataTransfer) || (gErrOutType == kErrOutType_CloseDataTransfer) || (gErrOutType == kErrOutType_ShutdownDataTransfer)) {
		if (Ri(2)) {
			erroutpos = 0;
		} else if (Ri(2)) {
			erroutpos = 1;
		} else if (Ri(2)) {
			erroutpos = cip->bufSize + 1;
		} else {
			erroutpos = cip->bufSize * 20;
		}
		if ((gErrOutType == kErrOutType_TimeoutDataTransfer) && (erroutpos < (129 * 1024)))
			erroutpos = 129 * 1024;
	}

	if (cip == NULL)
		return (kErrBadParameter);
	if (strcmp(cip->magic, kLibraryMagic))
		return (kErrBadMagic);
	
	if (file == NULL)
		return (kErrBadParameter);
	if (file[0] == '\0')
		return (kErrBadParameter);

	result = kNoErr;
	FTPInitIOTimer(cip);
	FTPStartIOTimer(cip);
	tmpResult = FTPStartDataCmd(cip, kNetReading, kTypeBinary, (longest_int) 0, "RETR %s", file);
	if (tmpResult < 0) {
		result = tmpResult;
		if (result == kErrGeneric)
			result = kErrRETRFailed;
		if ((cip->errNo == kErrAcceptDataSocket) || (cip->errNo == kErrNewStreamSocket))
			MonkeyCloseHost(cip);
		cip->errNo = result;
		return (result);
	}

	buf = cip->buf;
	bufSize = cip->bufSize;
	for ( ; ; ) {
		if ((erroutpos != (longest_int) -1) && (cip->bytesTransferred >= erroutpos)) {
			if (gErrOutType == kErrOutType_TimeoutDataTransfer) {
				printf("\n>>>>> Intentional Error: keeping data connection open, but not reading from it.\n");
				printf(">>>>> " PRINTF_LONG_LONG " bytes had already been transferred.\n", cip->bytesTransferred);
				fflush(stdout);
				MonkeyStallOut(cip);
				break;	/* otherwise _we_ will errout */
			} else if (gErrOutType == kErrOutType_CloseDataTransfer) {
				printf("\n>>>>> Intentional Error: closing data connection prematurely.\n");
				printf(">>>>> " PRINTF_LONG_LONG " bytes had already been transferred.\n", cip->bytesTransferred);
				fflush(stdout);
				sleep(2);
				close(cip->dataSocket);
				erroutpos = (longest_int) -1;
				break;	/* otherwise _we_ will errout */
			} else if (gErrOutType == kErrOutType_ShutdownDataTransfer) {
				printf("\n>>>>> Intentional Error: closing data connection prematurely.\n");
				printf(">>>>> " PRINTF_LONG_LONG " bytes had already been transferred.\n", cip->bytesTransferred);
				fflush(stdout);
				sleep(2);
				shutdown(cip->dataSocket, 2);
				erroutpos = (longest_int) -1;
				/* continue */
			}
		}

#ifdef NO_SIGNALS
		nread = SRead(cip->dataSocket, buf, bufSize, (int) cip->xferTimeout, kFullBufferNotRequired|kNoFirstSelect);
		if (nread == kTimeoutErr) {
			cip->errNo = result = kErrDataTimedOut;
			/* Error(cip, kDontPerror, "Remote write timed out.\n"); */
			break;
		} else if (nread < 0) {
			if (errno == EINTR)
				continue;
			/* Error(cip, kDoPerror, "Remote read failed.\n"); */
			result = kErrSocketReadFailed;
			cip->errNo = kErrSocketReadFailed;
			break;
		} else if (nread == 0) {
			break;
		}
#else
		nread = read(cip->dataSocket, buf, bufSize);
		if (nread < 0) {
			/* Error(cip, kDoPerror, "Remote read failed.\n"); */
			result = kErrSocketReadFailed;
			cip->errNo = result;
			break;
		} else if (nread == 0) {
			break;
		}
#endif	/* NO_SIGNALS */
		cip->bytesTransferred += (long) nread;
	}

	tmpResult = FTPEndDataCmd(cip, 1);
	if ((tmpResult < 0) && (result == 0)) {
		result = kErrRETRFailed;
		if ((cip->errNo == kErrAcceptDataSocket) || (cip->errNo == kErrNewStreamSocket))
			MonkeyCloseHost(cip);
		cip->errNo = result;
	}

	FTPStopIOTimer(cip);
	return (result);
}	/* FTPReadOneFile */



static int
FileLIST(const FTPCIPtr cip, FTPLineListPtr lines, const char *lsflags)
{
	char secondaryBuf[512];
	char line[128];
	char *tok;
	int ftype;
	char fname[128];
#ifndef NO_SIGNALS
	char *secBufPtr, *secBufLimit;
	int nread;
	int result;
#else	/* NO_SIGNALS */
	SReadlineInfo lsSrl;
	int result;
#endif	/* NO_SIGNALS */

	InitLineList(lines);

	FTPInitIOTimer(cip);
	FTPStartIOTimer(cip);
	result = FTPStartDataCmd(cip, kNetReading, kTypeAscii, (longest_int) 0, "%s%s", "LIST", lsflags);

#ifdef NO_SIGNALS

	if (result == 0) {
		if (InitSReadlineInfo(&lsSrl, cip->dataSocket, secondaryBuf, sizeof(secondaryBuf), (int) cip->xferTimeout, 1) < 0) {
			/* Not really fdopen, but close in what we're trying to do. */
			result = kErrFdopenR;
			cip->errNo = kErrFdopenR;
			/* Error(cip, kDoPerror, "Could not fdopen.\n"); */
			return (result);
		}
		
		for (;;) {
skip:
			result = SReadline(&lsSrl, line, sizeof(line) - 2);
			if (result == kTimeoutErr) {
				/* timeout */
				/* Error(cip, kDontPerror, "Could not directory listing data -- timed out.\n"); */
				cip->errNo = kErrDataTimedOut;
				return (cip->errNo);
			} else if (result == 0) {
				/* end of listing -- done */
				cip->numListings++;
				break;
			} else if (result < 0) {
				/* error */
				/* Error(cip, kDoPerror, "Could not read directory listing data"); */
				result = kErrLISTFailed;
				cip->errNo = kErrLISTFailed;
				break;
			} else if (strstr(line, "ermission denied") != NULL) {
				result = kErrLISTFailed;
				cip->errNo = kErrLISTFailed;
				break;
			}

			if (line[result - 1] == '\n') {
				line[result - 1] = '\0';
			}

			cip->bytesTransferred += (long) result;
			ftype = line[0];
			if ((ftype == '-') || (ftype == 'd')) {
				tok = line + strlen(line);
				while (*--tok != ' ') {
					if (tok < line)
						goto skip;
				}
				fname[0] = (char) ftype;
				fname[1] = (char) ' ';
				fname[2] = '\0';
				STRNCAT(fname, tok + 1);
				AddLine(lines, fname);
			}
		}

		DisposeSReadlineInfo(&lsSrl);
		result = FTPEndDataCmd(cip, 1);
		FTPStopIOTimer(cip);
		if (result < 0) {
			result = kErrLISTFailed;
			cip->errNo = result;
		}
		result = kNoErr;
	} else if (result == kErrGeneric) {
		result = kErrLISTFailed;
		if ((cip->errNo == kErrAcceptDataSocket) || (cip->errNo == kErrNewStreamSocket))
			MonkeyCloseHost(cip);
		cip->errNo = result;
	}

#else	/* NO_SIGNALS */	
	
	if (result == 0) {
		/* This line sets the buffer pointer so that the first thing
		 * BufferGets will do is reset and fill the buffer using
		 * real I/O.
		 */
		secBufPtr = secondaryBuf + sizeof(secondaryBuf);
		secBufLimit = (char *) 0;

		for ( ; ; ) {
skip:
			nread = BufferGets(line, sizeof(line), cip->dataSocket, secondaryBuf, &secBufPtr, &secBufLimit, sizeof(secondaryBuf));
			if (nread <= 0) {
				if (nread < 0)
					break;
			} else {
				cip->bytesTransferred += (long) nread;
				ftype = line[0];
				if ((ftype == '-') || (ftype == 'd')) {
					tok = line + strlen(line);
					while (*--tok != ' ') {
						if (tok < line)
							goto skip;
					}
					fname[0] = ftype;
					fname[1] = ' ';
					fname[2] = '\0';
					STRNCAT(fname, tok + 1);
					AddLine(lines, fname);
				}
			}
		}
		result = FTPEndDataCmd(cip, 1);
		FTPStopIOTimer(cip);
		if (result < 0) {
			result = kErrLISTFailed;
			cip->errNo = result;
		}
		result = kNoErr;
	} else if (result == kErrGeneric) {
		result = kErrLISTFailed;
		if ((cip->errNo == kErrAcceptDataSocket) || (cip->errNo == kErrNewStreamSocket))
			MonkeyCloseHost(cip);
		cip->errNo = result;
	}
#endif	/* NO_SIGNALS */

	return (result);
}	/* FileLIST */



static int
FileNLST(const FTPCIPtr cip, FTPLineListPtr lines, const char *lsflags)
{
	char secondaryBuf[512];
	char line[128];
	char *cp1, *cp2, *tok;
	int ftype;
	char fname[128];
#ifndef NO_SIGNALS
	char *secBufPtr, *secBufLimit;
	int nread;
	int result;
#else	/* NO_SIGNALS */
	SReadlineInfo lsSrl;
	int result;
#endif	/* NO_SIGNALS */

	InitLineList(lines);

	FTPInitIOTimer(cip);
	FTPStartIOTimer(cip);
	result = FTPStartDataCmd(cip, kNetReading, kTypeAscii, (longest_int) 0, "%s%s", "NLST", lsflags);

#ifdef NO_SIGNALS

	if (result == 0) {
		if (InitSReadlineInfo(&lsSrl, cip->dataSocket, secondaryBuf, sizeof(secondaryBuf), (int) cip->xferTimeout, 1) < 0) {
			/* Not really fdopen, but close in what we're trying to do. */
			result = kErrFdopenR;
			cip->errNo = kErrFdopenR;
			/* Error(cip, kDoPerror, "Could not fdopen.\n"); */
			return (result);
		}
		
		for (;;) {
			result = SReadline(&lsSrl, line, sizeof(line) - 2);
			if (result == kTimeoutErr) {
				/* timeout */
				/* Error(cip, kDontPerror, "Could not directory listing data -- timed out.\n"); */
				cip->errNo = kErrDataTimedOut;
				return (cip->errNo);
			} else if (result == 0) {
				/* end of listing -- done */
				cip->numListings++;
				break;
			} else if (result < 0) {
				/* error */
				/* Error(cip, kDoPerror, "Could not read directory listing data"); */
				result = kErrLISTFailed;
				cip->errNo = kErrLISTFailed;
				break;
			} else if (strstr(line, "ermission denied") != NULL) {
				result = kErrLISTFailed;
				cip->errNo = kErrLISTFailed;
				break;
			}

			cip->bytesTransferred += (long) result;
			/* Parse files out of possibly
			 * multicolumn output.
			 */
			for (cp1 = line; ; cp1 = NULL) {
				tok = strtok(cp1, " \r\n\t");
				if (tok == NULL)
					break;
				cp2 = tok + strlen(tok) - 1;
				switch (*cp2) {
				case '*':
				case '@':
					*cp2 = '\0';
					ftype = '-';
					break;
				case '/':
					*cp2 = '\0';
					ftype = 'd';
					break;
				default:
					ftype = '-';
					break;
				}
				fname[0] = (char) ftype;
				fname[1] = (char) ' ';
				fname[2] = '\0';
				STRNCAT(fname, tok);
				AddLine(lines, fname);
			}
		}

		DisposeSReadlineInfo(&lsSrl);
		result = FTPEndDataCmd(cip, 1);
		FTPStopIOTimer(cip);
		if (result < 0) {
			result = kErrLISTFailed;
			cip->errNo = result;
		}
		result = kNoErr;
	} else if (result == kErrGeneric) {
		result = kErrLISTFailed;
		if ((cip->errNo == kErrAcceptDataSocket) || (cip->errNo == kErrNewStreamSocket))
			MonkeyCloseHost(cip);
		cip->errNo = result;
	}

#else	/* NO_SIGNALS */
	
	if (result == 0) {
		/* This line sets the buffer pointer so that the first thing
		 * BufferGets will do is reset and fill the buffer using
		 * real I/O.
		 */
		secBufPtr = secondaryBuf + sizeof(secondaryBuf);
		secBufLimit = (char *) 0;

		for ( ; ; ) {
			nread = BufferGets(line, sizeof(line), cip->dataSocket, secondaryBuf, &secBufPtr, &secBufLimit, sizeof(secondaryBuf));
			if (nread <= 0) {
				if (nread < 0)
					break;
			} else {
				cip->bytesTransferred += (long) nread;
				/* Parse files out of possibly
				 * multicolumn output.
				 */
				for (cp1 = line; ; cp1 = NULL) {
					tok = strtok(cp1, " \r\n\t");
					if (tok == NULL)
						break;
					cp2 = tok + strlen(tok) - 1;
					switch (*cp2) {
						case '*':
						case '@':
							*cp2 = '\0';
							ftype = '-';
							break;
						case '/':
							*cp2 = '\0';
							ftype = 'd';
							break;
						default:
							ftype = '-';
							break;
					}
					fname[0] = ftype;
					fname[1] = ' ';
					fname[2] = '\0';
					STRNCAT(fname, tok);
					AddLine(lines, fname);
				}
			}
		}
		result = FTPEndDataCmd(cip, 1);
		FTPStopIOTimer(cip);
		if (result < 0) {
			result = kErrLISTFailed;
			cip->errNo = result;
		}
		result = kNoErr;
	} else if (result == kErrGeneric) {
		result = kErrLISTFailed;
		if ((cip->errNo == kErrAcceptDataSocket) || (cip->errNo == kErrNewStreamSocket))
			MonkeyCloseHost(cip);
		cip->errNo = result;
	}
#endif	/* NO_SIGNALS */

	return (result);
}	/* FileNLST */



static int
FileList(const FTPCIPtr cip)
{
	int p, rc;
	FTPLinePtr lp;
	struct timeval t0;

	if (gHaveDir == 1) {
		DisposeLineListContents(&gDir);
		gHaveDir = 0;
	}
	p = Ri(3);
	if (p == 0) {
		MsgStart("ls -F");
		gettimeofday(&t0, NULL);
		rc = FileNLST(cip, &gDir, " -F");
	} else if (p == 1) {
		MsgStart("dir");
		gettimeofday(&t0, NULL);
		rc = FileLIST(cip, &gDir, "");
	} else {
		MsgStart("ls -CF");
		gettimeofday(&t0, NULL);
		rc = FileNLST(cip, &gDir, " -CF");
	}
	if (rc >= 0) {
		gHaveDir = 1;
		printf("%7.3f sec   " PRINTF_LONG_LONG " bytes, %.1f k/s\n", cip->sec, cip->bytesTransferred, cip->kBytesPerSec);
		if (gPrintLists != 0) {
			for (lp=gDir.first; lp != NULL; lp=lp->next)
				printf("    %s\n", lp->line);
		}
	}
	return (rc);
}	/* FileList */




static char *
RandomFile(void)
{
	FTPLinePtr lp;
	int nfiles, pick, i;

	if (gHaveDir == 0)
		return NULL;

	for (lp=gDir.first, nfiles=0; lp != NULL; lp=lp->next)
		if (lp->line[0] == '-')
			nfiles++;
	if (nfiles == 0)
		return NULL;

	pick = Ri(nfiles);
	for (lp=gDir.first, i=0; lp != NULL; lp=lp->next) {
		if (lp->line[0] == '-') {
			if (pick == i)
				return (lp->line + 2);
			i++;
		}
	}
	return NULL;
}	/* RandomFile */




static char *
RandomDir(void)
{
	FTPLinePtr lp;
	int ndirs, pick, i;

	if (gHaveDir == 0)
		return NULL;

	for (lp=gDir.first, ndirs=0; lp != NULL; lp=lp->next)
		if (lp->line[0] == 'd')
			ndirs++;
	if (ndirs == 0)
		return NULL;

	pick = Ri(ndirs);
	for (lp=gDir.first, i=0; lp != NULL; lp=lp->next) {
		if (lp->line[0] == 'd') {
			if (pick == i)
				return (lp->line + 2);
			i++;
		}
	}
	return NULL;
}	/* RandomDir */



static void
Shell(const FTPCIPtr cip)
{
	int i, maxw, needlist = 1;
	int p;
	char *randdir, *randfile;
	char s1[128];
	char errstr[128];
	struct timeval t0;
	double dura;
	time_t now;
	int result;

	for (i=1; i<c_max; i++) {
		gWeights[i] += gWeights[i-1];
	}
	maxw = gWeights[i-1];
	for (gLoops = 0; ((gMaxLoops < 1) || (gLoops < gMaxLoops)); gLoops++) {
		time(&now);
		if ((gAlarmClock != 0) && (now >= gAlarmClock)) {
			printf("%-5d time up!\n", gLoops);
			return;
		}
		p = Ri(maxw);
		for (i=0; i<c_max; i++) {
			if (p < gWeights[i])
				break;
		}

		/* Lost connection? */
		if (cip->connected == 0)
			i = c_quit;

		switch (i) {
			case c_quit:
				if (gQuitMode == kYesCloseYesQuit) {
					printf("%-5d quit\n", gLoops);
					return;
				} else if (gQuitMode == kYesCloseNoQuit) {
					/* Close, then re-open */
					MonkeyCloseHost(cip);
					sleep((unsigned int) Ri(3) + 3);
					MonkeyOpenHost(cip);
					needlist = 1;
				} /* else kNoCloseNoQuit */
				break;
			case c_cd:
				if (Rp(20)) {
					(void) MonkeyChdir(cip, "..");
					needlist = 1;
				} else if (Rp(20)) {
					(void) MonkeyChdir(cip, gMainDir);
					needlist = 1;
				} else {
cd:
					if (needlist) {
						if (FileList(cip) >= 0)
							needlist = 0;
						++gLoops;
					}
					if ((needlist == 0) && ((randdir = RandomDir()) != NULL)) {
						if (MonkeyChdir(cip, randdir) == 0)
							needlist = 1;
					} else {
						(void) MonkeyChdir(cip, gMainDir);
						needlist = 1;
					}
				}
				break;
			case c_pwd:
				MsgStart("pwd");
				gettimeofday(&t0, NULL);
				(void) FTPGetCWD(cip, s1, sizeof(s1));	/* pwd */
				dura = FTPDuration(&t0);
				printf("%7.3f sec\n", dura);
				break;
			case c_dir:
				if (FileList(cip) >= 0)
					needlist = 0;
				break;
			case c_get:
				if (needlist) {
					if (FileList(cip) >= 0)
						needlist = 0;
					++gLoops;
				}
				if (needlist == 0) {
					randfile = RandomFile();
					if (randfile == NULL)
						goto cd;
					/* pick a random file to get */
					MsgStart("get %s", randfile);
					gettimeofday(&t0, NULL);
					result = FTPReadOneFile(cip, randfile);
					dura = FTPDuration(&t0);
					if (result == kNoErr) {
						printf("%7.3f sec   " PRINTF_LONG_LONG " bytes, %.1f k/s\n", cip->sec, cip->bytesTransferred, cip->kBytesPerSec);
					} else {
						printf("%7.3f sec   %s\n", dura, FTPStrError2(cip, result, errstr, sizeof(errstr), kErrCouldNotStartDataTransfer));
					}
				}
				break;
		}
		sleep((unsigned int) Ri(4));
	}
}	/* Shell */



main_void_return_t
main(int argc, char **argv)
{
	int result, c;
	int seed;
	time_t now;
	struct tm *ltp;
	char dstr[64];
	char *password;
	GetoptInfo opt;

	InitWinsock();

	result = FTPInitLibrary(&li);
	if (result < 0) {
		fprintf(kErrStream, "Init library error %d.\n", result);
		DisposeWinsock();
		exit(3);
	}
	result = FTPInitConnectionInfo(&li, &fi, kDefaultFTPBufSize);
	if (result < 0) {
		fprintf(kErrStream, "Init connection info error %d.\n", result);
		DisposeWinsock();
		exit(4);
	}

	fi.debugLog = NULL;
	fi.errLog = kErrStream;
	STRNCPY(fi.user, "anonymous");
	STRNCPY(fi.pass, "monkey@bowser.nintendo.co.jp");
	seed = -1;

	GetoptReset(&opt);
	while ((c = Getopt(&opt, argc, argv, "a:QRSCD:m:ls:d:e:P:p:u:")) > 0) switch(c) {
		case 'a':
			time(&gAlarmClock);
			gAlarmClock += (time_t) atoi(opt.arg);
			break;
		case 'm':
			gMaxLoops = atoi(opt.arg);
			break;
		case 's':
			seed = atoi(opt.arg);
			break;
		case 'C':
			gCLNT = 1;
			break;
		case 'D':
			gMainDir = opt.arg;
			break;
		case 'l':
			gPrintLists = !gPrintLists;
			break;
		case 'Q':
			gQuitMode = kNoCloseNoQuit;
			break;
		case 'R':
			gQuitMode = kYesCloseNoQuit;
			break;
		case 'S':
			gQuitMode = kYesCloseYesQuit;
			break;
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
			if (strchr(opt.arg, ',') == NULL)
				gErrOutOnPurpose = atoi(opt.arg);
			else
				(void) sscanf(opt.arg, "%d,%d", &gErrOutOnPurpose, &gErrOutTypeToUse);
			break;
		case 'd':
			fi.debugLog = fopen(opt.arg, "a");
			break;
		default:
			Usage();
	}
	if (opt.ind > argc - 1)
		Usage();

	if (strcmp(fi.user, "anonymous") && strcmp(fi.user, "ftp")) {
		if (fi.pass[0] == '\0') {
			password = GetPass2("Password: ");		
			if (password != NULL) {
				STRNCPY(fi.pass, password);
				/* Don't leave cleartext password in memory. */
				memset(password, 0, strlen(fi.pass));
			}
		}
	}

	/* The library destroys it each time. */
	STRNCPY(gPasswd, fi.pass);

	if (seed < 0) {
#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
		seed = ((unsigned int) time(NULL)) & 0xffff;
#else
		seed = (time(NULL) & 0xff) << 8;
		seed |= (getpid() & 0xff);
#endif
	}
	srand((unsigned int) seed);
	gSeed = seed;

	STRNCPY(fi.host, argv[opt.ind]);
	
	gettimeofday(&gMonkeyStart, NULL);
	MonkeyOpenHost(&fi);
	Shell(&fi);
	MonkeyCloseHost(&fi);

	time(&now);
	ltp = localtime(&now);
	strftime(dstr, sizeof(dstr), "%Y-%m-%d %H:%M:%S", ltp);
	printf("\n-- Finished at %s (program runtime = %.1f sec) --\n", dstr,
		FTPDuration(&gMonkeyStart));
	
	DisposeWinsock();
	exit(0);
}	/* main */
