/* unlstest.c
 * 
 * This program tests the library's ls output parser.
 * Capture some output then feed it to this program
 * as stdin, and see if it is correctly parsed and
 * reformatted on stdout.
 */

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include <ncftp.h>				/* Library header. */
#include <Strn.h>				/* Library header. */

/* The program keeps a timestamp of 6 months ago and an hour from now, because
 * the standard /bin/ls command will print the time (i.e. "Nov  8 09:20")
 * instead of the year (i.e. "Oct 27  1996") if a file's timestamp is within
 * this period.
 */
static time_t gNowMinus6Mon = 0, gNowPlus1Hr = 0;

/* An array of month name abbreviations.  This may not be in English. */
static char gLsMon[13][4];

extern int FTPAllocateHost(const FTPCIPtr cip);


static void
Usage(void)
{
	fprintf(stderr, "Usage:  unlstest unix|dos|mlsd < ls_output.txt\n");
	fprintf(stderr, "\nLibrary version: %s.\n", gLibNcFTPVersion + 5);
#ifdef UNAME
	fprintf(stderr, "System: %s.\n", UNAME);
#endif
	exit(2);
}	/* Usage */




/* Creates the ls monthname abbreviation array, so we don't have to
 * re-calculate them each time.
 */
static void
InitLsMonths(void)
{
	time_t now;
	struct tm *ltp;
	int i;

	(void) time(&now);
	ltp = localtime(&now);	/* Fill up the structure. */
	ltp->tm_mday = 15;
	ltp->tm_hour = 12;
	for (i=0; i<12; i++) {
		ltp->tm_mon = i;
		(void) strftime(gLsMon[i], sizeof(gLsMon[i]), "%b", ltp);
		gLsMon[i][sizeof(gLsMon[i]) - 1] = '\0';
	}
	(void) strcpy(gLsMon[i], "BUG");
}	/* InitLsMonths */




/* Converts a timestamp into a recent date string ("May 27 06:33"), or an
 * old (or future) date string (i.e. "Oct 27  1996").
 */
static void
LsDate(char *dstr, time_t ts)
{
	struct tm *gtp;

	if (ts == kModTimeUnknown) {
		(void) strcpy(dstr, "            ");
		return;
	}
	gtp = localtime(&ts);
	if (gtp == NULL) {
		(void) strcpy(dstr, "Jan  0  1900");
		return;
	}
	if ((ts > gNowPlus1Hr) || (ts < gNowMinus6Mon)) {
		(void) sprintf(dstr, "%s %2d  %4d",
			gLsMon[gtp->tm_mon],
			gtp->tm_mday,
			gtp->tm_year + 1900
		);
	} else {
		(void) sprintf(dstr, "%s %2d %02d:%02d",
			gLsMon[gtp->tm_mon],
			gtp->tm_mday,
			gtp->tm_hour,
			gtp->tm_min
		);
	}
}	/* LsDate */




/* Does "ls -l", or the detailed /bin/ls-style, one file per line . */
static void
LsL(FTPFileInfoListPtr dirp, int endChars, int linkedTo, FILE *stream)
{
	FTPFileInfoPtr diritemp;
	FTPFileInfoVec diritemv;
	int i;
	char fTail[2];
	int fType;
	const char *l1, *l2;
	char datestr[16];
	char sizestr[32];
	char plugspec[16];
	char plugstr[64];
	const char *expad;

	fTail[0] = '\0';
	fTail[1] = '\0';

	(void) time(&gNowPlus1Hr);
	gNowMinus6Mon = gNowPlus1Hr - (time_t) 15552000;
	gNowPlus1Hr += 3600;

	diritemv = dirp->vec;
#ifdef HAVE_SNPRINTF
	(void) snprintf(
		plugspec,
		sizeof(plugspec) - 1,
#else
	(void) sprintf(
		plugspec,
#endif
		"%%-%us",
		(unsigned int) dirp->maxPlugLen
	);

	if (dirp->maxPlugLen < 29) {
		/* We have some extra space to work with,
		 * so we can space out the columns a little.
		 */
		expad = "  ";
	} else {
		expad = "";
	}

	for (i=0; ; i++) {
		diritemp = diritemv[i];
		if (diritemp == NULL)
			break;

		fType = (int) diritemp->type;
		if (endChars != 0) {
			if (fType == 'd')
				fTail[0] = '/';
			else
				fTail[0] = '\0';
		}

		if (diritemp->rlinkto != NULL) {
			if (linkedTo != 0) {
				l1 = "";
				l2 = "";
			} else {
				l1 = " -> ";
				l2 = diritemp->rlinkto;
			}
		} else {
			l1 = "";
			l2 = "";
		}

		LsDate(datestr, diritemp->mdtm);

		if (diritemp->size == kSizeUnknown) {
			*sizestr = '\0';
		} else {
#ifdef HAVE_SNPRINTF
			(void) snprintf(
				sizestr,
				sizeof(sizestr) - 1,
#else
			(void) sprintf(
				sizestr,
#endif
#if defined(HAVE_LONG_LONG) && defined(PRINTF_LONG_LONG)
				PRINTF_LONG_LONG,
#else
				"%ld",
#endif
				(longest_int) diritemp->size
			);
		}

#ifdef HAVE_SNPRINTF
		(void) snprintf(
			plugstr,
			sizeof(plugstr) - 1,
#else
		(void) sprintf(
			plugstr,
#endif
			plugspec,
			diritemp->plug
		);

		(void) fprintf(stream, "%s %12s %s%s %s%s%s%s%s\n",
			plugstr,
			sizestr,
			expad,
			datestr,
			expad,
			diritemp->relname,
			l1,
			l2,
			fTail
		);
	}
}	/* LsL */



main_void_return_t
main(int argc, char **argv)
{
	int result;
	FTPLibraryInfo li;
	FTPConnectionInfo ci;
	FTPLineList ll;
	FTPFileInfoList fil;
	FILE *fp;
	char line[256];
	int nc;

	result = FTPInitLibrary(&li);
	if (result < 0) {
		fprintf(stderr, "unlstest: init library error %d (%s).\n", result, FTPStrError(result));
		exit(4);
	}

	result = FTPInitConnectionInfo(&li, &ci, kDefaultFTPBufSize);
	if (result < 0) {
		fprintf(stderr, "unlstest: init connection info error %d (%s).\n", result, FTPStrError(result));
		exit(8);
	}

	/* Normally this next step is done automatically upon FTPOpenHost, but
	 * we are not doing that for this sample program.
	 */
	result = FTPAllocateHost(&ci);
	if (result < 0) {
		fprintf(stderr, "unlstest: memory allocation error %d (%s).\n", result, FTPStrError(result));
		exit(8);
	}

	if (argc < 2)
		Usage();

	fp = stdin;
	if (argc > 2) {
		fp = fopen(argv[2], "r");
		if (fp == NULL) {
			perror(argv[2]);
			exit(1);
		}
	}

	InitLineList(&ll);
	while (FGets(line, sizeof(line), fp) != NULL) {
		AddLine(&ll, line);
	}

	if (fp != stdin)
		fclose(fp);

	InitFileInfoList(&fil);
	nc = -1;
	if (strcmp(argv[1], "unix") == 0) {
		nc = UnLslR(&ci, &fil, &ll, kServerTypeUnknown);
		printf("Number of UNIX ls lines converted: %d\n", nc);
	} else if (strcmp(argv[1], "dos") == 0) {
		nc = UnLslR(&ci, &fil, &ll, kServerTypeMicrosoftFTP);
		printf("Number of MS-DOS ls lines converted: %d\n", nc);
	} else if (strcmp(argv[1], "mlsd") == 0) {
		nc = UnMlsD(&ci, &fil, &ll);
		printf("Number of MLSD lines converted: %d\n", nc);
	} else {
		Usage();
	}

	InitLsMonths();
	if (nc >= 0) {
		VectorizeFileInfoList(&fil);
		if (fil.vec == NULL) {
			(void) fprintf(stderr, "List processing failed.\n");
			exit(1);
		}
		printf("\n");
		LsL(&fil, 1, 0, stdout);
	}

	DisposeLineListContents(&ll);
	DisposeFileInfoListContents(&fil);
	exit(0);
}	/* main */
