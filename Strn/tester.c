#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <Strn.h>

int main(int argc, char **argv)
{
	char a[8];
	char *b;
	char *str;
	const char *cp;
	char c[37];
	int i;
	int len1, len2;
	int failures = 0;
	DStr ds;

	b = Strncpy(a, "hello", sizeof(a));
	b = Strncat(b, "world", sizeof(a));
	if (memcmp(b, "hellowo", 7 + 1) != 0) {
		++failures;
		printf("1: fail\n");
		printf("1: result=[%s] should be=[%s]\n",
			b,
			"hellowo"
		);
	}

	for (i=0; i<(int) sizeof(c); i++)
		c[i] = 'X';
	b = Strncpy(c, "testing", sizeof(c) - 2);
#if (STRN_ZERO_PAD == 1)
	for (i=7; i<(int) sizeof(c) - 2; i++) {
		if (c[i] != '\0') {
			++failures;
			printf("2: did not clear to end of buffer\n");
			break;
		}
	}
#endif
	for (i=(int) sizeof(c) - 2; i<(int) sizeof(c); i++) {
		if (c[i] != 'X') {
			++failures;
			printf("2: overwrote buffer\n");
			break;
		}
	}

	for (i=0; i<(int) sizeof(c); i++)
		c[i] = 'X';
	b = Strncpy(c, "testing", sizeof(c) - 2);
	b = Strncat(b, " still", sizeof(c) - 2);
#if (STRN_ZERO_PAD == 1)
	for (i=13; i<(int) sizeof(c) - 2; i++) {
		if (c[i] != '\0') {
			++failures;
			printf("3: did not clear to end of buffer\n");
			break;
		}
	}
#endif
	for (i=(int) sizeof(c) - 2; i<(int) sizeof(c); i++) {
		if (c[i] != 'X') {
			++failures;
			printf("3: overwrote buffer\n");
			break;
		}
	}

/*--------------*/

	b = Strnpcpy(a, "hello", sizeof(a));
	len1 = (int) (b - a);
	b = Strnpcat(a, "world", sizeof(a));
	len2 = (int) (b - a);
	if ((len1 != 5) || (len2 != 7) || (strcmp(a, "hellowo") != 0)) {
		++failures;
		printf("4: fail\n");
		printf("4: result=[%s] should be=[%s] len1=%d len2=%d\n",
			a,
			"hellowo",
			len1,
			len2
		);
	}
	for (i=0; i<(int) sizeof(c); i++)
		c[i] = 'X';
	b = Strnpcpy(c, "testing", sizeof(c) - 2);
#if (STRNP_ZERO_PAD == 1)
	for (i=7; i<(int) sizeof(c) - 2; i++) {
		if (c[i] != '\0') {
			++failures;
			printf("5: did not clear to end of buffer\n");
			break;
		}
	}
#endif
	for (i=(int) sizeof(c) - 2; i<(int) sizeof(c); i++) {
		if (c[i] != 'X') {
			++failures;
			printf("5: overwrote buffer\n");
			break;
		}
	}

	for (i=0; i<(int) sizeof(c); i++)
		c[i] = 'X';
	b = Strnpcpy(c, "testing", sizeof(c) - 2);
	b = Strnpcat(c, " still", sizeof(c) - 2);
#if (STRNP_ZERO_PAD == 1)
	for (i=13; i<(int) sizeof(c) - 2; i++) {
		if (c[i] != '\0') {
			++failures;
			printf("6: did not clear to end of buffer\n");
			break;
		}
	}
#endif
	for (i=(int) sizeof(c) - 2; i<(int) sizeof(c); i++) {
		if (c[i] != 'X') {
			++failures;
			printf("6: overwrote buffer\n");
			break;
		}
	}

	str = NULL;
	if (Dynscat(&str, "this is a test", 0) == NULL) {
		++failures;
		printf("7a: fail\n");
	} else if (strcmp(str, "this is a test") != 0) {
		++failures;
		printf("7b: fail\n");
	}
	free(str);

	str = NULL;
	if (Dynscat(&str, "this is a test", 0) == NULL) {
		++failures;
		printf("7c: fail\n");
	} else if (strcmp(str, "this is a test") != 0) {
		++failures;
		printf("7d: fail\n");
	} else if (Dynscat(&str, " ", "", "and", " ", "so is this", 0) == NULL) {
		++failures;
		printf("7e: fail\n");
	} else if (strcmp(str, "this is a test and so is this") != 0) {
		++failures;
		printf("7f: fail\n");
	}
	free(str);

	str = NULL;
	Dynscpy(&str, "test", 0);
	if (strcmp(str, "test") != 0) {
		++failures;
		printf("8: fail\n");
	}
#if 0	/* no longer supported */
	Dynscpy(&str, "This is a ", str, ".",  0);
	if (strcmp(str, "This is a test.") != 0) {
		++failures;
		printf("9: fail\n");
	}
#endif
	free(str);

	str = NULL;
	Dynsrecpy(&str, "cruel", 0);
	Dynsrecpy(&str, "hello, ", str, " world!", 0);
	if (strcmp(str, "hello, cruel world!") != 0) {
		++failures;
		printf("10: fail\n");
	} else {
		StrFree(&str);
		if (str != NULL) {
			++failures;
			printf("11: fail\n");
		}
	}

	str = NULL;
	Dynscat(&str, "cruel", 0);
	if ((Dynscat(&str, "hello ", str, " world!") != NULL) || (str != NULL)) {
		++failures;
		printf("12: fail\n");
	}

	DStrInit(&ds);
	DStrFree(&ds);	/* Should be able to do this now */

	if (DStrNew(&ds, 20) < 0) {
		++failures;
		printf("13a: fail\n");
	} else {
		if (ds.allocSize != 32) {
			printf("13b: fail (%u)\n", ds.allocSize);
			++failures;
		}
		if (ds.len != 0) {
			printf("13c: fail (%u)\n", ds.len);
			++failures;
		}
		if ((((int) ds.s) & 1) != 0) {
			printf("13d: fail (%p)\n", ds.s);
			++failures;
		}
		DStrFree(&ds);
		if (ds.s != NULL) {
			printf("13e: fail (%p)\n", ds.s);
			++failures;
		}
	}

	cp = DStrCpy(&ds, "the quick brown fox jumped over the lazy dog");
	if (cp == NULL) {
		++failures;
		printf("14a: fail\n");
	} else {
		if (ds.allocSize != 48) {
			printf("14b: fail (%u)\n", ds.allocSize);
			++failures;
		}
		if (ds.len != 44) {
			printf("14c: fail (%u)\n", ds.len);
			++failures;
		}
		if ((((int) ds.s) & 1) != 0) {
			printf("14d: fail (%p)\n", ds.s);
			++failures;
		}
		if (strcmp(ds.s, "the quick brown fox jumped over the lazy dog") != 0) {
			printf("14e: fail (%s)\n", ds.s);
			++failures;
		}
		if (cp != ds.s) {
			printf("14f: fail (%s)\n", ds.s);
			++failures;
		}
		cp = DStrCat(&ds, " and then died of a massive heart attack");
		if (cp == NULL) {
			++failures;
			printf("15a: fail\n");
		} else {
			if (ds.allocSize != 96) {
				printf("15b: fail (%u)\n", ds.allocSize);
				++failures;
			}
			if (ds.len != 84) {
				printf("15c: fail (%u)\n", ds.len);
				++failures;
			}
			if ((((int) ds.s) & 1) != 0) {
				printf("15d: fail (%p)\n", ds.s);
				++failures;
			}
			if (strcmp(ds.s, "the quick brown fox jumped over the lazy dog and then died of a massive heart attack") != 0) {
				printf("15e: fail (%s)\n", ds.s);
				++failures;
			}
			if (cp != ds.s) {
				printf("15f: fail (%s)\n", ds.s);
				++failures;
			}
			cp = DStrCat(&ds, " (EOLN)");
			if (cp == NULL) {
				++failures;
				printf("16a: fail\n");
			} else {
				if (ds.allocSize != 96) {
					printf("16b: fail (%u)\n", ds.allocSize);
					++failures;
				}
				if (ds.len != 91) {
					printf("16c: fail (%u)\n", ds.len);
					++failures;
				}
				if ((((int) ds.s) & 1) != 0) {
					printf("16d: fail (%p)\n", ds.s);
					++failures;
				}
				if (strcmp(ds.s, "the quick brown fox jumped over the lazy dog and then died of a massive heart attack (EOLN)") != 0) {
					printf("16e: fail (%s)\n", ds.s);
					++failures;
				}
				if (cp != ds.s) {
					printf("16f: fail (%s)\n", ds.s);
					++failures;
				}
				(void) DStrCat(&ds, "123");
				if (ds.allocSize != 96) {
					printf("17a: fail (%u)\n", ds.allocSize);
					++failures;
				}
				if (ds.len != 94) {
					printf("17b: fail (%u)\n", ds.len);
					++failures;
				}
				if (strcmp(ds.s, "the quick brown fox jumped over the lazy dog and then died of a massive heart attack (EOLN)123") != 0) {
					printf("17c: fail (%s)\n", ds.s);
					++failures;
				}
				(void) DStrCat(&ds, "4");
				if (ds.allocSize != 96) {
					printf("18a: fail (%u)\n", ds.allocSize);
					++failures;
				}
				if (ds.len != 95) {
					printf("18b: fail (%u)\n", ds.len);
					++failures;
				}
				if (strcmp(ds.s, "the quick brown fox jumped over the lazy dog and then died of a massive heart attack (EOLN)1234") != 0) {
					printf("18c: fail (%s)\n", ds.s);
					++failures;
				}
				(void) DStrCat(&ds, "5");
				if (ds.allocSize != 112) {
					printf("19a: fail (%u)\n", ds.allocSize);
					++failures;
				}
				if (ds.len != 96) {
					printf("19b: fail (%u)\n", ds.len);
					++failures;
				}
				if (strcmp(ds.s, "the quick brown fox jumped over the lazy dog and then died of a massive heart attack (EOLN)12345") != 0) {
					printf("19c: fail (%s)\n", ds.s);
					++failures;
				}
			}
		}
		DStrFree(&ds);
		if (ds.s != NULL) {
			printf("14g: fail (%p)\n", ds.s);
			++failures;
		}
	}

	cp = DStrCpyList(&ds,
		"the",
		" quick",
		" brown",
		" fox",
		" jumped",
		" over",
		" the",
		" lazy",
		" dog",
		0
	);
	if (cp == NULL) {
		++failures;
		printf("20a: fail\n");
	} else {
		if (ds.allocSize != 48) {
			printf("20b: fail (%u)\n", ds.allocSize);
			++failures;
		}
		if (ds.len != 44) {
			printf("20c: fail (%u)\n", ds.len);
			++failures;
		}
		if ((((int) ds.s) & 1) != 0) {
			printf("20d: fail (%p)\n", ds.s);
			++failures;
		}
		if (strcmp(ds.s, "the quick brown fox jumped over the lazy dog") != 0) {
			printf("20e: fail (%s)\n", ds.s);
			++failures;
		}
		if (cp != ds.s) {
			printf("20f: fail (%s)\n", ds.s);
			++failures;
		}
		DStrFree(&ds);
	}

	(void) DStrCpy(&ds, "the");
	cp = DStrCatList(&ds,
		" quick",
		" brown",
		" fox",
		" jumped",
		" over",
		" the",
		" lazy",
		" dog",
		0
	);
	if (cp == NULL) {
		++failures;
		printf("21a: fail\n");
	} else {
		if (ds.allocSize != 48) {
			printf("21b: fail (%u)\n", ds.allocSize);
			++failures;
		}
		if (ds.len != 44) {
			printf("21c: fail (%u)\n", ds.len);
			++failures;
		}
		if ((((int) ds.s) & 1) != 0) {
			printf("21d: fail (%p)\n", ds.s);
			++failures;
		}
		if (strcmp(ds.s, "the quick brown fox jumped over the lazy dog") != 0) {
			printf("21e: fail (%s)\n", ds.s);
			++failures;
		}
		if (cp != ds.s) {
			printf("21f: fail (%s)\n", ds.s);
			++failures;
		}
		DStrFree(&ds);
	}

	(void) DStrCpy(&ds, "fox");
	cp = DStrCpyList(&ds,
		"the",
		" quick",
		" brown ",
		ds.s,
		" jumped",
		" over",
		" the",
		" lazy",
		" dog",
		0
	);
	if (cp == NULL) {
		++failures;
		printf("22a: fail\n");
	} else {
		if (ds.allocSize != 48) {
			printf("22b: fail (%u)\n", ds.allocSize);
			++failures;
		}
		if (ds.len != 44) {
			printf("22c: fail (%u)\n", ds.len);
			++failures;
		}
		if ((((int) ds.s) & 1) != 0) {
			printf("22d: fail (%p)\n", ds.s);
			++failures;
		}
		if (strcmp(ds.s, "the quick brown fox jumped over the lazy dog") != 0) {
			printf("22e: fail (%s)\n", ds.s);
			++failures;
		}
		if (cp != ds.s) {
			printf("22f: fail (%s)\n", ds.s);
			++failures;
		}
		DStrFree(&ds);
	}

	(void) DStrCpy(&ds, "the");
	cp = DStrCatList(&ds,
		" quick",
		" brown",
		" fox",
		" jumped",
		" over",
		" ",
		ds.s,
		" lazy",
		" dog",
		0
	);
	if (cp == NULL) {
		++failures;
		printf("23a: fail\n");
	} else {
		if (ds.allocSize != 48) {
			printf("23b: fail (%u)\n", ds.allocSize);
			++failures;
		}
		if (ds.len != 44) {
			printf("23c: fail (%u)\n", ds.len);
			++failures;
		}
		if ((((int) ds.s) & 1) != 0) {
			printf("23d: fail (%p)\n", ds.s);
			++failures;
		}
		if (strcmp(ds.s, "the quick brown fox jumped over the lazy dog") != 0) {
			printf("23e: fail (%s)\n", ds.s);
			++failures;
		}
		if (cp != ds.s) {
			printf("23f: fail (%s)\n", ds.s);
			++failures;
		}
		DStrFree(&ds);
	}

	cp = DStrCpy(&ds, "1234567890");
	if (cp == NULL) {
		++failures;
		printf("24a: fail\n");
	} else {
		cp = DStrCat(&ds, ds.s);
		if (cp == NULL) {
			++failures;
			printf("24b: fail\n");
		} else {
			if (ds.allocSize != 32) {
				printf("23c: fail (%u)\n", ds.allocSize);
				++failures;
			}
			if (ds.len != 20) {
				printf("23d: fail (%u)\n", ds.len);
				++failures;
			}
			if ((((int) ds.s) & 1) != 0) {
				printf("23e: fail (%p)\n", ds.s);
				++failures;
			}
			if (strcmp(ds.s, "12345678901234567890") != 0) {
				printf("23f: fail (%s)\n", ds.s);
				++failures;
			}
			if (cp != ds.s) {
				printf("23g: fail (%s)\n", ds.s);
				++failures;
			}
			DStrFree(&ds);
		}
	}

	printf("Done.  Number of tests failed: %d.\n", failures);
	exit(0);
}
