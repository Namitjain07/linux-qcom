/* v4l2-enc-neg-test.c - negotiation self-test for the PATCHED Iris encoder (no encode is started).
 *
 * Each check below corresponds to behaviour changed by the kernel patch series in ../patches/
 * (see ../FIXES.md). A check prints PASS when the driver behaves as the patched driver should and
 * FAIL when it behaves like the unpatched 7.0.11 driver, so the same binary tells you which kernel
 * is running. The unpatched result is quoted next to each check.
 *
 * Build:  gcc -O2 -Wall -o v4l2-enc-neg-test v4l2-enc-neg-test.c
 * Run:    ./v4l2-enc-neg-test /dev/videoN        (the Iris *encoder* node; ./v4l2-enc-probe lists nodes' names)
 * Risk:   T1 - opening the node boots the VPU firmware, exactly like any client. No buffers are
 *         allocated and nothing is streamed.
 * Exit:   0 = all PASS, 1 = at least one FAIL, 2 = could not run.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/videodev2.h>

static int fails;

static void check(int ok, const char *what, const char *unpatched)
{
	printf("%-5s %-62s [unpatched: %s]\n", ok ? "PASS" : "FAIL", what, unpatched);
	if (!ok)
		fails++;
}

static int s_fmt(int fd, __u32 type, __u32 pixfmt, unsigned w, unsigned h, struct v4l2_format *out)
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

static int sel(int fd, unsigned long req, __u32 target, struct v4l2_rect *r)
{
	struct v4l2_selection s;

	memset(&s, 0, sizeof(s));
	s.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;	/* selection uses the single-planar type name on m2m encoders */
	s.target = target;
	if (req == VIDIOC_S_SELECTION)
		s.r = *r;
	if (ioctl(fd, req, &s) < 0)
		return -errno;
	*r = s.r;
	return 0;
}

int main(int argc, char **argv)
{
	struct v4l2_capability cap;
	struct v4l2_format o, c;
	struct v4l2_rect r;
	struct v4l2_streamparm p;
	char what[160], unp[96];
	int fd, e;

	if (argc != 2) {
		fprintf(stderr, "usage: %s /dev/videoN\n", argv[0]);
		return 2;
	}
	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		perror("open");
		return 2;
	}
	memset(&cap, 0, sizeof(cap));
	if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
		perror("QUERYCAP");
		return 2;
	}
	printf("device %s: driver=%s card=%s bus=%s\n\n", argv[1], cap.driver, cap.card, cap.bus_info);
	if (!strstr((char *)cap.card, "ncoder"))
		fprintf(stderr, "warning: card name does not say 'Encoder' - wrong node?\n");

	/* --- 1. crop bounds must describe the visible frame, and be accepted by S_SELECTION --- */
	if ((e = s_fmt(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_NV12, 1920, 1080, &o))) {
		printf("S_FMT OUTPUT 1920x1080 failed: %s\n", strerror(-e));
		return 2;
	}
	printf("S_FMT OUTPUT NV12 1920x1080 -> %ux%u (stride %u)\n", o.fmt.pix_mp.width,
	       o.fmt.pix_mp.height, o.fmt.pix_mp.plane_fmt[0].bytesperline);

	e = sel(fd, VIDIOC_G_SELECTION, V4L2_SEL_TGT_CROP_BOUNDS, &r);
	snprintf(what, sizeof(what), "CROP_BOUNDS is the visible frame 1920x1080 (got %dx%d, err %d)", r.width, r.height, e);
	check(!e && r.width == 1920 && r.height == 1080, what, "1920x1088");

	e = sel(fd, VIDIOC_G_SELECTION, V4L2_SEL_TGT_CROP, &r);
	snprintf(what, sizeof(what), "default CROP equals the bounds, 1920x1080 (got %dx%d)", r.width, r.height);
	check(!e && r.width == 1920 && r.height == 1080, what, "1920x1088");

	r = (struct v4l2_rect){ .left = 0, .top = 0, .width = 1920, .height = 1080 };
	e = sel(fd, VIDIOC_S_SELECTION, V4L2_SEL_TGT_CROP, &r);
	check(!e, "S_SELECTION CROP = the advertised bounds is accepted", "works only for heights that are a multiple of 32");

	/* --- 2. S_SELECTION adjusts instead of failing --- */
	r = (struct v4l2_rect){ .left = 17, .top = 9, .width = 4000, .height = 4000 };
	e = sel(fd, VIDIOC_S_SELECTION, V4L2_SEL_TGT_CROP, &r);
	snprintf(what, sizeof(what), "oversized crop is adjusted into 1920x1080 at (0,0) (got %dx%d@%d,%d err %d)", r.width,
		 r.height, r.left, r.top, e);
	check(!e && r.width <= 1920 && r.height <= 1080 && !r.left && !r.top, what, "EINVAL");

	r = (struct v4l2_rect){ .left = 0, .top = 0, .width = 0, .height = 0 };
	e = sel(fd, VIDIOC_S_SELECTION, V4L2_SEL_TGT_CROP, &r);
	snprintf(what, sizeof(what), "empty crop is raised to the minimum size (got %dx%d err %d)", r.width, r.height, e);
	check(!e && r.width >= 128 && r.height >= 128, what, "accepted as 0x0");

	/* restore a sane state */
	s_fmt(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_NV12, 1280, 720, &o);

	/* --- 3. frame size validation --- */
	e = s_fmt(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_NV12, 16384, 16384, &o);
	snprintf(what, sizeof(what), "OUTPUT 16384x16384 is clamped to <= 8192 (got %ux%u err %d)", o.fmt.pix_mp.width,
		 o.fmt.pix_mp.height, e);
	check(!e && o.fmt.pix_mp.width <= 8192 && o.fmt.pix_mp.height <= 8192, what, "accepted as is");

	e = s_fmt(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_NV12, 64, 64, &o);
	snprintf(what, sizeof(what), "OUTPUT 64x64 is raised to >= 128 (got %ux%u err %d)", o.fmt.pix_mp.width,
		 o.fmt.pix_mp.height, e);
	check(!e && o.fmt.pix_mp.width >= 128 && o.fmt.pix_mp.height >= 128, what, "accepted as is");

	/* --- 4. the encoder can only scale down --- */
	s_fmt(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_NV12, 1280, 720, &o);
	e = s_fmt(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_PIX_FMT_H264, 3840, 2160, &c);
	snprintf(what, sizeof(what), "CAPTURE larger than the raw frame is not an upscale (raw 1280x720, got %ux%u)",
		 c.fmt.pix_mp.width, c.fmt.pix_mp.height);
	check(!e && c.fmt.pix_mp.width <= 1280 && c.fmt.pix_mp.height <= 736, what, "3840x2160 accepted");

	e = s_fmt(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_PIX_FMT_H264, 640, 360, &c);
	snprintf(what, sizeof(what), "CAPTURE smaller than the raw frame stays smaller (got %ux%u) - downscale request",
		 c.fmt.pix_mp.width, c.fmt.pix_mp.height);
	snprintf(unp, sizeof(unp), "same");
	check(!e && c.fmt.pix_mp.width < 1280, what, unp);

	/* --- 5. frame rate rounding (observable through G_PARM) --- */
	memset(&p, 0, sizeof(p));
	p.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	p.parm.capture.timeperframe.numerator = 1001;
	p.parm.capture.timeperframe.denominator = 30000;
	e = ioctl(fd, VIDIOC_S_PARM, &p) < 0 ? -errno : 0;
	memset(&p, 0, sizeof(p));
	p.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	if (!e && ioctl(fd, VIDIOC_G_PARM, &p) < 0)
		e = -errno;
	snprintf(what, sizeof(what), "30000/1001 is stored as 30 fps (G_PARM reports %u/%u)",
		 p.parm.capture.timeperframe.numerator, p.parm.capture.timeperframe.denominator);
	check(!e && p.parm.capture.timeperframe.denominator == 30, what, "1/29");

	printf("\n%s\n", fails ? "RESULT: FAIL - this driver does not (fully) have the negotiation fixes."
			      : "RESULT: PASS - negotiation behaves like the patched driver.");
	close(fd);
	return fails ? 1 : 0;
}
