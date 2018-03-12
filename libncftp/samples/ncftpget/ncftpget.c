/* ncftpget.c
 *
 * A non-interactive utility to grab files from a remote FTP server.
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

#include "gpshare.h"

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
	(void) fprintf(fp, "NcFTPGet %s.\n\n", VERSION);
	(void) fprintf(fp, "*** NOTE: This version is for demonstration only -- get the latest and most\nfeature complete version from the NcFTP Client package. ***\n\n");
	(void) fprintf(fp, "Usages:\n");
	(void) fprintf(fp, "  ncftpget [flags] remote-host local-dir remote-path-names...   (mode 1)\n");
	(void) fprintf(fp, "  ncftpget -f login.cfg [flags] local-dir remote-path-names...  (mode 2)\n");
	(void) fprintf(fp, "  ncftpget [flags] ftp://url.style.host/path/name               (mode 3)\n");
	(void) fprintf(fp, "\nFlags:\n\
  -u XX  Use username XX instead of anonymous.\n\
  -p XX  Use password XX with the username.\n\
  -P XX  Use port number XX instead of the default FTP service port (21).\n\
  -d XX  Use the file XX for debug logging.\n\
  -a     Use ASCII transfer type instead of binary.\n");
	(void) fprintf(fp, "\
  -t XX  Timeout after XX seconds.\n\
  -v/-V  Do (do not) use progress meters.\n\
  -f XX  Read the file XX for host, user, and password information.\n\
  -A     Append to local files, instead of overwriting them.\n");
	(void) fprintf(fp, "\
  -z/-Z  Do (do not) not try to resume downloads (default: -z).\n\
  -F     Use passive (PASV) data connections.\n\
  -DD    Delete remote file after successfully downloading it.\n\
  -r XX  Redial XX times until connected.\n\
  -R     Recursive mode; copy whole directory trees.\n");
	(void) fprintf(fp, "\nExamples:\n\
  ncftpget ftp.wustl.edu . /pub/README /pub/README.too\n\
  ncftpget ftp.wustl.edu . '/pub/README*'\n\
  ncftpget -R ftp.ncftp.com /tmp /pub/ncftpd  (ncftpd is a directory)\n\
  ncftpget ftp://ftp.wustl.edu/pub/README\n\
  ncftpget -u gleason -p my.password Bozo.probe.net . '/home/mjg/.*rc'\n\
  ncftpget -u gleason Bozo.probe.net . /home/mjg/foo.txt  (prompt for password)\n\
  ncftpget -f Bozo.cfg '/home/mjg/.*rc'\n\
  ncftpget -a -d /tmp/debug.log -t 60 ftp.wustl.edu . '/pub/README*'\n");

	(void) fprintf(fp, "\nLibrary version: %s.\n", gLibNcFTPVersion + 5);
	(void) fprintf(fp, "\nThis is a freeware program by Mike Gleason (http://www.NcFTP.com/contact/).\n");
	(void) fprintf(fp, "This was built using LibNcFTP (http://www.ncftp.com/libncftp).\n");

	ClosePager(fp);
	DisposeWinsock();
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




static int 
Copy(FTPCIPtr cip, const char *dstdir, char **files, int rflag, int xtype, int resumeflag, int appendflag, int deleteflag, int tarflag)
{
	int i;
	int result;
	const char *file;
	int rc = 0;

	for (i=0; ; i++) {
		file = files[i];
		if (file == NULL)
			break;
		result = FTPGetFiles3(cip, file, dstdir, rflag, kGlobYes, xtype, resumeflag, appendflag, deleteflag, tarflag, kNoFTPConfirmResumeDownloadProc, 0);
		if (result != 0) {
			(void) fprintf(stderr, "ncftpget: file retrieval error: %s.\n", FTPStrError(result));
			rc = result;
		}
	}
	return (rc);
}	/* Copy */



main_void_return_t
main(int argc, char **argv)
{
	int result, c;
	int rflag = 0;
	int xtype = kTypeBinary;
	int appendflag = kAppendNo;
	int resumeflag = kResumeYes;
	int deleteflag = kDeleteNo;
	int tarflag = kTarYes;
	int progmeters;
	const char *dstdir;
	char **flist;
	ExitStatus es;
	char url[256];
	char urlfile[128];
	int urlxtype;
	FTPLineList cdlist;
	FTPLinePtr lp;
	int rc;
	int nD = 0;
	GetoptInfo opt;

	InitWinsock();
	result = FTPInitLibrary(&li);
	if (result < 0) {
		(void) fprintf(stderr, "ncftpget: init library error %d (%s).\n", result, FTPStrError(result));
		DisposeWinsock();
		exit(kExitInitLibraryFailed);
	}
	result = FTPInitConnectionInfo(&li, &fi, kDefaultFTPBufSize);
	if (result < 0) {
		(void) fprintf(stderr, "ncftpget: init connection info error %d (%s).\n", result, FTPStrError(result));
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
	progmeters = GetDefaultProgressMeterSetting();
	urlfile[0] = '\0';
	InitLineList(&cdlist);
	dstdir = NULL;

	GetoptReset(&opt);
	while ((c = Getopt(&opt, argc, argv, "P:u:p:e:d:t:aRr:vVf:ADzZFT")) > 0) switch(c) {
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
		case 'D':
			/* Require two -D's in case they typo. */
			nD++;
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
		case 'a':
			xtype = kTypeAscii;
			break;
		case 'r':
			SetRedial(&fi, opt.arg);
			break;
		case 'R':
			rflag = 1;
			break;
		case 'T':
			tarflag = 0;
			break;
		case 'v':
			progmeters = 1;
			break;
		case 'V':
			progmeters = 0;
			break;
		case 'f':
			ReadConfigFile(opt.arg, &fi);
			break;
		case 'A':
			appendflag = kAppendYes;
			break;
		case 'z':
			resumeflag = kResumeYes;
			break;
		case 'Z':
			resumeflag = kResumeNo;
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

	InitOurDirectory();
	LoadFirewallPrefs();

	if (progmeters != 0)
		fi.progress = PrStatBar;

	if (fi.host[0] == '\0') {
		(void) STRNCPY(url, argv[opt.ind]);
		rc = FTPDecodeURL(&fi, url, &cdlist, urlfile, sizeof(urlfile), (int *) &urlxtype, NULL);
		if (rc == kMalformedURL) {
			(void) fprintf(stderr, "Malformed URL: %s\n", url);
			DisposeWinsock();
			exit(kExitMalformedURL);
		} else if (rc == kNotURL) {
			/* This is what should happen most of the time. */
			if (opt.ind > argc - 3)
				Usage();
			(void) STRNCPY(fi.host, argv[opt.ind]);
			dstdir = argv[opt.ind + 1];
			flist = argv + opt.ind + 2;
		} else {
			/* URL okay */
			flist = NULL;
			if ((urlfile[0] == '\0') && (rflag == 0)) {
				/* It was obviously a directory, and they didn't say -R. */
				(void) fprintf(stderr, "ncftpget: Use -R if you want the whole directory tree.\n");
				es = kExitUsage;
				DisposeWinsock();
				exit((int) es);
			}
			xtype = urlxtype;
		}
	} else {
		if (opt.ind > argc - 2)
			Usage();
		dstdir = argv[opt.ind + 0];
		flist = argv + opt.ind + 1;
	}

	if ((strcmp(fi.user, "anonymous") != 0) && (strcmp(fi.user, "ftp") != 0) && (fi.pass[0] == '\0'))
		(void) GetPass("Password: ", fi.pass, sizeof(fi.pass));

	if (MayUseFirewall(fi.host) != 0) {
		fi.firewallType = gFirewallType; 
		(void) STRNCPY(fi.firewallHost, gFirewallHost);
		(void) STRNCPY(fi.firewallUser, gFirewallUser);
		(void) STRNCPY(fi.firewallPass, gFirewallPass);
		fi.firewallPort = gFirewallPort;
	}

	if (nD >= 2)
		deleteflag = kDeleteYes;
	
	es = kExitOpenTimedOut;
	if ((result = FTPOpenHost(&fi)) < 0) {
		(void) fprintf(stderr, "ncftpget: cannot open %s: %s.\n", fi.host, FTPStrError(result));
		es = kExitOpenFailed;
		DisposeWinsock();
		exit((int) es);
	}
	if (flist == NULL) {
		/* URL mode */
		es = kExitChdirTimedOut;
		for (lp = cdlist.first; lp != NULL; lp = lp->next) {
			if (FTPChdir(&fi, lp->line) != 0) {
				(void) fprintf(stderr, "ncftpget: cannot chdir to %s: %s.\n", lp->line, FTPStrError(fi.errNo));
				es = kExitChdirFailed;
				DisposeWinsock();
				exit((int) es);
			}
		}
		
		es = kExitXferTimedOut;
		(void) signal(SIGINT, Abort);
		if (FTPGetFiles3(&fi, urlfile, ".", rflag, kGlobYes, xtype, resumeflag, appendflag, deleteflag, tarflag, kNoFTPConfirmResumeDownloadProc, 0) < 0) {
			(void) fprintf(stderr, "ncftpget: file retrieval error: %s.\n", FTPStrError(fi.errNo));
			es = kExitXferFailed;
		} else {
			es = kExitSuccess;
		}
	} else {
		es = kExitXferTimedOut;
		(void) signal(SIGINT, Abort);
		if (Copy(&fi, dstdir, flist, rflag, xtype, resumeflag, appendflag, deleteflag, tarflag) < 0)
			es = kExitXferFailed;
		else
			es = kExitSuccess;
	}
	(void) FTPCloseHost(&fi);
	DisposeWinsock();
	exit((int) es);
}	/* main */
