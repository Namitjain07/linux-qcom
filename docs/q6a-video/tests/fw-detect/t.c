#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
typedef uint8_t u8;
#define min(a,b) ((a)<(b)?(a):(b))
static size_t strscpy(char *d, const char *s, size_t n){ size_t i=0; for(;i+1<n && s[i];i++) d[i]=s[i]; d[i]=0; return i; }
#include FN
/* Place `payload` so that it ends exactly at the end of a mapping that is followed by a PROT_NONE guard page. */
static int run(const char *name, const char *payload, int tail_pad, bool expect)
{
	size_t pg = 4096, len = strlen(payload) + tail_pad;
	u8 *base = mmap(NULL, 3*pg, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	mprotect(base + 2*pg, pg, PROT_NONE);              /* guard page right after the data */
	u8 *blob = base + 2*pg - len;
	memset(blob, 'x', len); memcpy(blob, payload, strlen(payload));
	bool got = iris_detect_gen2_from_fwdata(blob, len);
	printf("%-44s -> %-5s (expected %s)%s\n", name, got?"Gen2":"Gen1", expect?"Gen2":"Gen1", got==expect?"":"  MISMATCH");
	return got != expect;
}
int main(void){
	int bad = 0;
	bad |= run("vfw tag, string at start",                 "QC_IMAGE_VERSION_STRING=vfw-3.4:rel0059\0", 100, true);
	bad |= run("video-firmware.1.0 (Gen1)",                "QC_IMAGE_VERSION_STRING=video-firmware.1.0-ed457c18\0", 100, false);
	bad |= run("video-firmware.2.1 (Gen2)",                "QC_IMAGE_VERSION_STRING=video-firmware.2.1\0", 100, true);
	bad |= run("no marker",                                "nothing to see here\0", 100, false);
	bad |= run("marker + 'vfw' ending exactly at blob end", "QC_IMAGE_VERSION_STRING=vfw", 0, true);
	bad |= run("marker + 'video-firmware.1' unterminated",  "QC_IMAGE_VERSION_STRING=video-firmware.1", 0, false);
	return bad;
}
