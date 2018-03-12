/* ncftpput.c
 *
 * A simple, non-interactive utility to send files to a remote FTP server.
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
	(void) fprintf(fp, "NcFTPPut %s.\n\n", VERSION);
	(void) fprintf(fp, "*** NOTE: This version is for demonstration only -- get the latest and most\nfeature complete version from the NcFTP Client package. ***\n\n");
	(void) fprintf(fp, "Usages:\n");
	(void) fprintf(fp, "  ncftpput [flags] remote-host remote-dir local-files...   (mode 1)\n");
	(void) fprintf(fp, "  ncftpput -f login.cfg [flags] remote-dir local-files...  (mode 2)\n");
	(void) fprintf(fp, "  ncftpput -c remote-host remote-path-name < stdin  (mode 3)\n");
	(void) fprintf(fp, "\nFlags:\n\
  -u XX  Use username XX instead of anonymous.\n\
  -p XX  Use password XX with the username.\n\
  -P XX  Use port number XX instead of the default FTP service port (21).\n\
  -d XX  Use the file XX for debug logging.\n\
  -e XX  Use the file XX for error logging.\n\
  -U XX  Use value XX for the umask.\n\
  -t XX  Timeout after XX seconds.\n");
	(void) fprintf(fp, "\
  -a     Use ASCII transfer type instead of binary.\n\
  -m     Attempt to mkdir the dstdir before copying.\n\
  -v/-V  Do (do not) use progress meters.\n\
  -f XX  Read the file XX for host, user, and password information.\n");
	(void) fprintf(fp, "\
  -c     Use stdin as input file to write on remote host.\n\
  -A     Append to remote files instead of overwriting them.\n\
  -z/-Z  Do (do not) not try to resume uploads (default: -Z).\n\
  -T XX  Upload into temporary files prefixed by XX.\n");
	(void) fprintf(fp, "\
  -S XX  Upload into temporary files suffixed by XX.\n\
  -DD    Delete local file after successfully uploading it.\n\
  -F     Use passive (PASV) data connections.\n\
  -y     Try using \"SITE UTIME\" to preserve timestamps on remote host.\n\
  -r XX  Redial XX times until connected.\n\
  -R     Recursive mode; copy whole directory trees.\n");
	(void) fprintf(fp, "\nExamples:\n\
  ncftpput -u gleason -p my.password Elwood.probe.net /home/gleason stuff.txt\n\
  ncftpput -u gleason Elwood.probe.net /home/gleason a.txt (prompt for pass)\n\
  ncftpput -a -u gleason -p my.password -m -U 007 Bozo.probe.net /tmp/tmpdir a.txt\n");
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
Copy(FTPCIPtr cip, const char *dstdir, char **files, int rflag, int xtype, int appendflag, const char *tmppfx, const char *tmpsfx, int resumeflag, int deleteflag)
{
	int i;
	int result;
	const char *file;
	int rc = 0;

	for (i=0; ; i++) {
		file = files[i];
		if (file == NULL)
			break;
		result = FTPPutFiles3(cip, file, dstdir, rflag,
#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
			kGlobYes,
#else
			kGlobNo,	/* Shell does the glob for you */
#endif
			xtype, appendflag, tmppfx, tmpsfx, resumeflag, deleteflag, kNoFTPConfirmResumeUploadProc, 0);
		if (result != 0) {
			(void) fprintf(stderr, "ncftpput: file send error: %s.\n", FTPStrError(result));
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
	int appendflag = kAppendNo;
	int deleteflag = kDeleteNo;
	int resumeflag = kResumeNo;
	const char *tmppfx = "";
	const char *tmpsfx = "";
	int xtype = kTypeBinary;
	ExitStatus es;
	int wantMkdir = 0;
	const char *Umask = NULL;
	const char *dstdir;
	char **files;
	int progmeters;
	int usingcfg = 0;
	int ftpcat = 0;
	int tryUtime = 0;
	int nD = 0;
	GetoptInfo opt;

	InitWinsock();
	result = FTPInitLibrary(&li);
	if (result < 0) {
		(void) fprintf(stderr, "ncftpput: init library error %d (%s).\n", result, FTPStrError(result));
		DisposeWinsock();
		exit(kExitInitLibraryFailed);
	}
	result = FTPInitConnectionInfo(&li, &fi, kDefaultFTPBufSize);
	if (result < 0) {
		(void) fprintf(stderr, "ncftpput: init connection info error %d (%s).\n", result, FTPStrError(result));
		DisposeWinsock();
		exit(kExitInitConnInfoFailed);
	}

	fi.xferTimeout = 60 * 60;
	fi.connTimeout = 30;
	fi.ctrlTimeout = 135;
	fi.debugLog = NULL;
	fi.errLog = stderr;
	(void) STRNCPY(fi.user, "anonymous");
	progmeters = GetDefaultProgressMeterSetting();
	dstdir = NULL;
	files = NULL;

	GetoptReset(&opt);
	while ((c = Getopt(&opt, argc, argv, "P:u:p:e:d:U:t:mar:RvVf:AT:S:FcyZzD")) > 0) switch(c) {
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
		case 'U':
			Umask = opt.arg;
			break;
		case 't':
			SetTimeouts(&fi, opt.arg);
			break;
		case 'm':
			wantMkdir = 1;
			break;
		case 'a':
			xtype = kTypeAscii;	/* Use ascii. */
			break;
		case 'r':
			SetRedial(&fi, opt.arg);
			break;
		case 'R':
			rflag = 1;
			break;
		case 'v':
			progmeters = 1;
			break;
		case 'V':
			progmeters = 0;
			break;
		case 'f':
			ReadConfigFile(opt.arg, &fi);
			usingcfg = 1;
			break;
		case 'A':
			appendflag = 1;
			break;
		case 'T':
			tmppfx = opt.arg;
			break;
		case 'S':
			tmpsfx = opt.arg;
			break;
		case 'F':
			if (fi.dataPortMode == kPassiveMode)
				fi.dataPortMode = kSendPortMode;
			else
				fi.dataPortMode = kPassiveMode;
			break;
		case 'c':
			ftpcat = 1;
			break;
		case 'y':
			tryUtime = 1;
			break;
		case 'z':
			resumeflag = kResumeYes;
			break;
		case 'Z':
			resumeflag = kResumeNo;
			break;
		default:
			Usage();
	}
	if (usingcfg != 0) {
		if (ftpcat == 0) {
			if (opt.ind > argc - 2)
				Usage();
			dstdir = argv[opt.ind + 0];
			files = argv + opt.ind + 1;
		} else {
			if (opt.ind > argc - 2)
				Usage();
			(void) STRNCPY(fi.host, argv[opt.ind]);
		}
	} else {
		if (ftpcat == 0) {
			if (opt.ind > argc - 3)
				Usage();
			(void) STRNCPY(fi.host, argv[opt.ind]);
			dstdir = argv[opt.ind + 1];
			files = argv + opt.ind + 2;
		} else {
			if (opt.ind > argc - 2)
				Usage();
			(void) STRNCPY(fi.host, argv[opt.ind]);
		}
	}

	InitOurDirectory();
	LoadFirewallPrefs();

	if ((strcmp(fi.user, "anonymous") != 0) && (strcmp(fi.user, "ftp") != 0) && (fi.pass[0] == '\0'))
		(void) GetPass("Password: ", fi.pass, sizeof(fi.pass));

	if (progmeters != 0)
		fi.progress = PrStatBar;
	if (tryUtime == 0)
		fi.hasSITE_UTIME = 0;
	if (nD >= 2)
		deleteflag = kDeleteYes;

	if (MayUseFirewall(fi.host) != 0) {
		fi.firewallType = gFirewallType; 
		(void) STRNCPY(fi.firewallHost, gFirewallHost);
		(void) STRNCPY(fi.firewallUser, gFirewallUser);
		(void) STRNCPY(fi.firewallPass, gFirewallPass);
		fi.firewallPort = gFirewallPort;
	}

	
	es = kExitOpenTimedOut;
	if ((result = FTPOpenHost(&fi)) < 0) {
		(void) fprintf(stderr, "ncftpput: cannot open %s: %s.\n", fi.host, FTPStrError(result));
		es = kExitOpenFailed;
		DisposeWinsock();
		exit((int) es);
	}
	if (Umask != NULL) {
		result = FTPUmask(&fi, Umask);
		if (result != 0)
			(void) fprintf(stderr, "ncftpput: umask failed: %s.\n", FTPStrError(result));
	}
	if (wantMkdir != 0) {
		result = FTPMkdir(&fi, dstdir, kRecursiveYes);
		if (result != 0)
			(void) fprintf(stderr, "ncftpput: mkdir failed: %s.\n", FTPStrError(result));
	}
	if (result >= 0) {
		es = kExitXferTimedOut;
		(void) signal(SIGINT, Abort);
		if (ftpcat == 0) {
			if (Copy(&fi, dstdir, files, rflag, xtype, appendflag, (const char *) tmppfx, (const char *) tmpsfx, resumeflag, deleteflag) < 0)
				es = kExitXferFailed;
			else
				es = kExitSuccess;
		} else {
			if (FTPPutOneFile2(&fi, NULL, argv[opt.ind + 1], xtype, STDIN_FILENO, appendflag, tmppfx, tmpsfx) < 0)
				es = kExitXferFailed;
			else
				es = kExitSuccess;
		}
	}
	
	(void) FTPCloseHost(&fi);
	DisposeWinsock();
	exit((int) es);
}	/* main */
