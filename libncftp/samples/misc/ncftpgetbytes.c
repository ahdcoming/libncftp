/* ncftpgetbytes.c */

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include <ncftp.h>				/* Library header. */
#include <Strn.h>				/* Library header. */
#include <sio.h>				/* Library header. */

#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
#	define _CRT_SECURE_NO_WARNINGS 1
#	define MY_FD_ZERO FD_ZERO
#	define MY_FD_SET(s,set) FD_SET((SOCKET) (s), set)
#	define MY_FD_CLR(s,set) FD_CLR((SOCKET) (s), set)
#	define MY_FD_ISSET FD_ISSET
#else
#	define MY_FD_ZERO FD_ZERO
#	define MY_FD_SET FD_SET
#	define MY_FD_CLR FD_CLR
#	define MY_FD_ISSET FD_ISSET
#endif

#define VERSION "1.0.0 (2000-04-06)"

#define kErrNoREST (-2000)
#define kErrPrematureEOF (-2001)

static int
FTPGetBytes(
	const FTPCIPtr cip,
	const char *const file,
	int xtype,
	const int fdtouse,
	const longest_int startPoint,
	const longest_int maxBytes
	);

static void
Usage(void)
{
	FILE *fp;

	fp = stdout;
	(void) fprintf(fp, "NcFTPGetBytes %s.\n\n", VERSION);
	(void) fprintf(fp, "Usage:\n");
	(void) fprintf(fp, "  ncftpgetbytes [flags] FTP-URL\n");
	(void) fprintf(fp, "\nFlags:\n\
  -u XX  Use username XX instead of anonymous.\n\
  -p XX  Use password XX with the username.\n\
  -P XX  Use port number XX instead of the default FTP service port (21).\n\
  -d XX  Use the file XX for debug logging.\n\
  -F     Use passive (PASV) data connections.\n\
  -o XX  Start at offset XX in remote data file.\n\
  -n XX  Retrieve a maximum of XX bytes from remote data file.\n");
	(void) fprintf(fp, "\nExamples:\n\
  ncftpgetbytes ftp://ftp.wustl.edu/pub/README | /usr/bin/more\n\
  ncftpgetbytes -F -o 100 -n 10000 ftp://ftp.wustl.edu/pub/README > file.dat\n");

	(void) fprintf(fp, "\nLibrary version: %s.\n", gLibNcFTPVersion + 5);
	(void) fprintf(fp, "\nThis is a freeware program by Mike Gleason (http://www.NcFTP.com/contact/).\n");
	(void) fprintf(fp, "This was built using LibNcFTP (http://www.ncftp.com/libncftp).\n");

	DisposeWinsock();
	exit(10);
}	/* Usage */




static longest_int
a_to_ll(const char *const str)
{
	longest_int ll;

#if defined(HAVE_LONG_LONG) && defined(SCANF_LONG_LONG)
	ll = (longest_int) 0;
	(void) sscanf(str, SCANF_LONG_LONG, &ll);
#elif defined(HAVE_LONG_LONG) && defined(HAVE_STRTOQ)
	ll = (longest_int) strtoq(str, NULL, 0);
#else
	ll = (longest_int) 0;
	(void) sscanf(str, "%ld", &ll);
#endif
	return (ll);
}	/* a_to_ll */



main_void_return_t
main(int argc, char **argv)
{
	int result;
	int c;
	int es = 0;
	int rc;
	FTPLibraryInfo li;
	FTPConnectionInfo ci;
	char url[256];
	char urlfile[128];
	int urlxtype;
	FTPLineList cdlist;
	FTPLinePtr lp;
	longest_int startPoint = (longest_int) 0;
	longest_int maxBytes = (longest_int) 0;
	GetoptInfo opt;

	InitWinsock();
	result = FTPInitLibrary(&li);
	if (result < 0) {
		fprintf(stderr, "ncftpgetbytes: init library error %d (%s).\n", result, FTPStrError(result));
		exit(9);
	}

	result = FTPInitConnectionInfo(&li, &ci, kDefaultFTPBufSize);
	if (result < 0) {
		fprintf(stderr, "ncftpgetbytes: init connection info error %d (%s).\n", result, FTPStrError(result));
		exit(8);
	}

	GetoptReset(&opt);
	while ((c = Getopt(&opt, argc, argv, "P:u:p:d:Fo:n:")) > 0) switch(c) {
		case 'P':
			ci.port = atoi(opt.arg);	
			break;
		case 'u':
			(void) STRNCPY(ci.user, opt.arg);
			break;
		case 'p':
			(void) STRNCPY(ci.pass, opt.arg);	/* Don't recommend doing this! */
			break;
		case 'd':
			if (opt.arg[0] == '-')
				ci.debugLog = stderr;
			else if (strcmp(opt.arg, "stderr") == 0)
				ci.debugLog = stderr;
			else
				ci.debugLog = fopen(opt.arg, "a");
			break;
		case 'F':
			if (ci.dataPortMode == kPassiveMode)
				ci.dataPortMode = kSendPortMode;
			else
				ci.dataPortMode = kPassiveMode;
			break;
		case 'o':
			startPoint = a_to_ll(opt.arg);
			break;
		case 'n':
			maxBytes = a_to_ll(opt.arg);
			break;
		default:
			Usage();
	}
	if (opt.ind > argc - 1)
		Usage();

	(void) STRNCPY(url, argv[opt.ind]);
	rc = FTPDecodeURL(&ci, url, &cdlist, urlfile, sizeof(urlfile), (int *) &urlxtype, NULL);
	if ((rc == kMalformedURL) || (rc == kNotURL)) {
		(void) fprintf(stderr, "Malformed URL: %s\n", url);
		DisposeWinsock();
		exit(7);
	} else {
		/* URL okay */
		if (urlfile[0] == '\0') {
			/* It was a directory! */
			(void) fprintf(stderr, "%s is a directory URL, not a file URL.\n", url);
			DisposeWinsock();
			exit(6);
		}
	}

	if ((result = FTPOpenHost(&ci)) < 0) {
		FTPPerror(&ci, result, 0, "Could not open", ci.host);
		es = 5;
	} else {
		/* At this point, we should close the host when
		 * were are finished.
		 */

		for (lp = cdlist.first; lp != NULL; lp = lp->next) {
			if (FTPChdir(&ci, lp->line) != 0) {
				(void) fprintf(stderr, "ncftpgetbytes: cannot chdir to %s: %s.\n", lp->line, FTPStrError(ci.errNo));
				es = 4;
				goto close;
			}
		}

		result = FTPGetBytes(
				&ci,
				urlfile,
				kTypeBinary,
				1,
				startPoint,
				maxBytes
			);
		if (result == kErrNoREST) {
			fprintf(stderr, "This server does not support the REST command. \nTo specify a start offset, REST is required to be supported.\n");
			es = 3;
		} else if (result == kErrPrematureEOF) {
			fprintf(stderr, "Unexpected EOF\n");
			es = 2;
		} else if (result < 0) {
			FTPPerror(&ci, result, kErrCouldNotStartDataTransfer, "Could not get", urlfile);
			es = 1;
		} else {
			es = 0;
		}
close:
		FTPCloseHost(&ci);
	}

	DisposeWinsock();
	exit(es);
}	/* main */




/* The purpose of this is to provide updates for the progress meters
 * during lags.  Return zero if the operation timed-out.
 */
static int
WaitForRemoteInput(const FTPCIPtr cip)
{
	fd_set ss, ss2;
	struct timeval tv;
	int result;
	int fd;
	int wsecs;
	int xferTimeout;
	int ocancelXfer;

	xferTimeout = cip->xferTimeout;
	if (xferTimeout < 1)
		return (1);

	fd = cip->dataSocket;
	if (fd < 0)
		return (1);

	ocancelXfer = cip->cancelXfer;
	wsecs = 0;
	cip->stalled = 0;

	while ((xferTimeout <= 0) || (wsecs < xferTimeout)) {
		if ((cip->cancelXfer != 0) && (ocancelXfer == 0)) {
			/* leave cip->stalled -- could have been stalled and then canceled. */
			return (1);
		}
		MY_FD_ZERO(&ss);
#if defined(__DECC) || defined(__DECCXX)
#pragma message save
#pragma message disable trunclongint
#endif
		MY_FD_SET(fd, &ss);
#if defined(__DECC) || defined(__DECCXX)
#pragma message restore
#endif
		ss2 = ss;
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		result = select(fd + 1, &ss, NULL, &ss2, &tv);
		if (result == 1) {
			/* ready */
			cip->stalled = 0;
			return (1);
		} else if (result < 0) {
			if (result != EINTR) {
				perror("select");
				cip->stalled = 0;
				return (1);
			}
		} else {
			wsecs++;
			cip->stalled = wsecs;
		}
		FTPUpdateIOTimer(cip);
	}

	cip->dataTimedOut = 1;
	return (0);	/* timed-out */
}	/* WaitForRemoteInput */




static int
FTPGetBytes(
	const FTPCIPtr cip,
	const char *const file,
	int xtype,
	const int fdtouse,
	const longest_int startPoint,
	const longest_int maxBytes
	)
{
	char *buf;
	size_t bufSize;
	int tmpResult;
	int result;
	read_return_t nread;
	write_return_t nwrote;
	size_t ntoread;
	longest_int totalRead;
	int fd = fdtouse;

	if (cip->buf == NULL) {
		cip->errNo = kErrNoBuf;
		return (cip->errNo);
	}

	result = kNoErr;
	cip->usingTAR = 0;

	if ((startPoint != (longest_int) 0) && (cip->hasREST == kCommandAvailabilityUnknown)) {
		(void) FTPSetTransferType(cip, kTypeBinary);
		if (FTPSetStartOffset(cip, (longest_int) 1) == kNoErr) {
			/* Now revert -- we still may not end up
			 * doing it.
			 */
			FTPSetStartOffset(cip, (longest_int) -1);
		} else {
			return kErrNoREST;
		}
	}

	tmpResult = FTPStartDataCmd(cip, kNetReading, xtype, startPoint, "RETR %s", file);

	if (tmpResult < 0) {
		result = tmpResult;
		if (result == kErrGeneric)
			result = kErrRETRFailed;
		cip->errNo = result;
		return (result);
	}

	FTPInitIOTimer(cip);
	buf = cip->buf;
	bufSize = cip->bufSize;

	cip->useProgressMeter = 0;
	result = kNoErr;
	FTPStartIOTimer(cip);

	/* Binary */
	for (totalRead = 0; ((maxBytes == (longest_int) 0) || (totalRead < maxBytes)); totalRead += nread) {
		if (! WaitForRemoteInput(cip)) {	/* could set cancelXfer */
			cip->errNo = result = kErrDataTimedOut;
			/* Error(cip, kDontPerror, "Remote read timed out.\n"); */
			break;
		}
		if (cip->cancelXfer > 0) {
			FTPAbortDataTransfer(cip);
			result = cip->errNo = kErrDataTransferAborted;
			break;
		}
		if (maxBytes == (longest_int) 0) {
			ntoread = bufSize;
			nread = (read_return_t) SRead(cip->dataSocket, buf, ntoread, (int) cip->xferTimeout, kFullBufferNotRequired|kNoFirstSelect);
		} else {
			ntoread = (size_t) (maxBytes - totalRead);
			if (ntoread > bufSize)
				ntoread = bufSize;
			nread = (read_return_t) SRead(cip->dataSocket, buf, ntoread, (int) cip->xferTimeout, kFullBufferNotRequired|kNoFirstSelect);
		}

		if (nread == kTimeoutErr) {
			cip->errNo = result = kErrDataTimedOut;
			/* Error(cip, kDontPerror, "Remote read timed out.\n"); */
			break;
		} else if (nread < 0) {
#if 0
			if (/*(gGotBrokenData != 0) ||*/ (errno == EPIPE)) {
#else
			if (errno == EPIPE) {
#endif
				result = cip->errNo = kErrSocketReadFailed;
				errno = EPIPE;
				/* Error(cip, kDoPerror, "Lost data connection to remote host.\n"); */
			} else if (errno == EINTR) {
				continue;
			} else {
				/* Error(cip, kDoPerror, "Remote read failed.\n"); */
				result = kErrSocketReadFailed;
				cip->errNo = kErrSocketReadFailed;
			}
			break;
		} else if (nread == 0) {
			if (maxBytes != (longest_int) 0)
				result = kErrPrematureEOF;
			break;
		}

		nwrote = write(fd, buf, (write_size_t) nread);
		if (nwrote != nread) {
#if 0
			if (/*(gGotBrokenData != 0) ||*/ (errno == EPIPE)) {
#else
			if (errno == EPIPE) {
#endif
				result = kErrWriteFailed;
				cip->errNo = kErrWriteFailed;
				errno = EPIPE;
			} else {
				/* Error(cip, kDoPerror, "Local write failed.\n"); */
				result = kErrWriteFailed;
				cip->errNo = kErrWriteFailed;
			}
			(void) shutdown(cip->dataSocket, 2);
			break;
		}
		cip->bytesTransferred += (longest_int) nread;
		FTPUpdateIOTimer(cip);
	}

	/* If there hasn't been an error, and you limited
	 * the number of bytes, we need to abort the
	 * remaining data.
	 */
	if ((result == kNoErr) && (maxBytes != (longest_int) 0)) {
		FTPAbortDataTransfer(cip);
		tmpResult = FTPEndDataCmd(cip, 1);
		if ((tmpResult < 0) && (result == 0) && (tmpResult != kErrDataTransferFailed)) {
			result = kErrRETRFailed;
			cip->errNo = kErrRETRFailed;
		}
	} else {
		tmpResult = FTPEndDataCmd(cip, 1);
		if ((tmpResult < 0) && (result == 0)) {
			result = kErrRETRFailed;
			cip->errNo = kErrRETRFailed;
		}
	}
	FTPStopIOTimer(cip);

	return (result);
}	/* FTPGetBytes */
