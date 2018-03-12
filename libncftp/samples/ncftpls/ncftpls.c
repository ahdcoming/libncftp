/* ncftpls.c
 *
 * A non-interactive utility to list directories on a remote FTP server.
 * Very useful in shell scripts!
 *
 * NOTE:  This is for demonstration only -- an up-to-date version of this
 * program comes with the NcFTP Client 3.0 distribution.  This version
 * will most likely have a subset of features and fixes.
 */

#define VERSION "1.6.0"

#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
#	define _CRT_SECURE_NO_WARNINGS 1
#endif

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include <ncftp.h>				/* Library header. */
#include <Strn.h>				/* Library header. */

#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
#	include "..\ncftpget\gpshare.h"
#else
#	include "../ncftpget/gpshare.h"
#endif

FTPLibraryInfo li;
FTPConnectionInfo fi;

extern int gFirewallType;
extern char gFirewallHost[64];
extern char gFirewallUser[32];
extern char gFirewallPass[32];
extern unsigned int gFirewallPort;

static void
Usage(void)
{
	FILE *fp;

	fp = OpenPager();
	(void) fprintf(fp, "NcFTPLs %s.\n\n", VERSION);
	(void) fprintf(fp, "*** NOTE: This version is for demonstration only -- get the latest and most\nfeature complete version from the NcFTP Client package. ***\n\n");
	(void) fprintf(fp, "Usages:\n");
	(void) fprintf(fp, "  ncftpls [FTP flags] [-x \"ls flags\"] ftp://url.style.host/path/name/\n");
	(void) fprintf(fp, "\nls Flags:\n\
  -1     Most basic format, one item per line.\n\
  -l     Long list format.\n\
  -x XX  Other flags to pass on to the remote server.\n");
	(void) fprintf(fp, "\nFTP Flags:\n\
  -u XX  Use username XX instead of anonymous.\n\
  -p XX  Use password XX with the username.\n\
  -P XX  Use port number XX instead of the default FTP service port (21).\n\
  -d XX  Use the file XX for debug logging.\n");
	(void) fprintf(fp, "\
  -t XX  Timeout after XX seconds.\n\
  -f XX  Read the file XX for user and password information.\n\
  -F     Use passive (PASV) data connections.\n\
  -r XX  Redial XX times until connected.\n");
	(void) fprintf(fp, "\nExamples:\n\
  ncftpls ftp://ftp.wustl.edu/pub/\n\
  ncftpls -1 ftp://ftp.wustl.edu/pub/\n\
  ncftpls -x \"-lrt\" ftp://ftp.wustl.edu/pub/\n");

	(void) fprintf(fp, "\nLibrary version: %s.\n", gLibNcFTPVersion + 5);
	(void) fprintf(fp, "\nThis is a freeware program by Mike Gleason (http://www.NcFTP.com/contact/).\n");
	(void) fprintf(fp, "This was built using LibNcFTP (http://www.ncftp.com/libncftp).\n");

	ClosePager(fp);
	exit(kExitUsage);
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




static void
SetLsFlags(char *dst, size_t dsize, int *longMode, const char *src)
{
	char *dlim = dst + dsize - 1;
	int i, c;

	for (i=0;;) {
		c = *src++;
		if (c == '\0')
			break;
		if (c == 'l') {
			*longMode = 1;
		} else if (c == '1') {
			*longMode = 0;
		} else if (c != '-') {
			if (c == 'C') {
				*longMode = 0;
			}
			if (i == 0) {
				if (dst < dlim)
					*dst++ = '-';
			} 
			i++;
			if (dst < dlim)
				*dst++ = (char) c;
		}
	}
	*dst = '\0';
}	/* SetLsFlags */





main_void_return_t
main(int argc, char **argv)
{
	int result, c;
	FTPConnectionInfo savedfi;
	FTPConnectionInfo startfi;
	ExitStatus es;
	char url[256];
	char urlfile[128];
	char rootcwd[256];
	int longMode = 0;
	int i;
	char lsflag[32] = "";
	FTPLineList cdlist;
	FTPLinePtr lp;
	int rc;
	int ndirs;
	GetoptInfo opt;

	InitWinsock();
	result = FTPInitLibrary(&li);
	if (result < 0) {
		(void) fprintf(stderr, "ncftpls: init library error %d (%s).\n", result, FTPStrError(result));
		DisposeWinsock();
		exit(kExitInitLibraryFailed);
	}
	result = FTPInitConnectionInfo(&li, &fi, kDefaultFTPBufSize);
	if (result < 0) {
		(void) fprintf(stderr, "ncftpls: init connection info error %d (%s).\n", result, FTPStrError(result));
		DisposeWinsock();
		exit(kExitInitConnInfoFailed);
	}

	fi.debugLog = NULL;
	fi.errLog = stderr;
	fi.xferTimeout = 60 * 60;
	fi.connTimeout = 30;
	fi.ctrlTimeout = 135;
	(void) STRNCPY(fi.user, "anonymous");
	fi.host[0] = '\0';
	urlfile[0] = '\0';
	InitLineList(&cdlist);
	SetLsFlags(lsflag, sizeof(lsflag), &longMode, "-CF");
	es = kExitSuccess;

	GetoptReset(&opt);
	while ((c = Getopt(&opt, argc, argv, "1lx:P:u:p:e:d:t:r:f:F")) > 0) switch(c) {
		case 'P':
			fi.port = atoi(opt.arg);	
			break;
		case 'u':
			(void) STRNCPY(fi.user, opt.arg);
			break;
		case 'p':
			(void) STRNCPY(fi.pass, opt.arg);	/* Don't recommend doing this! */
			break;
		case 'e':
			if (strcmp(opt.arg, "stdout") == 0)
				fi.errLog = stdout;
			else if (opt.arg[0] == '-')
				fi.errLog = stdout;
			else if (strcmp(opt.arg, "stderr") == 0)
				fi.errLog = stderr;
			else
				fi.errLog = fopen(opt.arg, "a");
			break;
		case 'd':
			if (strcmp(opt.arg, "stdout") == 0)
				fi.debugLog = stdout;
			else if (opt.arg[0] == '-')
				fi.debugLog = stdout;
			else if (strcmp(opt.arg, "stderr") == 0)
				fi.debugLog = stderr;
			else
				fi.debugLog = fopen(opt.arg, "a");
			break;
		case 't':
			SetTimeouts(&fi, opt.arg);
			break;
		case 'r':
			SetRedial(&fi, opt.arg);
			break;
		case 'f':
			ReadConfigFile(opt.arg, &fi);
			break;
		case 'F':
			if (fi.dataPortMode == kPassiveMode)
				fi.dataPortMode = kSendPortMode;
			else
				fi.dataPortMode = kPassiveMode;
			break;
		case 'l':
			SetLsFlags(lsflag, sizeof(lsflag), &longMode, "-l");
			break;
		case '1':
			SetLsFlags(lsflag, sizeof(lsflag), &longMode, "-1");
			break;
		case 'x':
			SetLsFlags(lsflag, sizeof(lsflag), &longMode, opt.arg);
			break;
		default:
			Usage();
	}
	if (opt.ind > argc - 1)
		Usage();

	InitOurDirectory();
	LoadFirewallPrefs();

	startfi = fi;
	memset(&savedfi, 0, sizeof(savedfi));
	ndirs = argc - opt.ind;
	for (i=opt.ind; i<argc; i++) {
		fi = startfi;
		(void) STRNCPY(url, argv[i]);
		rc = FTPDecodeURL(&fi, url, &cdlist, urlfile, sizeof(urlfile), (int *) 0, NULL);
		(void) STRNCPY(url, argv[i]);
		if (rc == kMalformedURL) {
			(void) fprintf(stderr, "Malformed URL: %s\n", url);
			DisposeWinsock();
			exit(kExitMalformedURL);
		} else if (rc == kNotURL) {
			(void) fprintf(stderr, "Not a URL: %s\n", url);
			DisposeWinsock();
			exit(kExitMalformedURL);
		} else if (urlfile[0] != '\0') {
			/* It not obviously a directory, and they didn't say -R. */
			(void) fprintf(stderr, "Not a directory URL: %s\n", url);
			DisposeWinsock();
			exit(kExitMalformedURL);
		}
		
		if ((strcmp(fi.host, savedfi.host) == 0) && (strcmp(fi.user, savedfi.user) == 0)) {
			fi = savedfi;
			
			/* This host is currently open, so keep using it. */
			if (FTPChdir(&fi, rootcwd) < 0) {
				(void) fprintf(stderr, "ncftpls: cannot chdir to %s: %s.\n", rootcwd, FTPStrError(fi.errNo));
				es = kExitChdirFailed;
				DisposeWinsock();
				exit((int) es);
			}
		} else {
			if (savedfi.connected != 0) {
				(void) FTPCloseHost(&savedfi);
			}
			memset(&savedfi, 0, sizeof(savedfi));

			if ((strcmp(fi.user, "anonymous") != 0) && (strcmp(fi.user, "ftp") != 0) && (fi.pass[0] == '\0'))
				(void) GetPass("Password: ", fi.pass, sizeof(fi.pass));
			
			if (MayUseFirewall(fi.host) != 0) {
				fi.firewallType = gFirewallType; 
				(void) STRNCPY(fi.firewallHost, gFirewallHost);
				(void) STRNCPY(fi.firewallUser, gFirewallUser);
				(void) STRNCPY(fi.firewallPass, gFirewallPass);
				fi.firewallPort = gFirewallPort;
			}
			
			es = kExitOpenTimedOut;
			if ((result = FTPOpenHost(&fi)) < 0) {
				(void) fprintf(stderr, "ncftpls: cannot open %s: %s.\n", fi.host, FTPStrError(result));
				es = kExitOpenFailed;
				DisposeWinsock();
				exit((int) es);
			}
			
			if (FTPGetCWD(&fi, rootcwd, sizeof(rootcwd)) < 0) {
				(void) fprintf(stderr, "ncftpls: cannot pwd: %s.\n", FTPStrError(fi.errNo));
				es = kExitChdirFailed;
				DisposeWinsock();
				exit((int) es);
			}			
		}
		
		es = kExitChdirTimedOut;
		for (lp = cdlist.first; lp != NULL; lp = lp->next) {
			if (FTPChdir(&fi, lp->line) != 0) {
				(void) fprintf(stderr, "ncftpls: cannot chdir to %s: %s.\n", lp->line, FTPStrError(fi.errNo));
				es = kExitChdirFailed;
				DisposeWinsock();
				exit((int) es);
			}
		}
		
		if (ndirs > 1) {
			fprintf(stdout, "%s%s\n\n",
				(i > opt.ind) ? "\n\n\n" : "", url);
		}
		fflush(stdout);
		
		es = kExitXferTimedOut;
		(void) signal(SIGINT, Abort);
		if (FTPList(&fi, STDOUT_FILENO, longMode, lsflag) < 0) {
			(void) fprintf(stderr, "ncftpls: directory listing error: %s.\n", FTPStrError(fi.errNo));
			es = kExitXferFailed;
		} else {
			es = kExitSuccess;
			savedfi = fi;
		}
		(void) signal(SIGINT, SIG_DFL);
	}
	
	(void) FTPCloseHost(&fi);

	DisposeWinsock();
	exit((int) es);
}	/* main */
