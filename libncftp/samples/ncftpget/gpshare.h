/* gpshare.h
 *
 * Shared routines for ncftpget and ncftpput.
 */

typedef enum ExitStatus {
	kExitSuccess = 0,
	kExitOpenFailed = 1,
	kExitOpenTimedOut,
	kExitXferFailed,
	kExitXferTimedOut,
	kExitChdirFailed,
	kExitChdirTimedOut,
	kExitMalformedURL,
	kExitUsage,
	kExitBadConfigFile,
	kExitInitLibraryFailed,
	kExitInitConnInfoFailed
} ExitStatus;

#define kKilobyte 1024
#define kMegabyte (kKilobyte * 1024)
#define kGigabyte ((long) kMegabyte * 1024L)
#define kTerabyte ((double) kGigabyte * 1024.0)

#ifndef STDIN_FILENO
#	define STDIN_FILENO 0
#	define STDOUT_FILENO 1
#	define STDERR_FILENO 2
#endif

/* gpshare.c */
void AbbrevStr(char *, const char *, size_t, int);
double FileSize(double, const char **, double *);
void PrSizeAndRateMeter(const FTPCIPtr, int);
void PrStatBar(const FTPCIPtr, int);
void ReadConfigFile(const char *, FTPCIPtr);
void InitOurDirectory(void);
void LoadFirewallPrefs(void);
int MayUseFirewall(const char *const);
void SetRedial(const FTPCIPtr, const char *const);
void SetTimeouts(const FTPCIPtr, const char *const);
char *GetPass2(const char *const prompt);
int GetDefaultProgressMeterSetting(void);
FILE *OpenPager(void);
void ClosePager(FILE *fp);
