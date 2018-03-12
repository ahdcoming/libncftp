/* pncftp.c */

#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
#	define _CRT_SECURE_NO_WARNINGS 1
#endif

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include <ncftp.h>				/* Library header. */
#include <Strn.h>				/* Library header. */
#include <sio.h>				/* Library header. */

#define VERSION "0.9.0 (2004-05-26)"

FTPLibraryInfo li;
FTPConnectionInfo srcci, dstci;
FTPCIPtr pasvhost, porthost;
FILE *debugLog = NULL;
int timeout = -1;



static void
Usage(void)
{
	FILE *fp;

	fp = stdout;
	(void) fprintf(fp, "Proxy NcFTP %s.\n\n", VERSION);
	(void) fprintf(fp, "Usage:\n");
	(void) fprintf(fp, "  pncftp [flags] source-host source-path dest-host dest-path\n");
	(void) fprintf(fp, "\nHost Parameters:\n\
  Are actually allowed to have the following formats:\n\
         [user[:pass]@]host[:port]\n\
         [user[/path/to/password.txt]@]host[:port]\n\
  This allows you to optionally specify usernames, passwords, and port numbers.\n\
  Defaults are anonymous user and port 21.\n");
	(void) fprintf(fp, "\nFlags:\n\
  -a     Use ASCII transfer type instead of binary.\n\
  -A     Use PASV on source host and PORT on destination host (default)\n\
  -B     Use PORT on source host and PASV on destination host\n\
  -d XX  Use the file XX for debug logging (for stdout, use \"-\").\n\
  -DD    Attempt to delete the source file after successfully uploading it.\n\
  -f     Force overwrite of destination file if it already exists.\n\
  -m     Attempt to mkdir the dstdir before copying.\n\
  -o XX  Start transferring from offset XX bytes.\n\
  -O XX  Specify miscellaneous options (see documentation).\n\
  -t XX  Timeout after XX seconds.\n\
  -U XX  Attempt to use value XX for the umask on destination host.\n\
  -z     Resume transfer of destination file if it already exists.\n");
	(void) fprintf(fp, "\nExample:\n\
  pncftp -d - joeuser:joespassword@ftp.joe.com /joesfiles/6gigs ftp.example.com /pub/incoming/sixgigfile\n");

	(void) fprintf(fp, "\nLibrary version: %s.\n", gLibNcFTPVersion + 5);
	(void) fprintf(fp, "\nThis is a freeware program by Mike Gleason (http://www.NcFTP.com/contact/).\n");
	(void) fprintf(fp, "This was built using LibNcFTP (http://www.ncftp.com/libncftp).\n");

	DisposeWinsock();
	exit(2);
}	/* Usage */




static void
PNcFTPLogProc(const FTPCIPtr cip, char *line)
{
	const char *pfx;
	time_t t;
	char tstr[64];
	
	if (debugLog == NULL)
		return;
		
	time(&t);
	strftime(tstr, sizeof(tstr) - 1, "%Y-%m-%d %H:%M:%S", localtime(&t));
	if (cip == &srcci) {
		pfx = "SRC";
	} else {
		pfx = "DST";
	}
	fprintf(debugLog, "%s | %s | %s", tstr, pfx, line);
	if (line[strlen(line) - 1] != '\n')
		fprintf(debugLog, "\n");	
	fflush(debugLog);
}	/* PNcFTPLogProc */




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




static int
Get1Response(const FTPCIPtr cip)
{
	int result;
	int respCode;
	ResponsePtr rp;

	rp = InitResponse();
	if (rp == NULL) {
		cip->errNo = kErrMallocFailed;
		result = cip->errNo;
		return (result);
	}
	result = GetResponse(cip, rp);
	if (result < 0)
		return (result);
	respCode = rp->codeType;
	DoneWithResponse(cip, rp);
	if (respCode > 2) {
		result = -1;
	} else {
		result = kNoErr;
	}
	
	return (result);
}	/* Get1Response */




#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
#       define MY_FD_ZERO FD_ZERO
#       define MY_FD_SET(s,set) FD_SET((SOCKET) (s), set)
#       define MY_FD_CLR(s,set) FD_CLR((SOCKET) (s), set)
#       define MY_FD_ISSET FD_ISSET
#else
#       define MY_FD_ZERO FD_ZERO
#       define MY_FD_SET FD_SET
#       define MY_FD_CLR FD_CLR
#       define MY_FD_ISSET FD_ISSET
#endif

static void
WaitForResponses(int srcfd, int *srcrc, int dstfd, int *dstrc, int stopearlyiferr, int secs)
{
	int result;
	fd_set ss;
	struct timeval tv;
	int fdmax;
	int srcdone = 0, dstdone = 0;
	int rc;
	int secsWaited = 0;
	
	*srcrc = *dstrc = 0;
	fdmax = srcci.ctrlSocketR;
	if (srcci.ctrlSocketR < dstci.ctrlSocketR)
		fdmax = dstci.ctrlSocketR;
		
	forever {
		if (srcfd < 0)
			srcdone++;
		if (dstfd < 0)
			dstdone++;
		if (srcdone && dstdone)
			break;
		if (!srcdone && !dstdone)
			PrintF(&srcci, "Waiting for %s and %s to reply.\n", srcci.host, dstci.host);
		else if (!dstdone)
			PrintF(&dstci, "Waiting for %s to reply.\n", dstci.host);
		else if (!srcdone)
			PrintF(&srcci, "Waiting for %s to reply.\n", srcci.host);

		MY_FD_ZERO(&ss);
#if defined(__DECC) || defined(__DECCXX)
#pragma message save
#pragma message disable trunclongint
#endif
		if (srcfd >= 0)
			MY_FD_SET(srcfd, &ss);
		if (dstfd >= 0)
			MY_FD_SET(dstfd, &ss);
#if defined(__DECC) || defined(__DECCXX)
#pragma message restore
#endif
		tv.tv_sec = (tv_sec_t) secs;
		tv.tv_usec = 0;
		do {
			errno = 0;
			result = select(fdmax + 1, SELECT_TYPE_ARG234 &ss, NULL, NULL, &tv);
		} while ((result < 0) && (errno == EINTR));
		if (result > 0) {
			if ((srcfd >= 0) && MY_FD_ISSET(srcfd, &ss)) {
				*srcrc = rc = Get1Response(&srcci);
				if ((rc < 0) && (stopearlyiferr != 0)) {
					/* Close the other one early */
					FTPCloseControlConnection(&dstci);
					return;
				}
				srcdone++;
			}
			if ((dstfd >= 0) && MY_FD_ISSET(dstfd, &ss)) {
				*dstrc = rc = Get1Response(&dstci);
				if ((rc < 0) && (stopearlyiferr != 0)) {
					/* Close the other one early */
					FTPCloseControlConnection(&srcci);
					return;
				}
				dstdone++;
			}
		} else if (result == 0) {
			secsWaited += secs;
			if (secsWaited >= timeout) {
				PrintF(&srcci, "Timed-out (%d seconds), aborting.\n", timeout);
				FTPCloseControlConnection(&srcci);
				FTPCloseControlConnection(&dstci);
				return;
			}
		}
	}
}	/* WaitForResponses */




main_void_return_t
main(int argc, char **argv)
{
	int c;
	int es = 0;
	int rc, srcrc, dstrc;
	longest_int startPoint = (longest_int) 0;
	GetoptInfo opt;
	struct sockaddr_in pasvaddr;
	int type = kTypeBinary;
	char *srcpath, *dstpath, *dstdir = NULL, *cp;
	int sfd, dfd;
	struct timeval t0;
	double elapsed = -1.0;
	longest_int dstsize = kSizeUnknown;
	longest_int srcsize = kSizeUnknown;
	time_t srcmdtm = kModTimeUnknown;
	time_t dstmdtm = kModTimeUnknown;
	int forceOverwrite = 0, tryResume = 0;
	char *Umask = NULL;
	int nD = 0;
	int wantMkdir = 0;
	char dstcwd[128];
	
	InitWinsock();
	rc = FTPInitLibrary(&li);
	if (rc < 0) {
		fprintf(stderr, "pncftp: init library error %d (%s).\n", rc, FTPStrError(rc));
		exit(1);
	}

	rc = FTPInitConnectionInfo(&li, &srcci, kDefaultFTPBufSize);
	if (rc < 0) {
		fprintf(stderr, "pncftp: init connection info error %d (%s).\n", rc, FTPStrError(rc));
		exit(3);
	}

	rc = FTPInitConnectionInfo(&li, &dstci, kDefaultFTPBufSize);
	if (rc < 0) {
		fprintf(stderr, "pncftp: init connection info error %d (%s).\n", rc, FTPStrError(rc));
		exit(3);
	}

	GetoptReset(&opt);
	srcci.debugLogProc = PNcFTPLogProc;
	dstci.debugLogProc = PNcFTPLogProc;
	srcci.maxDials = 1;
	dstci.maxDials = 1;
	pasvhost = &srcci;
	porthost = &dstci;
	
	while ((c = Getopt(&opt, argc, argv, "ABad:Dfmto:O:U:z")) > 0) switch(c) {
		case 'A':
			pasvhost = &srcci;
			porthost = &dstci;
			break;
		case 'B':
			pasvhost = &dstci;
			porthost = &srcci;
			break;
		case 'd':
			if (opt.arg[0] == '-')
				debugLog = stdout;
			else if (strcmp(opt.arg, "stderr") == 0)
				debugLog = stderr;
			else if (strcmp(opt.arg, "stdout") == 0)
				debugLog = stdout;
			else
				debugLog = fopen(opt.arg, "a");
			break;
		case 'D':
			/* Require two -D's in case they typo. */
			nD++;
			break;
		case 'f':
			forceOverwrite = 1;
			break;
		case 'm':
			wantMkdir = 1;
			break;
		case 't':
			timeout = atoi(optarg);
			break;
		case 'z':
			tryResume = 1;
			break;
		case 'o':
			startPoint = a_to_ll(opt.arg);
			break;
		case 'O':
			srcci.manualOverrideFeatures = opt.arg;
			dstci.manualOverrideFeatures = opt.arg;
			break;
		case 'U':
			Umask = opt.arg;
			break;
		default:
			Usage();
	}
	if (opt.ind > argc - 4)
		Usage();

	/* Get the hostnames */
	rc = FTPDecodeHostName(&srcci, argv[opt.ind]);
	if (rc < 0) {
		(void) fprintf(stderr, "Malformed source hostname: %s\n", argv[opt.ind]);
		DisposeWinsock();
		exit(4);
	}
	srcpath = argv[opt.ind + 1];

	rc = FTPDecodeHostName(&dstci, argv[opt.ind + 2]);
	if (rc < 0) {
		(void) fprintf(stderr, "Malformed destination hostname: %s\n", argv[opt.ind + 2]);
		DisposeWinsock();
		exit(4);
	}
	dstpath = argv[opt.ind + 3];
	
	
	/* Open the hosts */
	(void) gettimeofday(&t0, NULL);
	if ((rc = FTPOpenHost(&dstci)) < 0) {
		FTPPerror(&dstci, rc, 0, "Could not open destination host", dstci.host);
		DisposeWinsock();
		exit(5);
	}

	if ((rc = FTPOpenHost(&srcci)) < 0) {
		FTPPerror(&srcci, rc, 0, "Could not open source host", srcci.host);
		FTPCloseHost(&dstci);
		DisposeWinsock();
		exit(6);
	}


	/* Try to create the remote destination directory, if asked. */
	if ((wantMkdir != 0) && (Dynscpy(&dstdir, dstpath, 0) != NULL)) {
		cp = StrRFindLocalPathDelim(dstdir + 1);
		if (cp != NULL) {
			*cp = '\0';
			rc = FTPGetCWD(&dstci, dstcwd, sizeof(dstcwd));
			rc = FTPChdir3(&dstci, dstdir, NULL, 0, kChdirFullPath|kChdirOneSubdirAtATime|kChdirAndMkdir);
			if (rc != kNoErr) {
				PrintF(&dstci, "Could not create remote directory on destination host %s.\n", dstci.host);
			}
			rc = FTPChdir(&dstci, dstcwd);
		}
	}
	
	
	/* Check the source file's size, if possible. */
	rc = FTPFileSizeAndModificationTime(&srcci, srcpath, &srcsize, kTypeBinary, &srcmdtm);
	rc = FTPFileSizeAndModificationTime(&dstci, dstpath, &dstsize, kTypeBinary, &dstmdtm);
	if (dstsize > 0) {
		if ((srcsize > 0) && (tryResume != 0)) {
			if (dstsize > srcsize) {
				PrintF(&srcci, "Destination file exists and is larger than the source file, aborting.\n");
				es = 7;
				goto quit;
			} else if (dstsize == srcsize) {
				PrintF(&srcci, "Destination file exists and is already the same size as the source file.\n");
				exit(0);	/* Not an error */
			} else {
				startPoint = dstsize;
			}
		} else if (forceOverwrite == 0) {
			PrintF(&srcci, "Destination file exists, aborting.\n");
			es = 8;
			goto quit;
		}
	}


	/* Set the umask on the destination host, if asked. */
        if (Umask != NULL) {
                rc = FTPUmask(&dstci, Umask);
                if (rc != 0)
                        FTPPerror(&dstci, rc, kErrUmaskFailed, "Could not set umask", Umask);
        }


	/* Set TYPE I or A on each host */
	if ((rc = FTPSetTransferType(&srcci, type)) < 0) {
		FTPPerror(porthost, rc, 0, "Could not set TYPE on source host", srcci.host);
		es = 9;
		goto quit;
	}
		
	if ((rc = FTPSetTransferType(&dstci, type)) < 0) {
		FTPPerror(porthost, rc, kErrTYPEFailed, "Could not set TYPE on destination host", dstci.host);
		es = 10;
		goto quit;
	}

	
	/* Setup the data connection addresses. */
	if ((rc = FTPSendPassive(pasvhost, &pasvaddr, NULL)) != 0) {
		FTPPerror(pasvhost, rc, kErrPASVFailed, "PASV failed on host", pasvhost->host);
		es = 11;
		goto quit;
	}

	if ((rc = FTPSendPort(porthost, &pasvaddr)) != 0) {
		FTPPerror(porthost, rc, kErrPORTFailed, "PORT failed on host", porthost->host);
		es = 12;
		goto quit;
	}


	/* If asked, attempt to start at a later position in the remote file. */
	if (startPoint != (longest_int) 0) {
		if ((startPoint == kSizeUnknown) || ((rc = FTPSetStartOffset(&dstci, startPoint)) != 0))
			startPoint = (longest_int) 0;
		if ((startPoint == kSizeUnknown) || ((rc = FTPSetStartOffset(&srcci, startPoint)) != 0))
			startPoint = (longest_int) 0;
	}
	dstci.startPoint = startPoint;
	srcci.startPoint = startPoint;


	/* Start the proxy transfer. */
	PrintF(&srcci, "Attempting proxy transfer from source host %s to destination host %s.\n", srcci.host, dstci.host);
	(void) gettimeofday(&t0, NULL);
	if (pasvhost == &srcci) {
		if ((rc = FTPCmdNoResponse(&srcci, "RETR %s", srcpath)) != 0) {
			FTPPerror(porthost, rc, kErrCouldNotStartDataTransfer, "Could not start download on source host", srcci.host);
			es = 13;
			goto quit;
		}
		if ((rc = FTPCmdNoResponse(&dstci, "STOR %s", dstpath)) != 0) {
			FTPPerror(porthost, rc, kErrCouldNotStartDataTransfer, "Could not start upload on destination host", dstci.host);
			es = 14;
			goto quit;
		}
	} else {
		if ((rc = FTPCmdNoResponse(&dstci, "STOR %s", dstpath)) != 0) {
			FTPPerror(porthost, rc, kErrCouldNotStartDataTransfer, "Could not start upload on destination host", dstci.host);
			es = 14;
			goto quit;
		}
		if ((rc = FTPCmdNoResponse(&srcci, "RETR %s", srcpath)) != 0) {
			FTPPerror(porthost, rc, kErrCouldNotStartDataTransfer, "Could not start download on source host", srcci.host);
			es = 13;
			goto quit;
		}
	}

	dfd = dstci.ctrlSocketR;
	sfd = srcci.ctrlSocketR;
	WaitForResponses(sfd, &srcrc, dfd, &dstrc, 1, 5);
	if (srcrc != 0) {
		PrintF(&srcci, "Could not start download on source host %s.\n", srcci.host);
		es = 15;
		sfd = -1;
	}
	if (dstrc != 0) {
		PrintF(&dstci, "Could not start upload on destination host %s.\n", dstci.host);
		es = 16;
		dfd = -1;
	}
	if (es == 0) {
		if (srcsize > 0) {
			PrintF(&srcci, "Proxy transfer of " PRINTF_LONG_LONG " bytes from source host %s to destination host %s has started.\n", srcsize, srcci.host, dstci.host);
		} else {
			PrintF(&srcci, "Proxy transfer from source host %s to destination host %s has started.\n", srcci.host, dstci.host);
		}
		WaitForResponses(sfd, &srcrc, dfd, &dstrc, 0, 60);
		if (srcrc != 0) {
			PrintF(&dstci, "Download from source host %s failed.\n", srcci.host);
			es = 17;
		}
		if (dstrc != 0) {
			PrintF(&dstci, "Upload to destination host %s failed.\n", dstci.host);
			es = 18;
		}
		elapsed = FTPDuration(&t0);
		if (es == 0) {
			if ((nD >= 2) && (FTPDelete(&srcci, srcpath, kRecursiveNo, kGlobNo) != kNoErr)) {
				PrintF(&srcci, "Could not delete source file %s after successful transfer to destination host.\n", srcpath);
			}
			(void) FTPUtime(&dstci, dstpath, srcmdtm, srcmdtm, srcmdtm);
			rc = FTPFileSizeAndModificationTime(&dstci, dstpath, &dstsize, kTypeBinary, &dstmdtm);
		}
	}
	
quit:
	if (elapsed < 0)
		elapsed = FTPDuration(&t0);
	FTPCloseHost(&dstci);
	FTPCloseHost(&srcci);
	DisposeWinsock();
	if (es == 0) {
		if (dstsize > 0) {
			printf("Done.  "  PRINTF_LONG_LONG " bytes in %.2f seconds (%.1f kB/sec).\n", dstsize, elapsed, (double) dstsize / 1024.0);
		} else if (srcsize > 0) {
			printf("Done.  "  PRINTF_LONG_LONG " bytes in %.2f seconds (%.1f kB/sec).\n", dstsize, elapsed, (double) srcsize / 1024.0);
		} else {
			printf("Done.  Elapsed Time = %.2f seconds.\n", elapsed);
		}
	} else {
		printf("ERROR(%d).  Elapsed Time = %.2f seconds.\n", es, elapsed);
	}
	exit(es);
}	/* main */

/* EOF pncftp.c */
