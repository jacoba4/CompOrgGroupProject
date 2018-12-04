#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>










int main(int argc, char* argv[])
{
	// input file START
	if (argc != 3)
	{
		fprintf(stderr, "ERROR: incorrect number of inputs\n");
		return EXIT_FAILURE;
	}

	if (argv[1] == NULL)
	{
		fprintf(stderr, "ERROR: provide forwarding setting\n");
		return EXIT_FAILURE;
	}

	if (argv[1][0] != 'F' && argv[1][0] != 'N')
	{
		fprintf(stderr, "ERROR: invalid forwarding setting");
		return EXIT_FAILURE;
	}

	if (argv[2] == NULL)
	{
		fprintf(stderr, "ERROR: provide a file name\n");
		return EXIT_FAILURE;
	}

	// open file
	char * input;
	input = calloc(128, sizeof(char));
	input = argv[2];
	FILE *file;
	file = fopen(input, "r");
	if (file == NULL)
	{
		fprintf(stderr, "ERROR: cannot open file\n");
		return EXIT_FAILURE;
	}
	// input file END







	return EXIT_SUCCESS;
}