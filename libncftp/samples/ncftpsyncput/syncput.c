/* NcFTPSyncPut.c
 *
 * Utility to synchronize a remote site.  This version only does incremental
 * updates by comparing recursive local directory listing to a previous listing.
 * This utility does not consider the actual contents of the remote directory,
 * so if you change files directly on the server this utility will not know
 * that those files have been changed.
 */

#define VERSION "1.0.0"

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

#ifdef HAVE_UTIME_H
#	include <utime.h>
#else
	struct utimbuf { time_t actime, modtime; };
#endif

FTPLibraryInfo gLib;
FTPConnectionInfo gConn;

char gLDir[256];
char gRDir[256];
char gRDirActual[256];
char gCatalogFile[256];
char *gCatalogFileNameOnly;	/* filename only */
char gDebugCurCatalogFile[256];
char gConfigFileNameOnly[80];	/* filename only */
char gUmaskStr[16];
int gDeleteRemoteFiles = 1;
#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
int gRPathsToLowercase = 0;
#endif
FTPFileInfoList gFiles, gFilesToDelete, gPrevCatalog, gCurCatalog;
char gTextExts[256] = ".txt;.html;.htm;.xml;.ini;.sh;.pl;.hqx;.cfg;.c;.h;.cpp;.hpp;.ps;.bat;.m3u;.pls";

extern int gFirewallType;
extern char gFirewallHost[64];
extern char gFirewallUser[32];
extern char gFirewallPass[32];
extern unsigned int gFirewallPort;


static void
PruneUnwantedFilesFromFileList(FTPFileInfoListPtr filp)
{
	FTPFileInfoPtr fip, nextfip;
	char *cp;

	for (fip = filp->first; fip != NULL; fip = nextfip) {
		nextfip = fip->next;
		cp = StrRFindLocalPathDelim(fip->lname);
		if (cp == NULL)
			cp = fip->lname;
		else
			cp++;
		if ((fip->type != '-') && (fip->type != 'l'))
			nextfip = RemoveFileInfo(filp, fip);
		else if (strcasecmp(cp, gConfigFileNameOnly) == 0)
			nextfip = RemoveFileInfo(filp, fip);
		else if (strcasecmp(fip->lname, gCatalogFileNameOnly) == 0)
			nextfip = RemoveFileInfo(filp, fip);
#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
		else if (gRPathsToLowercase != 0) {
			_strlwr(fip->lname);
			_strlwr(fip->relname);
		}
#endif
	}
}	/* PruneUnwantedFilesFromFileList */




static void
PrintFileList(FTPFileInfoListPtr filp)
{
	FTPFileInfoPtr fip;
	struct tm *ltp, lt;
	int i, n;
	char mdtmstr[64];
	
	n = filp->nFileInfos;
	for (i=0; i<n; i++) {
		fip = filp->vec[i];
		ltp = Localtime(fip->mdtm, &lt);
		if (ltp == NULL)
			STRNCPY(mdtmstr, "(modtime unknown)");
		else
			strftime(mdtmstr, sizeof(mdtmstr), "%Y-%m-%d %H:%M:%S", ltp);
		printf("%c  %s  size=%-10d  %s\n",
			fip->type,
			mdtmstr,
			(int) fip->size,
			fip->lname
		);
	}
}	/* PrintFileList */




static int
BreadthFirstCaseCmp(const void *a, const void *b)
{
	char *cp, *cpa, *cpb;
	int depth, deptha, depthb;
	int c;
	const FTPFileInfo *const *fipa;
	const FTPFileInfo *const *fipb;

	fipa = (const FTPFileInfo *const *) a;
	fipb = (const FTPFileInfo *const *) b;

	cpa = (**fipa).relname;
	cpb = (**fipb).relname;

	for (cp = cpa, depth = 0;;) {
		c = *cp++;
		if (c == '\0')
			break;
		if ((c == '/') || (c == '\\')) {
			depth++;
		}
	}
	deptha = depth;

	for (cp = cpb, depth = 0;;) {
		c = *cp++;
		if (c == '\0')
			break;
		if ((c == '/') || (c == '\\')) {
			depth++;
		}
	}
	depthb = depth;

	if (deptha < depthb)
		return (-1);
	else if (deptha > depthb)
		return (1);

#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
	/* Paths are generally not case-sensitive. */
	return (_stricmp(cpa, cpb));
#elif defined(HAVE_SETLOCALE)
	return (strcoll(cpa, cpb));
#else
	return (strcmp(cpa, cpb));
#endif
}	/* BreadthFirstCaseCmp */




static void
LoadCatalogFromFile(const char *const catalogFileName, FTPFileInfoListPtr const filp)
{
	FILE *fp;
	char line[1024];
	char *cp;
	char *tokstart;
	FTPFileInfo fi;
	int havesz, havemt;
	unsigned int mt;

	InitFileInfoList(filp);
	fp = fopen(
		catalogFileName,
#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
		"rt"
#else
		"r"
#endif
		);

	if (fp == NULL) {
		/* No previous catalog -- this is OK,
		 * since this could be the first time
		 * doing an update.
		 */
		return;
	}

	InitFileInfo(&fi);
	havesz = 0;
	havemt = 0;

	memset(line, 0, sizeof(line));
	while (fgets(line, sizeof(line) - 1, fp) != NULL) {
		cp = line;
		while (*cp && isspace((int) *cp))
			cp++;

		if ((*cp == '\0') || (cp[0] == '#') || (cp[0] == '[') || (cp[0] == ';') || (isspace((int) cp[0])))
			continue;
		tokstart = cp;
		cp = line + strlen(line) - 1;
		if (*cp == '\n') {
			*cp-- = '\0';
			if (*cp == '\r')
				*cp-- = '\0';
		}

		if (strncasecmp(tokstart, "File: ",  6) == 0) {
			InitFileInfo(&fi);
			cp = tokstart + 6;
			fi.relnameLen = strlen(cp);
			fi.relname = StrDup(cp);
			fi.lname = StrDup(cp);
			fi.type = '-';
			havesz = 0;
			havemt = 0;
		} else if (strncasecmp(tokstart, "Size: ",  6) == 0) {
			cp = tokstart + 6;
			(void) sscanf(cp, SCANF_LONG_LONG, (longest_int *) &fi.size);
			havesz++;
		} else if (strncasecmp(tokstart, "Date: ",  6) == 0) {
			cp = tokstart + 6;
			(void) sscanf(cp, "%u", (unsigned int *) &mt);
			fi.mdtm = (time_t) mt;
			havemt++;
		}

		if ((fi.relnameLen != 0) && (havesz != 0) && (havemt != 0)) {
			(void) AddFileInfo(filp, &fi);

			/* The FileInfo and its StrDup'ed contents
			 * are now property of the list and will be
			 * disposed of when the list is disposed of.
			 */

			/* Clear out the structure for re-use. */
			InitFileInfo(&fi);
		}
	}
	(void) fclose(fp);

	PruneUnwantedFilesFromFileList(filp);
	VectorizeFileInfoList(filp);
	if (filp->nFileInfos > 1) {
		qsort(filp->vec, (size_t) filp->nFileInfos,
			sizeof(FTPFileInfoPtr), BreadthFirstCaseCmp);
	}
}	/* LoadCatalogFromFile */




static void
LoadPreviousCatalogFile(void)
{
	LoadCatalogFromFile(gCatalogFile, &gPrevCatalog);
}	/* LoadPreviousCatalogFile */




static void
ComputeCurrentCatalog(void)
{
	FTPLineList directories;
	int result;

	InitFileInfoList(&gCurCatalog);
	InitLineList(&directories);
	AddLine(&directories, gLDir);
	result = FTPLocalRecursiveFileList2(&gConn, &directories, &gCurCatalog, 1);
	if (result < 0) {
		(void) fprintf(stderr, "NcFTPSyncPut: could not traverse %s: %s.\n", gLDir, FTPStrError(result));
		DisposeWinsock();
		DisposeLineListContents(&directories);
		exit(1);
	}
	DisposeLineListContents(&directories);

	PruneUnwantedFilesFromFileList(&gCurCatalog);
	VectorizeFileInfoList(&gCurCatalog);
	if (gCurCatalog.nFileInfos > 1) {
		qsort(gCurCatalog.vec, (size_t) gCurCatalog.nFileInfos,
			sizeof(FTPFileInfoPtr), BreadthFirstCaseCmp);
	}
}	/* ComputeCurrentCatalog */




static int
CopyFileInfoAndAddToFileList(FTPFileInfoPtr lp, FTPFileInfoListPtr filp)
{
	FTPFileInfo newfi;

	newfi = *lp;
	newfi.relname = StrDup(lp->relname);
	newfi.lname = StrDup(lp->lname);
	newfi.rname = StrDup(lp->rname);
	newfi.rlinkto = StrDup(lp->rlinkto);
	newfi.plug = StrDup(lp->plug);
	if (AddFileInfo(filp, &newfi) == NULL)
		return (-1);
	return 0;
}	/* CopyFileInfoAndAddToFileList */




static void
CollectListOfChangedFiles(void)
{
	FTPFileInfoPtr prevfip, curfip;
	int iPrev, iCur;
	int cmprc;

	InitFileInfoList(&gFiles);
	InitFileInfoList(&gFilesToDelete);

	LoadPreviousCatalogFile();
	if (gDebugCurCatalogFile[0] != '\0')
		LoadCatalogFromFile(gDebugCurCatalogFile, &gCurCatalog);	/* for testing */
	else
		ComputeCurrentCatalog();

	/* We now have both the old catalog and current catalog
	 * ready to compare.  We next need to create a new list
	 * containing just the list of files that we need to
	 * update.
	 */

	iPrev = 0;
	iCur = 0;

	for (;;) {
		if (gCurCatalog.vec == NULL)
			break;	/* done */

		curfip = gCurCatalog.vec[iCur];
		if (curfip == NULL)
			break;	/* done */
		
		for (;;) {
			if (
				(gPrevCatalog.vec == NULL) ||
				((prevfip = gPrevCatalog.vec[iPrev]) == NULL)
			) {
				/* Prev list did not have this file. */
				(void) CopyFileInfoAndAddToFileList(curfip, &gFiles);
				break;
			}
		
			cmprc = BreadthFirstCaseCmp(&prevfip, &curfip);
			if (cmprc == 0) {
				/* Prev list _did_ have the file.
				 * Now compare it by time/size.
				 */
				if ((prevfip->mdtm != curfip->mdtm) || (prevfip->size != curfip->size))
					(void) CopyFileInfoAndAddToFileList(curfip, &gFiles);
				iPrev++;
				break;
			} else if (cmprc < 0) {
				/* Current list did not have file.
				 * That means the file was deleted
				 * since last run.
				 */
				(void) CopyFileInfoAndAddToFileList(prevfip, &gFilesToDelete);
				iPrev++;
				/* continue... */
			} else /* if (cmprc > 0) */ {
				/* Previous list did not have this file.
				 * That means we need to upload this file
				 * which had been added since the last run.
				 */
				(void) CopyFileInfoAndAddToFileList(curfip, &gFiles);
				break;
			}
		}

		iCur++;
	}

	for (;;) {
		if (gPrevCatalog.vec == NULL)
			break;

		prevfip = gPrevCatalog.vec[iPrev];
		if (prevfip == NULL) {
			break;
		}

		/* Current list did not have file.
		 * That means the file was deleted
		 * since last run.
		 */
		(void) CopyFileInfoAndAddToFileList(prevfip, &gFilesToDelete);
		iPrev++;
	}

	VectorizeFileInfoList(&gFiles);
	if (gFiles.nFileInfos > 1) {
		qsort(gFiles.vec, (size_t) gFiles.nFileInfos,
			sizeof(FTPFileInfoPtr), BreadthFirstCaseCmp);
	}

	VectorizeFileInfoList(&gFilesToDelete);
	if (gFilesToDelete.nFileInfos > 1) {
		qsort(gFilesToDelete.vec, (size_t) gFilesToDelete.nFileInfos,
			sizeof(FTPFileInfoPtr), BreadthFirstCaseCmp);
	}
}	/* CollectListOfChangedFiles */




static void
Usage(void)
{
	FILE *fp;

	fp = OpenPager();
	(void) fprintf(fp, "NcFTPSyncPut %s.\n\n", VERSION);
	(void) fprintf(fp, "Usage:\n");
	(void) fprintf(fp, "  NcFTPSyncPut [flags] config.file\n");
	(void) fprintf(fp, "\nFlags:\n\
  -d XX  Use the file XX for debug logging.\n\
  -UU    Update catalog file only, no uploading.\n\
  -t XX  Timeout after XX seconds.\n");
	(void) fprintf(fp, "\
  -v/-V  Do (do not) use progress meters.\n\
  -F     Use passive (PASV) data connections.\n\
  -y     Try using \"SITE UTIME\" to preserve timestamps on remote host.\n");
	(void) fprintf(fp, "\nExample minimal configuration file:\n\
  [main]\n\
  host=www.example.com\n\
  user=gleason\n\
  pass=mypassword\n\
  ldir=/home/gleason/reports\n\
  rdir=/users/g/gleason/public_html/report_dir\n\
  catalog-file=/home/gleason/files/catalog.file\n");
	(void) fprintf(fp, "\n\
Other config file options:\n\
  debug-log                (same as \"-d\"; default is off)\n\
  passive                  (yes/no, default: no)\n\
  sync-deletes             (yes/no, default: yes)\n\
  port                     (default: 21)\n\
  umask                    (default depends on server)\n\
  textexts                 (default: %s)\n", gTextExts);
#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
	(void) fprintf(fp, "\n\
  pathnames-to-lowercase   (default: no)\n");
#endif
	(void) fprintf(fp, "\n\
Note: If you choose to put the config file and/or the timestamp\n\
      file in the directory to synchronize, NcFTPSyncPut will _not_\n\
      upload those files _if_ they are detected.\n");
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
	gConn.cancelXfer++;

	/* If the user appears to be getting impatient,
	 * restore the default signal handler so the
	 * next ^C abends the program.
	 */
	if (gConn.cancelXfer >= 2)
		signal(sigNum, SIG_DFL);
}	/* Abort */




static int
UpdateCatalogFile(void)
{
	FILE *fp;
	FTPFileInfoPtr fip;
	int i, n;
	char mdtmstr[64];

	fp = fopen(
		gCatalogFile,
#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
		"wt"
#else
		"w"
#endif
		);
	if (fp == NULL) {
		perror(gCatalogFile);
		return (-1);
	}

	n = gCurCatalog.nFileInfos;
	for (i=0; i<n; i++) {
		fip = gCurCatalog.vec[i];
		(void) strftime(mdtmstr, sizeof(mdtmstr), "%Y-%m-%d %H:%M:%S", localtime(&fip->mdtm));
		(void) fprintf(fp, "File: %s\nSize: ", fip->relname);
		(void) fprintf(fp, PRINTF_LONG_LONG, fip->size);
		(void) fprintf(fp, "\nDate: %lu (%s)\n\n", (unsigned long) fip->mdtm, mdtmstr);
		if (fflush(fp) < 0) {
			(void) fclose(fp);
			return (-1);
		}
	}

	return (0);
}	/* UpdateCatalogFile */




static int
GetFileTypeBasedOffOfExtension(const char *const fname)
{
	char buf[256], *tok, extbuf[16];
	const char *delims;
	const char *ext;
	int len;
	int i;


	ext = strrchr(fname, '.');
	if (ext == NULL)
		return (kTypeBinary);

	ext++;
	STRNCPY(extbuf, ext);
	for (i=0, len=(int) strlen(extbuf); i<len; i++)
		if (isupper((int) extbuf[i]))
			extbuf[i] = (char) tolower(extbuf[i]);

	STRNCPY(buf, gTextExts);
	for (i=0, len=(int) strlen(buf); i<len; i++)
		if (isupper((int) buf[i]))
			buf[i] = (char) tolower(buf[i]);

	delims = ".;, \t\r\n";
	for (tok = strtok(buf, delims); tok != NULL; tok = strtok(NULL, delims)) {
		if (strcmp(tok, ext) == 0)
			return (kTypeAscii);
	}

	return (kTypeBinary);
}	/* GetFileTypeBasedOffOfExtension */





static int
CopyFiles(void)
{
	FTPFileInfoPtr fip;
	int i, n;
	int result;
	int xtype;
	char *cp, relDir[256], curRelDir[256];

	STRNCPY(curRelDir, "");
	n = gFiles.nFileInfos;
	if (n < 1)
		return (0);

	for (i=0; i<n; i++) {
		fip = gFiles.vec[i];

#if 0
		printf("\n\n%c  %-10lu  size=%-10d  %s\n",
			fip->type,
			fip->mdtm,
			(int) fip->size,
			fip->lname
		);
#endif
		STRNCPY(relDir, fip->relname);
		cp = StrRFindLocalPathDelim(relDir);
		if (cp != NULL)
			*cp = '\0';
		else
			relDir[0] = '\0';

		if (strcmp(relDir, curRelDir) != 0) {
			/* This file was in a different
			 * directory than the one before.
			 * 
			 * We now need to change back to
			 * our starting directory, and then
			 * change to each sub-node in the
			 * new relative directory.
			 */
			result = FTPChdir(&gConn, gRDirActual);
			if (result != 0) {
				(void) fprintf(stderr, "NcFTPSyncPut: chdir back to %s failed: %s.\n", gRDirActual, FTPStrError(result));
				return (-1);
			}
			STRNCPY(curRelDir, relDir);
			if (curRelDir[0] != '\0')
				result = FTPChdir3(&gConn, curRelDir, NULL, 0, (kChdirOneSubdirAtATime|kChdirAndMkdir));
			if (result != 0) {
				(void) fprintf(stderr, "NcFTPSyncPut: chdir to %s failed: %s.\n", curRelDir, FTPStrError(result));
				return (-1);
			}
		}

		if (fip->type == 'l') {
			if ((fip->rlinkto != NULL) && (fip->relname != NULL)) {
				cp = StrRFindLocalPathDelim(fip->relname);
				if (cp == NULL)
					cp = fip->relname;
				else
					cp++;
				(void) FTPSymlink(&gConn, fip->rlinkto, cp);
			}
		} else {
			xtype = GetFileTypeBasedOffOfExtension(fip->lname);
			result = FTPPutFiles3(&gConn, fip->lname, ".", kRecursiveNo, kGlobNo,
				xtype, kAppendNo, NULL, ".tmp", kResumeNo, kDeleteNo,
				kNoFTPConfirmResumeUploadProc, 0);
			if (result != 0) {
				(void) fprintf(stderr, "NcFTPSyncPut: file send error for %s: %s.\n", fip->lname, FTPStrError(result));
				return (result);
			}
		}
	}

	n = gFilesToDelete.nFileInfos;
	if ((n < 1) || (gDeleteRemoteFiles == 0))
		return (0);

	if (gConn.startingWorkingDirectory == NULL) {
		(void) fprintf(stderr, "NcFTPSyncPut: could not change back to starting directory.\n");
		(void) fprintf(stderr, "This is required to synchronize remote deletions.\n");
		/* Don't reupload everything everytime... */
		return (0);
	}
	return 0;
}	/* CopyFiles */




static int
DeleteFiles(void)
{
	FTPFileInfoPtr fip;
	int i, n;
	int result;
	char *cp, *fname, relDir[256], curRelDir[256];

	STRNCPY(curRelDir, "");
	n = gFilesToDelete.nFileInfos;
	if (n < 1)
		return (0);

	for (i=0; i<n; i++) {
		fip = gFilesToDelete.vec[i];

#if 0
		printf("\n\n%c  %-10lu  size=%-10d  %s\n",
			fip->type,
			fip->mdtm,
			(int) fip->size,
			fip->lname
		);
#endif
		relDir[0] = '\0';
		cp = StrFindLocalPathDelim(fip->relname);
		if (cp != NULL) {
			STRNCPY(relDir, fip->relname);
			relDir[(int) (cp - fip->relname)] = '\0';
			fname = cp + 1;
		} else {
			fname = fip->relname;
		}

		if (strcmp(relDir, curRelDir) != 0) {
			/* This file was in a different
			 * directory than the one before.
			 * 
			 * We now need to change back to
			 * our starting directory, and then
			 * change to each sub-node in the
			 * new relative directory.
			 */
			result = FTPChdir(&gConn, gRDirActual);
			if (result != 0) {
				(void) fprintf(stderr, "NcFTPSyncPut: chdir back to %s failed: %s.\n", gRDirActual, FTPStrError(result));
				return (-1);
			}
			STRNCPY(curRelDir, relDir);
			if (curRelDir[0] != '\0')
				result = FTPChdir3(&gConn, curRelDir, NULL, 0, (kChdirOneSubdirAtATime));
			if (result != 0) {
				/* Assume parent directory (and the file) have been deleted. */
				continue;
			}
		}

		if (fip->type == 'd')
			continue;	/* We only handle 'l' and '-' (links, files) */
		result = FTPDelete(&gConn, fname, kRecursiveNo, kGlobNo);
		if (result != 0) {
			(void) fprintf(stderr, "NcFTPSyncPut: remote delete of %s failed: %s.\n", fip->lname, FTPStrError(result));
			continue;
		}
	}

	return 0;
}	/* DeleteFiles */




static int 
StrToBool(const char *const s)
{
	int c;
	int result;
	
	c = *s;
	if (isupper(c))
		c = tolower(c);
	result = 0;
	switch (c) {
		case 'f':			       /* false */
		case 'n':			       /* no */
			break;
		case 'o':			       /* test for "off" and "on" */
			c = (int) s[1];
			if (isupper(c))
				c = tolower(c);
			if (c == 'f')
				break;
			result = 1;
			break;
		case 't':			       /* true */
		case 'y':			       /* yes */
			result = 1;
			break;
		default:			       /* 1, 0, -1, other number? */
			if (atoi(s) != 0)
				result = 1;
	}
	return result;
}						       /* StrToBool */



static void
SetDebugLog(const char *const fn)
{
	if ((gConn.debugLog != NULL) && (gConn.debugLog != stderr) && (gConn.debugLog != stdout)) {
		(void) fclose(gConn.debugLog);
	}
	if (strcmp(fn, "stdout") == 0)
		gConn.debugLog = stdout;
	else if (fn[0] == '-')
		gConn.debugLog = stdout;
	else if (strcmp(fn, "stderr") == 0)
		gConn.debugLog = stderr;
	else
		gConn.debugLog = fopen(
			fn,
#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
			"wt"
#else
			"w"
#endif
		);
}	/* SetDebugLog */



static void
ReadSyncputConfigFile(const char *fn, FTPCIPtr cip)
{
	FILE *fp;
	char line[128];
	char *cp, *tokstart;

#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
#else
	(void) chmod(fn, 0600);
#endif

	cp = StrRFindLocalPathDelim(fn);
	STRNCPY(gConfigFileNameOnly, cp ? (cp + 1) : fn);

	fp = fopen(fn,
#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
		"rt"
#else
		"r"
#endif
		);
	if (fp == NULL) {
		perror(fn);
		exit(kExitBadConfigFile);
	}

	line[sizeof(line) - 1] = '\0';
	while (fgets(line, sizeof(line) - 1, fp) != NULL) {
		cp = line;
		while (*cp && isspace((int) *cp))
			cp++;

		if ((cp[0] == '#') || (cp[0] == '[') || (cp[0] == ';') || (isspace((int) cp[0])))
			continue;
		tokstart = cp;
		cp = line + strlen(line) - 1;
		if (*cp == '\n') {
			*cp-- = '\0';
			if (*cp == '\r')
				*cp-- = '\0';
		}
		if (strncmp(tokstart, "user", 4) == 0) {
			(void) STRNCPY(cip->user, tokstart + 5);
		} else if (strncmp(tokstart, "password", 8) == 0) {
			(void) STRNCPY(cip->pass, tokstart + 9);
		} else if ((strncmp(tokstart, "pass", 4) == 0) && ((isspace((int) tokstart[4])) || (tokstart[4] == '='))) {
			(void) STRNCPY(cip->pass, tokstart + 5);
		} else if (strncmp(tokstart, "host", 4) == 0) {
			(void) STRNCPY(cip->host, tokstart + 5);
		} else if (strncmp(tokstart, "port", 4) == 0) {
			cip->port = atoi(tokstart + 5);
		} else if ((strncmp(tokstart, "acct", 4) == 0) && ((isspace((int) tokstart[4])) || (tokstart[4] == '='))) {
			(void) STRNCPY(cip->acct, tokstart + 5);
		} else if (strncmp(tokstart, "account", 7) == 0) {
			(void) STRNCPY(cip->acct, tokstart + 8);
		} else if (strncmp(tokstart, "ldir", 4) == 0) {
			(void) STRNCPY(gLDir, tokstart + 5);
		} else if (strncmp(tokstart, "rdir", 4) == 0) {
			(void) STRNCPY(gRDir, tokstart + 5);
		} else if (strncmp(tokstart, "catalog-file", 12) == 0) {
			(void) STRNCPY(gCatalogFile, tokstart + 13);
			cp = StrRFindLocalPathDelim(gCatalogFile);
			if (cp == NULL)
				gCatalogFileNameOnly = gCatalogFile;
			else
				gCatalogFileNameOnly = cp + 1;
		} else if (strncmp(tokstart, "cur-catalog-file", 16) == 0) {
			(void) STRNCPY(gDebugCurCatalogFile, tokstart + 17);
		} else if (strncmp(tokstart, "textexts", 8) == 0) {
			(void) STRNCPY(gTextExts, tokstart + 9);
		} else if (strncmp(tokstart, "umask", 5) == 0) {
			(void) STRNCPY(gUmaskStr, tokstart + 6);
		} else if (strncmp(tokstart, "sync-deletes", 12) == 0) {
			gDeleteRemoteFiles = StrToBool(tokstart + 13);
#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
		} else if (strncmp(tokstart, "pathnames-to-lowercase", 22) == 0) {
			gRPathsToLowercase = StrToBool(tokstart + 23);
#endif
		} else if (strncmp(tokstart, "passive", 7) == 0) {
			if (StrToBool(tokstart + 8))
				cip->dataPortMode = kPassiveMode;
			else
				cip->dataPortMode = kSendPortMode;
		} else if (strncmp(tokstart, "debug-log", 9) == 0) {
			SetDebugLog(tokstart + 10);
		}
	}
	(void) fclose(fp);
}	/* ReadSyncputConfigFile */



main_void_return_t
main(int argc, char **argv)
{
	int result, c;
	ExitStatus es;
	int progmeters;
	int tryUtime = 1;
	int showListOnly = 0;
	int updateListOnly = 0;
	GetoptInfo opt;

	InitWinsock();
	result = FTPInitLibrary(&gLib);
	if (result < 0) {
		(void) fprintf(stderr, "NcFTPSyncPut: init library error %d (%s).\n", result, FTPStrError(result));
		DisposeWinsock();
		exit(kExitInitLibraryFailed);
	}
	result = FTPInitConnectionInfo(&gLib, &gConn, kDefaultFTPBufSize);
	if (result < 0) {
		(void) fprintf(stderr, "NcFTPSyncPut: init connection info error %d (%s).\n", result, FTPStrError(result));
		DisposeWinsock();
		exit(kExitInitConnInfoFailed);
	}

	memset(gCatalogFile, 0, sizeof(gCatalogFile));
	memset(gDebugCurCatalogFile, 0, sizeof(gDebugCurCatalogFile));
	memset(gLDir, 0, sizeof(gLDir));
	memset(gRDir, 0, sizeof(gRDir));
	memset(gUmaskStr, 0, sizeof(gUmaskStr));

	gConn.xferTimeout = 60 * 60;
	gConn.connTimeout = 30;
	gConn.ctrlTimeout = 135;
	gConn.debugLog = NULL;
	gConn.errLog = stderr;
	(void) STRNCPY(gConn.user, "anonymous");
	progmeters = GetDefaultProgressMeterSetting();

	GetoptReset(&opt);
	while ((c = Getopt(&opt, argc, argv, "P:u:p:e:d:t:vUVFyl")) > 0) switch(c) {
		case 'P':
			gConn.port = atoi(opt.arg);	
			break;
		case 'u':
			(void) STRNCPY(gConn.user, opt.arg);
			break;
		case 'p':
			(void) STRNCPY(gConn.pass, opt.arg);	/* Don't recommend doing this! */
			break;
		case 'e':
			if (strcmp(opt.arg, "stdout") == 0)
				gConn.errLog = stdout;
			else if (opt.arg[0] == '-')
				gConn.errLog = stdout;
			else if (strcmp(opt.arg, "stderr") == 0)
				gConn.errLog = stderr;
			else
				gConn.errLog = fopen(opt.arg, "a");
			break;
		case 'd':
			SetDebugLog(opt.arg);
			break;
		case 't':
			SetTimeouts(&gConn, opt.arg);
			break;
		case 'v':
			progmeters = 1;
			break;
		case 'V':
			progmeters = 0;
			break;
		case 'F':
			if (gConn.dataPortMode == kPassiveMode)
				gConn.dataPortMode = kSendPortMode;
			else
				gConn.dataPortMode = kPassiveMode;
			break;
		case 'y':
			tryUtime = 1;
			break;
		case 'U':
			updateListOnly++;
			break;
		case 'l':
			showListOnly = 1;
			break;
		default:
			Usage();
	}

	if (opt.ind > argc - 1)
		Usage();
	ReadSyncputConfigFile(argv[opt.ind], &gConn);

	if (gLDir[0] == '\0') {
		fprintf(stderr, "You haven't specified a local directory.\n");
		Usage();
	}
	StrRemoveTrailingSlashes(gLDir);

	if (gRDir[0] == '\0') {
		fprintf(stderr, "You haven't specified a remote directory.\n");
		Usage();
	}
	StrRemoveTrailingSlashes(gRDir);

	if (gCatalogFile[0] == '\0') {
		fprintf(stderr, "You haven't specified a catalog file.\n");
		Usage();
	}

	if (gConn.host[0] == '\0') {
		fprintf(stderr, "You haven't specified a remote host.\n");
		Usage();
	}

	InitOurDirectory();
	LoadFirewallPrefs();

	if ((strcmp(gConn.user, "anonymous") != 0) && (strcmp(gConn.user, "ftp") != 0) && (gConn.pass[0] == '\0'))
		(void) GetPass("Password: ", gConn.pass, sizeof(gConn.pass));

	if (progmeters != 0)
		gConn.progress = PrStatBar;
	if ((tryUtime == 0) && (gConn.hasSITE_UTIME < 1))
		gConn.hasSITE_UTIME = 0;

	if (MayUseFirewall(gConn.host) != 0) {
		gConn.firewallType = gFirewallType; 
		(void) STRNCPY(gConn.firewallHost, gFirewallHost);
		(void) STRNCPY(gConn.firewallUser, gFirewallUser);
		(void) STRNCPY(gConn.firewallPass, gFirewallPass);
		gConn.firewallPort = gFirewallPort;
	}


	CollectListOfChangedFiles();
	if ((gFiles.nFileInfos < 1) && (gFilesToDelete.nFileInfos < 1)) {
		(void) fprintf(stdout, "No files have been added, removed, or changed since the last update.\n");
		es = kExitSuccess;
		DisposeWinsock();
		exit((int) es);
	}

	if (showListOnly != 0) {
		if (gFiles.nFileInfos > 0) {
			(void) fprintf(stdout, "New or changed files:\n");
			PrintFileList(&gFiles);
		}
		if (gFilesToDelete.nFileInfos > 0) {
			(void) fprintf(stdout, "\nDeleted files:\n");
			PrintFileList(&gFilesToDelete);
		}
		es = kExitSuccess;
		DisposeWinsock();
		exit((int) es);
	}

	if (updateListOnly >= 2) {
		/* -UU */
		UpdateCatalogFile();
		es = kExitSuccess;
		DisposeWinsock();
		exit((int) es);
	}

	es = kExitOpenTimedOut;
	if ((result = FTPOpenHost(&gConn)) < 0) {
		(void) fprintf(stderr, "NcFTPSyncPut: cannot open %s: %s.\n", gConn.host, FTPStrError(result));
		es = kExitOpenFailed;
		DisposeWinsock();
		exit((int) es);
	}
	if (gUmaskStr[0] != '\0') {
		result = FTPUmask(&gConn, gUmaskStr);
		if (result != 0)
			(void) fprintf(stderr, "NcFTPSyncPut: umask failed: %s.\n", FTPStrError(result));
	}

	result = FTPChdir3(&gConn, gRDir, gRDirActual, sizeof(gRDirActual), (kChdirAndMkdir|kChdirAndGetCWD|kChdirOneSubdirAtATime));
	if (result != 0) {
			(void) fprintf(stderr, "NcFTPSyncPut: chdir %s failed: %s.\n", gRDir, FTPStrError(result));
	}

	if (result >= 0) {
		es = kExitXferTimedOut;
		(void) signal(SIGINT, Abort);
		if (CopyFiles() == 0) {
			DeleteFiles();
			es = kExitSuccess;
		}
	}

	(void) FTPCloseHost(&gConn);

	if ((es == kExitSuccess) && (UpdateCatalogFile() < 0))
		es = kExitBadConfigFile;

	DisposeWinsock();
	exit((int) es);
}	/* main */
