/* ncftpftw.c
 *
 * Copyright (c) 2001 Mike Gleason, NcFTP Software.
 * All rights reserved.
 *
 */

#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
#	define _CRT_SECURE_NO_WARNINGS 1
#endif

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include <ncftp.h>				/* Library header. */
#include <Strn.h>				/* Library header. */

FTPLibraryInfo gLib;
FTPConnectionInfo fi;
char *gDirToTraverse;
size_t gLimitMaxDirs = 0;
size_t gLimitMaxFiles = 0;
size_t gLimitMaxDepth = 20;
int gDebugExtra = 0;

static void
Usage(void)
{
	FILE *fp = stdout;

	(void) fprintf(fp, "Usages:\n");
	(void) fprintf(fp, "  ncftpftw [flags] ftp://url.style.host/dir/path/name/\n");
	(void) fprintf(fp, "  ncftpftw [flags] /local/directory/path/name\n");
	(void) fprintf(fp, "\nFlags:\n\
  -l     Long mode; Prefix lines with modification timestamp and file size.\n\
  -1     Short mode.  (Think: /bin/ls -1 -R -F)\n\
  -z     Print summary of statistics at exit.\n\
  -n XX  Exit after processing XX files.\n\
  -N XX  Exit after processing XX directories.\n\
  -D XX  Exit if depth reaches XX.\n\
  -d XX  Use the file XX for debug logging.\n");
	(void) fprintf(fp, "\nExample:\n\
  ncftpftw ftp://ftp.freebsd.org/pub/FreeBSD/ISO-IMAGES/\n");

	(void) fprintf(fp, "\nLibrary version: %s.\n", gLibNcFTPVersion + 5);
	(void) fprintf(fp, "\nThis is a freeware program by Mike Gleason (http://www.NcFTP.com/contact/).\n");
	(void) fprintf(fp, "This was built using LibNcFTP (http://www.ncftp.com/libncftp).\n");

	DisposeWinsock();
	exit(2);
}	/* Usage */




static int
MyFtwProc(const FtwInfoPtr ftwip)
{
	char tstr[64], sstr[64];
	struct tm lt;
	const char *linkto;
	char *llinkto;

	if ((gLimitMaxDepth != 0) && (ftwip->depth >= gLimitMaxDepth)) {
		fprintf(stdout, "Aborting -- reached your max depth limit (%u).\n", (unsigned int) ftwip->depth);
		return (-1);
	}

	if (*((int *) ftwip->userdata) == 'l') {
		strftime(tstr, sizeof(tstr), "%Y-%m-%d %H:%M:%S", Localtime(ftwip->curStat.st_mtime, &lt));
		sprintf(sstr, PRINTF_LONG_LONG, ftwip->curStat.st_size);
		fprintf(stdout, "%s | %12s bytes | ", tstr, sstr);
	}

	if (ftwip->curType == 'd') {
		/* Directory file */
		fprintf(stdout, "%s%s", ftwip->curPath, "/");
	} else if (ftwip->curType == 'l') {
		/* Symbolic link
		 *
		 * The library has FTPFtw() set the rlinkto field
		 * since it has that information handy at the time.
		 *
		 * For local Ftw(), we have to readlink() the link
		 * ourselves if we want that information.
		 */
		llinkto = NULL;
		linkto = NULL;
		if (ftwip->rlinkto != NULL) {
			linkto = ftwip->rlinkto;
#ifdef HAVE_READLINK
		} else {
			llinkto = calloc(ftwip->curStat.st_size + 2, (size_t) 1);
			if (llinkto != NULL) {
				(void) readlink(ftwip->curPath, llinkto, ftwip->curStat.st_size + 1);
				linkto = llinkto;
			}
#endif
		}
		if (linkto != NULL) {
			fprintf(stdout, "%s -> %s", ftwip->curPath, linkto);
		} else {
			fprintf(stdout, "%s%s", ftwip->curPath, "@");
		}
		if (llinkto != NULL)
			free(llinkto);
	} else /* if (ftwip->curType == '-') */ {
		/* Regular file */
		fprintf(stdout, "%s%s", ftwip->curPath, "");
	}

	if (gDebugExtra != 0) {
		fprintf(stdout, " | len=%u | %s | len=%u",
			(unsigned int) ftwip->curPathLen,
			ftwip->curFile,
			(unsigned int) ftwip->curFileLen
		);
		if (ftwip->curPathLen != strlen(ftwip->curPath))
			fprintf(stdout, " | *** PathLen != strlen(Path) *** ");
		if (ftwip->curFileLen != strlen(ftwip->curFile))
			fprintf(stdout, " | *** FileLen != strlen(File) *** ");
		if ((strchr(ftwip->curFile, '/') != NULL) || (strchr(ftwip->curFile, '\\') != NULL))
			fprintf(stdout, " | *** File contains dirsep *** ");
	}

	fprintf(stdout, "\n");

	if ((gLimitMaxDirs != 0) && (ftwip->numDirs >= gLimitMaxDirs)) {
		fprintf(stdout, "Aborting -- reached your max dirs limit (%u).\n", (unsigned int) ftwip->numDirs);
		return (-1);
	}
	if ((gLimitMaxFiles != 0) && (ftwip->numFiles >= gLimitMaxFiles)) {
		fprintf(stdout, "Aborting -- reached your max files limit (%u).\n", (unsigned int) ftwip->numFiles);
		return (-1);
	}
	return (0);
}	/* MyFtwProc */




main_void_return_t
main(int argc, char **argv)
{
	int c;
	char url[256];
	char urlfile[128];
	int i;
	FTPLineList cdlist;
	FTPLinePtr lp;
	int rc;
	FtwInfo ftwi;
	int myflags;
	int summary;
	GetoptInfo opt;

	InitWinsock();
#if (defined(SOCKS)) && (SOCKS >= 5)
	SOCKSinit(argv[0]);
#endif	/* SOCKS */

	rc = FTPInitLibrary(&gLib);
	if (rc < 0) {
		(void) fprintf(stderr, "ncftpftw: init library error %d (%s).\n", rc, FTPStrError(rc));
		DisposeWinsock();
		exit(1);
	}
	rc = FTPInitConnectionInfo(&gLib, &fi, kDefaultFTPBufSize);
	if (rc < 0) {
		(void) fprintf(stderr, "ncftpftw: init connection info error %d (%s).\n", rc, FTPStrError(rc));
		DisposeWinsock();
		exit(1);
	}

	urlfile[0] = '\0';
	InitLineList(&cdlist);
	FtwInit(&ftwi);
	myflags = '1';
	ftwi.userdata = &myflags;
	summary = 0;

	GetoptReset(&opt);
	while ((c = Getopt(&opt, argc, argv, "1D:d:EFln:N:z")) > 0) switch(c) {
		case 'd':
			if (strcmp(opt.arg, "stdout") == 0)
				fi.debugLog = stdout;
			else if (opt.arg[0] == '-')
				fi.debugLog = stdout;
			else if (strcmp(opt.arg, "stderr") == 0)
				fi.debugLog = stderr;
			else
				fi.debugLog = fopen(opt.arg, "w");
			gDebugExtra = 1;
			break;
		case 'E':
			fi.dataPortMode = kSendPortMode;
			break;
		case 'F':
			fi.dataPortMode = kPassiveMode;
			break;
		case '1':
			myflags = '1';	/* as in "ls -1" */
			break;
		case 'l':
			myflags = 'l';	/* as in "ls -l" */
			break;
		case 'z':
			summary = 1;
			break;
		case 'D':
			gLimitMaxDepth = atoi(opt.arg);
			break;
		case 'N':
			gLimitMaxDirs = atoi(opt.arg);
			break;
		case 'n':
			gLimitMaxFiles = atoi(opt.arg);
			break;
		default:
			Usage();
	}
	if (opt.ind > argc - 1)
		Usage();

	for (i=opt.ind; i<argc; i++) {
		(void) STRNCPY(url, argv[i]);
		rc = FTPDecodeURL(&fi, url, &cdlist, urlfile, sizeof(urlfile), (int *) 0, NULL);
		(void) STRNCPY(url, argv[i]);
		if (rc == kMalformedURL) {
			(void) fprintf(stderr, "Malformed URL: %s\n", url);
			continue;
		} else if (rc == kNotURL) {
			/* Local Directory? */
			gDirToTraverse = NULL;
			(void) Dynscpy(&gDirToTraverse, argv[i], 0);
			StrRemoveTrailingSlashes(gDirToTraverse);
			if (fi.debugLog != NULL)
				(void) fprintf(fi.debugLog, "Traversing [%s]\n", gDirToTraverse);
			if ((rc = Ftw(&ftwi, gDirToTraverse, MyFtwProc)) != 0) {
				perror(gDirToTraverse);
			}
		} else if (urlfile[0] != '\0') {
			/* It not obviously a directory. */
			(void) fprintf(stderr, "Not a directory URL: %s\n", url);
			continue;
		} else {
			/* FTP Directory URL */
			gDirToTraverse = NULL;
			for (lp = cdlist.first; lp != NULL; lp = lp->next) {
				if (gDirToTraverse == NULL) {
					(void) Dynscpy(&gDirToTraverse, lp->line, 0);
				} else {
					(void) Dynscat(&gDirToTraverse, "/", lp->line, 0);
				}
			}
			if (gDirToTraverse == NULL)
				(void) Dynscpy(&gDirToTraverse, ".", 0);

			if ((rc = FTPOpenHost(&fi)) < 0) {
				(void) fprintf(stderr, "ncftpftw: cannot open %s: %s.\n", fi.host, FTPStrError(rc));
				continue;
			}

			if (fi.debugLog != NULL)
				(void) fprintf(fi.debugLog, "Traversing [%s] on %s\n", gDirToTraverse, fi.actualHost);
			
			if ((rc = FTPFtw(&fi, &ftwi, gDirToTraverse, MyFtwProc)) != 0) {
				FTPPerror(&fi, fi.errNo, kErrCWDFailed, "ncftpftw: Could not traverse directory", NULL);
			}
			(void) FTPCloseHost(&fi);
		}

		if (summary != 0) {
			(void) fprintf(stdout, "--- Summary for %s: rc=%d #dirs=%u #files=%u #links=%u depth=%u ---\n", gDirToTraverse, rc, (unsigned int) ftwi.numDirs, (unsigned int) ftwi.numFiles, (unsigned int) ftwi.numLinks, (unsigned int) ftwi.maxDepth);
		}
		free(gDirToTraverse);
	}

	FtwDispose(&ftwi);
	DisposeWinsock();
	exit((rc == 0) ? 0 : 1);
}	/* main */
