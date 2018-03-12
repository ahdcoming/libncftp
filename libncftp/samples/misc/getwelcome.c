/* getwelcome: print the welcome message and exit. */

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include <ncftp.h>				/* Library header. */
#include <Strn.h>				/* Library header. */

static void
Usage(void)
{
	fprintf(stderr, "Usage:  getwelcome <host>\n");
	fprintf(stderr, "\nLibrary version: %s.\n", gLibNcFTPVersion + 5);
#ifdef UNAME
	fprintf(stderr, "System: %s.\n", UNAME);
#endif
	exit(2);
}	/* Usage */



main_void_return_t
main(int argc, char **argv)
{
	int result;
	FTPLibraryInfo li;
	FTPConnectionInfo fi;

	if (argc < 2)
		Usage();

	InitWinsock();

	result = FTPInitLibrary(&li);
	if (result < 0) {
		fprintf(stderr, "getwelcome: init library error %d (%s).\n", result, FTPStrError(result));
		DisposeWinsock();
		exit(3);
	}

	result = FTPInitConnectionInfo(&li, &fi, kDefaultFTPBufSize);
	if (result < 0) {
		fprintf(stderr, "getwelcome: init connection info error %d (%s).\n", result, FTPStrError(result));
		DisposeWinsock();
		exit(4);
	}

	fi.debugLog = stdout;
	fi.errLog = stderr;
	STRNCPY(fi.user, "anonymous");
	fi.connTimeout = (unsigned int) 20;
	STRNCPY(fi.host, argv[1]);

	if ((result = FTPOpenHost(&fi)) < 0) {
		fprintf(stderr, "getwelcome: cannot open %s: %s.\n", fi.host, FTPStrError(result));
		DisposeWinsock();
		exit(5);
	}

	FTPCloseHost(&fi);
	DisposeWinsock();

	exit(0);
}	/* main */
