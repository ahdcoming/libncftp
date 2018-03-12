/* ncftpgettomem.c */

#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
#	define _CRT_SECURE_NO_WARNINGS 1
#endif

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include <ncftp.h>				/* Library header. */
#include <Strn.h>				/* Library header. */
#include <sio.h>				/* Library header. */

static void
Usage(void)
{
	FILE *fp;

	fp = stdout;
	(void) fprintf(fp, "Usage:\n");
	(void) fprintf(fp, "  ncftpgettomem [flags] FTP-URL > stdout\n");
	(void) fprintf(fp, "\nFlags:\n\
  -u XX  Use username XX instead of anonymous.\n\
  -p XX  Use password XX with the username.\n\
  -P XX  Use port number XX instead of the default FTP service port (21).\n\
  -d XX  Use the file XX for debug logging.\n\
  -F     Use passive (PASV) data connections.\n\
  -o XX  Start at offset XX in remote data file.\n\
  -n XX  Retrieve a maximum of XX bytes from remote data file.\n");
	(void) fprintf(fp, "\nExamples:\n\
  ncftpgettomem ftp://ftp.freebsd.org/pub/FreeBSD/README.TXT | /usr/bin/more\n\
  ncftpgettomem -F -o 100 -n 3000 ftp://ftp.freebsd.org/pub/FreeBSD/README.TXT > file.dat\n");

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
	unsigned int maxBytesU;
	size_t maxBytes = 0;
	GetoptInfo opt;
	longest_int expectedSize;
	char *memBuf;
	size_t maxNumBytesToGet;
	size_t totalWrote;

	InitWinsock();
	result = FTPInitLibrary(&li);
	if (result < 0) {
		fprintf(stderr, "ncftpgettomem: init library error %d (%s).\n", result, FTPStrError(result));
		exit(9);
	}

	result = FTPInitConnectionInfo(&li, &ci, kDefaultFTPBufSize);
	if (result < 0) {
		fprintf(stderr, "ncftpgettomem: init connection info error %d (%s).\n", result, FTPStrError(result));
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
			maxBytesU = 0;
			(void) sscanf(opt.arg, "%u", &maxBytesU);
			maxBytes = (size_t) maxBytesU;
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
				(void) fprintf(stderr, "ncftpgettomem: cannot chdir to %s: %s.\n", lp->line, FTPStrError(ci.errNo));
				es = 4;
				goto close;
			}
		}

		if (FTPFileSize(&ci, urlfile, &expectedSize, kTypeBinary) == 0) {
			maxNumBytesToGet = (size_t) expectedSize;
		} else {
			maxNumBytesToGet = 100;
		}
		if ((maxBytes != 0) && (maxBytes < maxNumBytesToGet))
			maxNumBytesToGet = maxBytes;
		memBuf = calloc(1, maxNumBytesToGet + 1);
		if (memBuf == NULL) {
			(void) fprintf(stderr, "ncftpgettomem: memory allocation of %lu bytes failed: %s\n", (unsigned long) maxNumBytesToGet + 1, strerror(errno));
			es = 2;
			goto close;
		}

		PrintF(&ci, "ncftpgettomem debug: maxNumBytesToGet = %lu\n", (unsigned long) maxNumBytesToGet);
		PrintF(&ci, "ncftpgettomem debug: startPoint = " PRINTF_LONG_LONG "\n", startPoint);

		result = FTPGetFileToMemory(
				&ci,
				urlfile,
				memBuf,
				maxNumBytesToGet,
				&totalWrote,
				startPoint,
				0
			);
		if (result == kErrRESTNotAvailable) {
			fprintf(stderr, "This server does not support the REST command. \nTo specify a start offset, the server needs to implement the REST command.\n");
			es = 3;
		} else if (result < 0) {
			FTPPerror(&ci, result, kErrCouldNotStartDataTransfer, "Could not get", urlfile);
			es = 1;
		} else {
			es = 0;

			PrintF(&ci, "ncftpgettomem debug: totalWrote = %lu\n", (unsigned long) totalWrote);
			/* The buffer has been loaded with "totalWrote" bytes,
			 * which may be less than "maxNumBytesToGet" bytes.
			 *
			 * Do something with the buffer here.
			 *
			 * For this example, we just dump it unmodified
			 * to stdout.
			 */
			(void) write(1, memBuf, totalWrote);
			free(memBuf);
		}
close:
		FTPCloseHost(&ci);
	}

	DisposeWinsock();
	exit(es);
}	/* main */
