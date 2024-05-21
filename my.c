#include <errno.h>
#include <fcntl.h> /* low-level i/o */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/videodev2.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

enum io_method {
  IO_METHOD_READ,
  IO_METHOD_MMAP,
  IO_METHOD_USERPTR,
};

static char *dev_name;
static enum io_method io = IO_METHOD_MMAP;
static int fd = -1;
static int force_format;

static void errno_exit(const char *s) {
  fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
  exit(EXIT_FAILURE);
}

static int xioctl(int fh, int request, void *arg) {
  int r;

  do {
    r = ioctl(fh, request, arg);
  } while (-1 == r && EINTR == errno);

  return r;
}

static void open_device() {
  struct stat st;
  if (-1 == stat(dev_name, &st)) {
    errno_exit("stat");
  }
  if (!S_ISCHR(st.st_mode)) {
    errno_exit("mode");
  }
  fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);
  if (-1 == fd) {
    errno_exit("open");
  }
}

static void init_device(void) {
  struct v4l2_capability cap;
  struct v4l2_cropcap cropcap;
  struct v4l2_crop crop;
  struct v4l2_format fmt;

  if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
    if (EINVAL == errno) {
      fprintf(stderr, "%s is no V4L2 device\n", dev_name);
      exit(EXIT_FAILURE);
    } else {
      errno_exit("VIDIOC_QUERYCAP");
    }
  }

  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    fprintf(stderr, "%s is no video capture device\n", dev_name);
    exit(EXIT_FAILURE);
  }

  switch (io) {
  case IO_METHOD_READ:
    if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
      fprintf(stderr, "%s does not support read i/o\n", dev_name);
      exit(EXIT_FAILURE);
    }
    break;
  case IO_METHOD_MMAP:
  case IO_METHOD_USERPTR:
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
      fprintf(stderr, "%s does not support streaming i/o\n", dev_name);
      exit(EXIT_FAILURE);
    }
    break;
  }

  CLEAR(cropcap);

  cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    crop.c = cropcap.defrect; /* reset to default */

    if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop)) {
      switch (errno) {
      case EINVAL:
        /* Cropping not supported. */
        break;
      default:
        /* Errors ignored. */
        break;
      }
    }
  }

  CLEAR(fmt);

  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (force_format) {
    fmt.fmt.pix.width = 640;
    fmt.fmt.pix.height = 480;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

    if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
      errno_exit("VIDIOC_S_FMT");

    /* Note VIDIOC_S_FMT may change width and height. */
  } else {
    /* Preserve original settings as set by v4l2-ctl for example */
    if (-1 == xioctl(fd, VIDIOC_G_FMT, &fmt))
      errno_exit("VIDIOC_G_FMT");
  }
}

static void close_device(void) {
  if (-1 == close(fd)) {
    errno_exit("close");
  }
}

int main() {
  dev_name = "/dev/video1";

  open_device();
  init_device();
  close_device();
}
