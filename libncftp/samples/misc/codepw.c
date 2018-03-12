/* codepw: trivial XOR encoder */

#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include <ncftp.h>				/* Library header. */

main_void_return_t
main(int c, char **argv)
{
	int i;
	int ch;
	unsigned char q[64];

	if (c < 3) {
		fprintf(stderr, "Usage:   codepw <password> <XOR-key-to-scramble-with>\n");
		fprintf(stderr, "Example: codepw my.pass helloworld\n");
		exit(1);
	}
	Scramble(q, sizeof(q), (unsigned char *) argv[1], argv[2]);
	printf("char const password[] = \"");
	for (i=0; ;i++) {
		ch = q[i];
		if (ch == 0)
			break;
		if (isprint(ch))
			printf("%c", ch);
		else printf("\\%03o", ch);
	}
	printf("\";\n");
	exit(0);
}
