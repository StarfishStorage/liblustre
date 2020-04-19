#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <lustre/lustre.h>

int main(int argc, char *argv[])
{
    char *ll_version;

    ll_version = (char *) malloc(PATH_MAX+1);

    printf("liblustre_version before %p\n", ll_version);
	ll_version = liblustre_version(ll_version);
    printf("liblustre_version after %p\n", ll_version);
    printf("Print liblustre_version %s\n", ll_version);
	lus_version();

	return 0;
}
