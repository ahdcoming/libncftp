/* gpshare.c
 *
 * Shared routines for ncftpget and ncftpput.
 */

#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
#	define _CRT_SECURE_NO_WARNINGS 1
#endif

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include <ncftp.h>				/* Library header. */
#include <Strn.h>				/* Library header. */

#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
#else	/* UNIX */
#	include <pwd.h>
#endif

#include "gpshare.h"

int gFirewallType;
char gFirewallHost[64];
char gFirewallUser[32];
char gFirewallPass[32];
char gFirewallExceptionList[256];
unsigned int gFirewallPort;
char gOurDirectoryPath[256];

#ifdef HAVE_GETDOMAINNAME
#if defined(SOLARIS) || defined(HPUX)
extern int getdomainname(char *name, int namelen);
#endif
#endif

/* This will abbreviate a string so that it fits into max characters.
 * It will use ellipses as appropriate.  Make sure the string has
 * at least max + 1 characters allocated for it.
 */
void
AbbrevStr(char *dst, const char *src, size_t max, int mode)
{
	int len;

	len = (int) strlen(src);
	if (len > (int) max) {
		if (mode == 0) {
			/* ...Put ellipses at left */
			(void) strcpy(dst, "...");
			(void) Strncat(dst, src + len - (int) max + 3, max + 1);
		} else {
			/* Put ellipses at right... */
			(void) Strncpy(dst, src, max + 1);
			(void) strcpy(dst + max - 3, "...");
		}
	} else {
		(void) Strncpy(dst, src, max + 1);
	}
}	/* AbbrevStr */




double
FileSize(double size, const char **uStr0, double *uMult0)
{
	double uMult, uTotal;
	const char *uStr;

	/* The comparisons below may look odd, but the reason
	 * for them is that we only want a maximum of 3 digits
	 * before the decimal point.  (I.e., we don't want to
	 * see "1017.2 kB", instead we want "0.99 MB".
	 */
	if (size > (999.5 * kGigabyte)) {
		uStr = "TB";
		uMult = kTerabyte;
	} else if (size > (999.5 * kMegabyte)) {
		uStr = "GB";
		uMult = kGigabyte;
	} else if (size > (999.5 * kKilobyte)) {
		uStr = "MB";
		uMult = kMegabyte;
	} else if (size > 999.5) {
		uStr = "kB";
		uMult = 1024;
	} else {
		uStr = "B";
		uMult = 1;
	}
	if (uStr0 != NULL)
		*uStr0 = uStr;
	if (uMult0 != NULL)
		*uMult0 = uMult;
	uTotal = size / ((double) uMult);
	if (uTotal < 0.0)
		uTotal = 0.0;
	return (uTotal);
}	/* FileSize */




void
PrSizeAndRateMeter(const FTPCIPtr cip, int mode)
{
	double rate;
	const char *rStr;
	char localName[32];
	char line[128];
	size_t i;

	switch (mode) {
		case kPrInitMsg:
			if (cip->expectedSize != kSizeUnknown) {
				cip->progress = PrStatBar;
				PrStatBar(cip, mode);
				return;
			}

			if (cip->lname == NULL) {
				localName[0] = '\0';
			} else {
				AbbrevStr(localName, cip->lname, sizeof(localName) - 2, 0);
				(void) STRNCAT(localName, ":");
			}

			(void) fprintf(stderr, "%-32s", localName);
			break;

		case kPrUpdateMsg:
			rate = FileSize(cip->kBytesPerSec * 1024.0, &rStr, NULL);

			if (cip->lname == NULL) {
				localName[0] = '\0';
			} else {
				AbbrevStr(localName, cip->lname, sizeof(localName) - 2, 0);
				(void) STRNCAT(localName, ":");
			}

#ifdef PRINTF_LONG_LONG_LLD
			(void) sprintf(line, "%-32s  %10lld bytes  %6.2f %s/s",
				localName,
				(longest_int) (cip->bytesTransferred + cip->startPoint),
				rate,
				rStr
			);
#elif defined(PRINTF_LONG_LONG_QD)
			(void) sprintf(line, "%-32s  %10qd bytes  %6.2f %s/s",
				localName,
				(longest_int) (cip->bytesTransferred + cip->startPoint),
				rate,
				rStr
			);
#else
			(void) sprintf(line, "%-32s  %10ld bytes  %6.2f %s/s",
				localName,
				(long) (cip->bytesTransferred + cip->startPoint),
				rate,
				rStr
			);
#endif

			/* Pad the rest of the line with spaces, to erase any
			 * stuff that might have been left over from the last
			 * update.
			 */
			for (i=strlen(line); i < 80 - 2; i++)
				line[i] = ' ';
			line[i] = '\0';

			/* Print the updated information. */
			(void) fprintf(stderr, "\r%s", line);
			break;

		case kPrEndMsg:
			(void) fprintf(stderr, "\n\r");
			break;
	}
}	/* PrSizeAndRateMeter */




void
PrStatBar(const FTPCIPtr cip, int mode)
{
	double rate, done;
	int secLeft, minLeft;
	const char *rStr;
	static const char *uStr;
	static double uTotal, uMult;
	const char *stall;
	char localName[80];
	char line[128];
	size_t i;

	switch (mode) {
		case kPrInitMsg:
			fflush(stdout);
			if (cip->expectedSize == kSizeUnknown) {
				cip->progress = PrSizeAndRateMeter;
				PrSizeAndRateMeter(cip, mode);
				return;
			}
			uTotal = FileSize((double) cip->expectedSize, &uStr, &uMult);

			if (cip->lname == NULL) {
				localName[0] = '\0';
			} else {
				/* Leave room for a ':' and '\0'. */
				AbbrevStr(localName, cip->lname, sizeof(localName) - 2, 0);
				(void) STRNCAT(localName, ":");
			}
			(void) fprintf(stderr, "%-32s", localName);
			break;

		case kPrUpdateMsg:
			secLeft = (int) (cip->secLeft + 0.5);
			minLeft = secLeft / 60;
			secLeft = secLeft - (minLeft * 60);
			if (minLeft > 999) {
				minLeft = 999;
				secLeft = 59;
			}

			rate = FileSize(cip->kBytesPerSec * 1024.0, &rStr, NULL);
			done = (double) (cip->bytesTransferred + cip->startPoint) / uMult;

			if (cip->lname == NULL) {
				localName[0] = '\0';
			} else {
				AbbrevStr(localName, cip->lname, 31, 0);
				(void) STRNCAT(localName, ":");
			}

			if (cip->stalled < 2)
				stall = " ";
			else if (cip->stalled < 15)
				stall = "-";
			else
				stall = "=";

			(void) sprintf(line, "%-32s   ETA: %3d:%02d  %6.2f/%6.2f %-2.2s  %6.2f %.2s/s %.1s",
				localName,
				minLeft,
				secLeft,
				done,
				uTotal,
				uStr,
				rate,
				rStr,
				stall
			);

			/* Print the updated information. */
			(void) fprintf(stderr, "\r%s", line);
			break;

		case kPrEndMsg:

			rate = FileSize(cip->kBytesPerSec * 1024.0, &rStr, NULL);
			done = (double) (cip->bytesTransferred + cip->startPoint) / uMult;

			if (cip->expectedSize >= (cip->bytesTransferred + cip->startPoint)) {
				if (cip->lname == NULL) {
					localName[0] = '\0';
				} else {
					AbbrevStr(localName, cip->lname, 52, 0);
					(void) STRNCAT(localName, ":");
				}

				(void) sprintf(line, "%-53s  %6.2f %-2.2s  %6.2f %.2s/s  ",
					localName,
					uTotal,
					uStr,
					rate,
					rStr
				);
			} else {
				if (cip->lname == NULL) {
					localName[0] = '\0';
				} else {
					AbbrevStr(localName, cip->lname, 45, 0);
					(void) STRNCAT(localName, ":");
				}

				(void) sprintf(line, "%-46s  %6.2f/%6.2f %-2.2s  %6.2f %.2s/s  ",
					localName,
					done,
					uTotal,
					uStr,
					rate,
					rStr
				);
			}

			/* Pad the rest of the line with spaces, to erase any
			 * stuff that might have been left over from the last
			 * update.
			 */
			for (i=strlen(line); i < 80 - 2; i++)
				line[i] = ' ';
			line[i] = '\0';

			/* Print the updated information. */
			(void) fprintf(stderr, "\r%s\n\r", line);
			break;
	}
}	/* PrStatBar */




void
ReadConfigFile(const char *fn, FTPCIPtr cip)
{
	FILE *fp;
	char line[128];
	char *cp;
	int goodfile = 0;

	fp = fopen(fn, "r");
	if (fp == NULL) {
		perror(fn);
		exit(kExitBadConfigFile);
	}

	line[sizeof(line) - 1] = '\0';
	while (fgets(line, sizeof(line) - 1, fp) != NULL) {
		if ((line[0] == '#') || (isspace((int) line[0])))
			continue;
		cp = line + strlen(line) - 1;
		if (*cp == '\n')
			*cp = '\0';
		if (strncmp(line, "user", 4) == 0) {
			(void) STRNCPY(cip->user, line + 5);
			goodfile = 1;
		} else if (strncmp(line, "password", 8) == 0) {
			(void) STRNCPY(cip->pass, line + 9);
			goodfile = 1;
		} else if ((strncmp(line, "pass", 4) == 0) && (isspace((int) line[4]))) {
			(void) STRNCPY(cip->pass, line + 5);
			goodfile = 1;
		} else if (strncmp(line, "host", 4) == 0) {
			(void) STRNCPY(cip->host, line + 5);
			goodfile = 1;
		} else if ((strncmp(line, "acct", 4) == 0) && (isspace((int) line[4]))) {
			(void) STRNCPY(cip->acct, line + 5);
		} else if (strncmp(line, "account", 7) == 0) {
			(void) STRNCPY(cip->acct, line + 8);
		}
	}
	(void) fclose(fp);

	if (goodfile == 0) {
		(void) fprintf(stderr, "%s doesn't contain anything useful.\n", fn);
		(void) fprintf(stderr, "%s should look something like this:\n", fn);
		(void) fprintf(stderr, "# Comment lines starting with a hash character\n# and blank lines are ignored.\n\n");
		(void) fprintf(stderr, "host Bozo.probe.net\n");
		(void) fprintf(stderr, "user gleason\n");
		(void) fprintf(stderr, "pass mypasswd\n");
		exit(kExitBadConfigFile);
	}
}	/* ReadConfigFile */




/* Create, if necessary, a directory in the user's home directory to
 * put our incredibly important stuff in.
 */
void
InitOurDirectory(void)
{
	struct stat st;
	char *cp;

	cp = getenv("NCFTPDIR");
	if (cp != NULL) {
		(void) STRNCPY(gOurDirectoryPath, cp);
	} else {
		GetHomeDir(gOurDirectoryPath, sizeof(gOurDirectoryPath));
		if (strcmp(gOurDirectoryPath, "/") == 0) {
			/* Don't create it if you're root and your home directory
			 * is the root directory.
			 *
			 * If you are root and you want to store your ncftp
			 * config files, move your home directory somewhere else,
			 * such as /root or /home/root.
			 */
			gOurDirectoryPath[0] = '\0';
			return;
		}

#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
		if (gOurDirectoryPath[strlen(gOurDirectoryPath) - 1] == LOCAL_PATH_DELIM)
			(void) STRNCAT(gOurDirectoryPath, LOCAL_PATH_DELIM_STR "ncftp");
		else
			(void) STRNCAT(gOurDirectoryPath, LOCAL_PATH_DELIM_STR "ncftp");
#else
		(void) STRNCAT(gOurDirectoryPath, LOCAL_PATH_DELIM_STR ".ncftp");
#endif

		if (stat(gOurDirectoryPath, &st) < 0) {
			if (MkDirs(gOurDirectoryPath, 00755) < 0) {
				gOurDirectoryPath[0] = '\0';
			}
		}
	}
}	/* InitOurDirectory */





void
LoadFirewallPrefs(void)
{
	FILE *fp;
	char pathName[256];
	char line[256];
	char *tok1, *tok2;
	int n;
#ifdef HAVE_GETDOMAINNAME
	char dom[64];
#endif /* HAVE_GETDOMAINNAME */

	if (gOurDirectoryPath[0] == '\0')
		return;		/* Don't create in root directory. */
	(void) STRNCPY(pathName, gOurDirectoryPath);
	(void) STRNCAT(pathName, "/firewall");

	gFirewallType = kFirewallNotInUse;
	gFirewallPort = 0;
	gFirewallHost[0] = '\0';
	gFirewallUser[0] = '\0';
	gFirewallPass[0] = '\0';
	gFirewallExceptionList[0] = '\0';

	fp = fopen(pathName, "r");
	if (fp == NULL) {
		/* Dont't create a blank one. */
		return;
	} else {
		/* Opened the firewall preferences file. */
		line[sizeof(line) - 1] = '\0';
		while (fgets(line, sizeof(line) - 1, fp) != NULL) {
			tok1 = strtok(line, " =\t\r\n");
			if ((tok1 == NULL) || (tok1[0] == '#'))
				continue;
			tok2 = strtok(NULL, " \t\r\n");
			if (tok2 == NULL)
				continue;
			if (!strcmp(tok1, "firewall-type")) {
				n = atoi(tok2);
				if ((n >= 0) && (n <= kFirewallLastType))
					gFirewallType = n;
			} else if (!strcmp(tok1, "firewall-host")) {
				(void) STRNCPY(gFirewallHost, tok2);
			} else if (!strcmp(tok1, "firewall-port")) {
				n = atoi(tok2);
				if (n > 0)
					gFirewallPort = (unsigned int) n;
			} else if (!strcmp(tok1, "firewall-user")) {
				(void) STRNCPY(gFirewallUser, tok2);
			} else if (!strcmp(tok1, "firewall-pass")) {
				(void) STRNCPY(gFirewallPass, tok2);
			} else if (!strcmp(tok1, "firewall-password")) {
				(void) STRNCPY(gFirewallPass, tok2);
			} else if (!strcmp(tok1, "firewall-exception-list")) {
				(void) STRNCPY(gFirewallExceptionList, tok2);
			}
		}
		(void) fclose(fp);
	}

#ifdef HAVE_GETDOMAINNAME
	if (gFirewallExceptionList[0] == '\0') {
		dom[sizeof(dom) - 1] = '\0';
		if (getdomainname(dom, sizeof(dom) - 1) == 0) {
			gFirewallExceptionList[0] = '.';
			gFirewallExceptionList[1] = '\0';
			(void) STRNCAT(gFirewallExceptionList, dom);
			(void) STRNCAT(gFirewallExceptionList, ",localdomain");
		}
	}
#endif /* HAVE_GETDOMAINNAME */
}	/* LoadFirewallPrefs */




int
MayUseFirewall(const char *const hn)
{
#ifdef HAVE_STRSTR
	char buf[256];
	char *tok;
	char *parse;
#endif /* HAVE_STRSTR */

	if (gFirewallType == kFirewallNotInUse)
		return (0);

	if (gFirewallExceptionList[0] == '\0') {
		if (strchr(hn, '.') == NULL) {
			/* Unqualified host name,
			 * assume it is in local domain.
			 */
			return (0);
		} else {
			return (1);
		}
	}

	if (strchr(hn, '.') == NULL) {
		/* Unqualified host name,
		 * assume it is in local domain.
		 *
		 * If "localdomain" is in the exception list,
		 * do not use the firewall for this host.
		 */
		(void) STRNCPY(buf, gFirewallExceptionList);
		for (parse = buf; (tok = strtok(parse, ", \n\t\r")) != NULL; parse = NULL) {
			if (strcmp(tok, "localdomain") == 0)
				return (0);
		}
		/* fall through */
	}

#ifdef HAVE_STRSTR
	(void) STRNCPY(buf, gFirewallExceptionList);
	for (parse = buf; (tok = strtok(parse, ", \n\t\r")) != NULL; parse = NULL) {
		/* See if host or domain was from exclusion list
		 * matches the host to open.
		 */
		if (strstr(hn, tok) != NULL)
			return (0);
	}
#endif
	return (1);
}	/* MayUseFirewall */



void
SetRedial(const FTPCIPtr cip, const char *const argstr)
{
	char buf[256];
	char *tok;
	char *parse;
	int nt = 0;
	int i;

	(void) STRNCPY(buf, argstr);
	for (parse = buf; (tok = strtok(parse, ", \n\t\r")) != NULL; parse = NULL) {
		nt++;
		if (nt == 1) {
			if (strcmp(tok, "forever") == 0)
				cip->maxDials = -1;
			else {
				i = atoi(tok);
				if (i == 0)
					cip->maxDials = 1;
				else
					cip->maxDials = i;
			}
		} else if (nt == 2) {
			i = atoi(tok);
			if (i < 2)
				i = 2;
			cip->redialDelay = i;
		}
	}
}	/* SetRedial */



void
SetTimeouts(const FTPCIPtr cip, const char *const argstr)
{
	char buf[256];
	char *tok;
	char *parse;
	int nt = 0;

	(void) STRNCPY(buf, argstr);
	for (parse = buf; (tok = strtok(parse, ", \n\t\r")) != NULL; parse = NULL) {
		nt++;
		if (nt == 1) {
			cip->xferTimeout = atoi(tok);
			cip->connTimeout = atoi(tok);
			cip->ctrlTimeout = atoi(tok);
		} else if (nt == 2) {
			cip->connTimeout = atoi(tok);
		} else if (nt == 3) {
			cip->ctrlTimeout = atoi(tok);
		}
	}
}	/* SetTimeouts */



int
GetDefaultProgressMeterSetting(void)
{
	int progmeters;

#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
	progmeters = _isatty(_fileno(stderr));
#else
	progmeters = ((isatty(2) != 0) && (getppid() > 1)) ? 1 : 0;
#endif
	return (progmeters);
}	/* GetDefaultProgressMeterSetting */



FILE *
OpenPager(void)
{
	FILE *fp;

#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
	fp = stdout;
#else
	const char *cp;

	cp = (const char *) getenv("PAGER");
	if (cp == NULL)
		cp = "more";
	fp = popen(cp, "w");
	if (fp == NULL)
		fp = stderr;
#endif
	return (fp);
}	/* OpenPager */



void ClosePager(FILE *fp)
{
	if ((fp == stderr) || (fp == stdout))
		return;
#if (defined(WIN32) || defined(_WINDOWS)) && !defined(__CYGWIN__)
#else
	(void) pclose(fp);
#endif
}	/* ClosePager */

/* eof */
