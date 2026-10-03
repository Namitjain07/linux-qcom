/* v4l2-enc-probe.c - negotiation-only probe of a V4L2 stateful encoder (Iris / Venus).
 *
 * Question it answers (WITHOUT starting any encode session):
 *   - which codecs / raw formats does the encoder node enumerate?
 *   - what frame-size limits does it report for H.264 / HEVC?
 *   - if the raw input is 1920x1080 and we ask for a 1280x720 coded stream, does the driver
 *     keep the two sizes different (i.e. encoder-side scaling can be requested) or does it
 *     force the coded size back to the raw size?
 *
 * Build:  gcc -O2 -Wall -o v4l2-enc-probe v4l2-enc-probe.c
 * Run:    ./v4l2-enc-probe /dev/videoN [raw_w raw_h coded_w coded_h]   (defaults 1920 1080 1280 720)
 *
 * NOTE: opening the node makes the driver load + boot the video firmware (same as any
 * client). It does not run the encoder. Order matters on Iris: OUTPUT is set first, then
 * CAPTURE, because S_FMT(OUTPUT) resets the CAPTURE size to the raw size.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/videodev2.h>

static const char *fcc(__u32 f, char out[5])
{
	for (int i = 0; i < 4; i++) {
		char c = (f >> (8 * i)) & 0xff;
		out[i] = (c >= 32 && c < 127) ? c : '?';
	}
	out[4] = 0;
	return out;
}

static void enum_formats(int fd, __u32 type, const char *name)
{
	struct v4l2_fmtdesc d;
	char b[5];

	printf("  %s formats:", name);
	for (int i = 0;; i++) {
		memset(&d, 0, sizeof(d));
		d.index = i;
		d.type = type;
		if (ioctl(fd, VIDIOC_ENUM_FMT, &d) < 0)
			break;
		printf(" %s", fcc(d.pixelformat, b));
	}
	printf("\n");
}

static void enum_sizes(int fd, __u32 pixfmt)
{
	struct v4l2_frmsizeenum s;
	char b[5];

	memset(&s, 0, sizeof(s));
	s.index = 0;
	s.pixel_format = pixfmt;
	if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &s) < 0) {
		printf("  %s frame sizes: (not reported: %s)\n", fcc(pixfmt, b), strerror(errno));
		return;
	}
	if (s.type == V4L2_FRMSIZE_TYPE_STEPWISE || s.type == V4L2_FRMSIZE_TYPE_CONTINUOUS)
		printf("  %s frame sizes: %ux%u .. %ux%u step %ux%u\n", fcc(pixfmt, b),
		       s.stepwise.min_width, s.stepwise.min_height, s.stepwise.max_width,
		       s.stepwise.max_height, s.stepwise.step_width, s.stepwise.step_height);
	else
		printf("  %s frame sizes: discrete list (first %ux%u)\n", fcc(pixfmt, b),
		       s.discrete.width, s.discrete.height);
}

static int set_fmt(int fd, __u32 type, __u32 pixfmt, unsigned w, unsigned h, struct v4l2_format *out)
{
	struct v4l2_format f;

	memset(&f, 0, sizeof(f));
	f.type = type;
	f.fmt.pix_mp.width = w;
	f.fmt.pix_mp.height = h;
	f.fmt.pix_mp.pixelformat = pixfmt;
	f.fmt.pix_mp.num_planes = 1;
	f.fmt.pix_mp.field = V4L2_FIELD_NONE;
	if (ioctl(fd, VIDIOC_S_FMT, &f) < 0)
		return -errno;
	*out = f;
	return 0;
}

int main(int argc, char **argv)
{
	unsigned rw = 1920, rh = 1080, cw = 1280, ch = 720;
	struct v4l2_capability cap;
	struct v4l2_format o, c;
	struct v4l2_selection sel;
	char b[5];
	int fd, r, scaled;

	if (argc < 2) {
		fprintf(stderr, "usage: %s /dev/videoN [raw_w raw_h coded_w coded_h]\n", argv[0]);
		return 2;
	}
	if (argc >= 6) {
		rw = atoi(argv[2]); rh = atoi(argv[3]); cw = atoi(argv[4]); ch = atoi(argv[5]);
	}

	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}
	memset(&cap, 0, sizeof(cap));
	if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
		perror("QUERYCAP");
		return 1;
	}
	printf("device: %s  driver=%s card=%s bus=%s\n", argv[1], cap.driver, cap.card, cap.bus_info);

	enum_formats(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, "OUTPUT (raw in) ");
	enum_formats(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, "CAPTURE (coded)");
	enum_sizes(fd, V4L2_PIX_FMT_H264);
	enum_sizes(fd, V4L2_PIX_FMT_HEVC);

	r = set_fmt(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_NV12, rw, rh, &o);
	if (r) {
		printf("S_FMT(OUTPUT NV12 %ux%u) failed: %s\n", rw, rh, strerror(-r));
		return 1;
	}
	printf("S_FMT OUTPUT  NV12 %ux%u  -> accepted %ux%u (stride %u, sizeimage %u)\n", rw, rh,
	       o.fmt.pix_mp.width, o.fmt.pix_mp.height, o.fmt.pix_mp.plane_fmt[0].bytesperline,
	       o.fmt.pix_mp.plane_fmt[0].sizeimage);

	r = set_fmt(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_PIX_FMT_H264, cw, ch, &c);
	if (r) {
		printf("S_FMT(CAPTURE H264 %ux%u) failed: %s\n", cw, ch, strerror(-r));
		return 1;
	}
	printf("S_FMT CAPTURE %s %ux%u  -> accepted %ux%u\n", fcc(c.fmt.pix_mp.pixelformat, b), cw, ch,
	       c.fmt.pix_mp.width, c.fmt.pix_mp.height);

	/* Re-read OUTPUT: some drivers fold a CAPTURE change back into OUTPUT. */
	memset(&o, 0, sizeof(o));
	o.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	if (ioctl(fd, VIDIOC_G_FMT, &o) == 0)
		printf("G_FMT OUTPUT  -> %ux%u\n", o.fmt.pix_mp.width, o.fmt.pix_mp.height);

	memset(&sel, 0, sizeof(sel));
	sel.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	sel.target = V4L2_SEL_TGT_CROP;
	if (ioctl(fd, VIDIOC_G_SELECTION, &sel) == 0)
		printf("G_SELECTION OUTPUT CROP -> %ux%u @ (%d,%d)\n", sel.r.width, sel.r.height, sel.r.left, sel.r.top);

	scaled = (c.fmt.pix_mp.width < o.fmt.pix_mp.width) || (c.fmt.pix_mp.height < o.fmt.pix_mp.height);
	printf("\nRESULT: driver %s a coded size smaller than the raw input (%ux%u raw vs %ux%u coded).\n",
	       scaled ? "KEPT" : "FORCED BACK", o.fmt.pix_mp.width, o.fmt.pix_mp.height,
	       c.fmt.pix_mp.width, c.fmt.pix_mp.height);
	printf("%s\n", scaled ?
	       "=> encoder-side scaling can at least be REQUESTED through V4L2 (whether the firmware scales is a separate test)." :
	       "=> no encoder-side scaling through V4L2 on this driver; scale before encoding (GPU/CPU).");
	close(fd);
	return 0;
}
