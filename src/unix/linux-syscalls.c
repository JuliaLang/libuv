/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "linux-syscalls.h"
#include "internal.h"
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/stat.h>


#if defined(__arm__)
# if defined(__thumb__) || defined(__ARM_EABI__)
#  define UV_SYSCALL_BASE 0
# else
#  define UV_SYSCALL_BASE 0x900000
# endif
#endif /* __arm__ */

#ifndef __NR_recvmmsg
# if defined(__x86_64__)
#  define __NR_recvmmsg 299
# elif defined(__arm__)
#  define __NR_recvmmsg (UV_SYSCALL_BASE + 365)
# endif
#endif /* __NR_recvmsg */

#ifndef __NR_sendmmsg
# if defined(__x86_64__)
#  define __NR_sendmmsg 307
# elif defined(__arm__)
#  define __NR_sendmmsg (UV_SYSCALL_BASE + 374)
# endif
#endif /* __NR_sendmmsg */

#ifndef __NR_utimensat
# if defined(__x86_64__)
#  define __NR_utimensat 280
# elif defined(__i386__)
#  define __NR_utimensat 320
# elif defined(__arm__)
#  define __NR_utimensat (UV_SYSCALL_BASE + 348)
# endif
#endif /* __NR_utimensat */

#ifndef __NR_preadv
# if defined(__x86_64__)
#  define __NR_preadv 295
# elif defined(__i386__)
#  define __NR_preadv 333
# elif defined(__arm__)
#  define __NR_preadv (UV_SYSCALL_BASE + 361)
# endif
#endif /* __NR_preadv */

#ifndef __NR_pwritev
# if defined(__x86_64__)
#  define __NR_pwritev 296
# elif defined(__i386__)
#  define __NR_pwritev 334
# elif defined(__arm__)
#  define __NR_pwritev (UV_SYSCALL_BASE + 362)
# endif
#endif /* __NR_pwritev */

#ifndef __NR_dup3
# if defined(__x86_64__)
#  define __NR_dup3 292
# elif defined(__i386__)
#  define __NR_dup3 330
# elif defined(__arm__)
#  define __NR_dup3 (UV_SYSCALL_BASE + 358)
# endif
#endif /* __NR_pwritev */

#ifndef __NR_copy_file_range
# if defined(__x86_64__)
#  define __NR_copy_file_range 326
# elif defined(__i386__)
#  define __NR_copy_file_range 377
# elif defined(__s390__)
#  define __NR_copy_file_range 375
# elif defined(__arm__)
#  define __NR_copy_file_range (UV_SYSCALL_BASE + 391)
# elif defined(__aarch64__)
#  define __NR_copy_file_range 285
# elif defined(__powerpc__)
#  define __NR_copy_file_range 379
# elif defined(__arc__)
#  define __NR_copy_file_range 285
# endif
#endif /* __NR_copy_file_range */

#ifndef __NR_statx
# if defined(__x86_64__)
#  define __NR_statx 332
# elif defined(__i386__)
#  define __NR_statx 383
# elif defined(__aarch64__)
#  define __NR_statx 397
# elif defined(__arm__)
#  define __NR_statx (UV_SYSCALL_BASE + 397)
# elif defined(__ppc__)
#  define __NR_statx 383
# elif defined(__s390__)
#  define __NR_statx 379
# endif
#endif /* __NR_statx */

#ifndef __NR_getrandom
# if defined(__x86_64__)
#  define __NR_getrandom 318
# elif defined(__i386__)
#  define __NR_getrandom 355
# elif defined(__aarch64__)
#  define __NR_getrandom 384
# elif defined(__arm__)
#  define __NR_getrandom (UV_SYSCALL_BASE + 384)
# elif defined(__ppc__)
#  define __NR_getrandom 359
# elif defined(__s390__)
#  define __NR_getrandom 349
# endif
#endif /* __NR_getrandom */

struct uv__mmsghdr;

int uv__sendmmsg(int fd, struct uv__mmsghdr* mmsg, unsigned int vlen) {
#if defined(__i386__)
  unsigned long args[4];
  int rc;

  args[0] = (unsigned long) fd;
  args[1] = (unsigned long) mmsg;
  args[2] = (unsigned long) vlen;
  args[3] = /* flags */ 0;

  /* socketcall() raises EINVAL when SYS_SENDMMSG is not supported. */
  rc = syscall(/* __NR_socketcall */ 102, 20 /* SYS_SENDMMSG */, args);
  if (rc == -1)
    if (errno == EINVAL)
      errno = ENOSYS;

  return rc;
#elif defined(__NR_sendmmsg)
  return syscall(__NR_sendmmsg, fd, mmsg, vlen, /* flags */ 0);
#else
  return errno = ENOSYS, -1;
#endif
}


static void uv__recvmmsg_unpoison(struct uv__mmsghdr* mmsg, int rc) {
  struct uv__mmsghdr* m;
  struct msghdr* h;
  struct iovec* v;
  size_t j;
  int i;

  for (i = 0; i < rc; i++) {
    m = mmsg + i;
    uv__msan_unpoison(m, sizeof(*m));

    h = &m->msg_hdr;
    if (h->msg_name != NULL)
      uv__msan_unpoison(h->msg_name, h->msg_namelen);

    if (h->msg_iov != NULL)
      uv__msan_unpoison(h->msg_iov, h->msg_iovlen * sizeof(*h->msg_iov));

    for (j = 0; j < h->msg_iovlen; j++) {
      v = h->msg_iov + j;
      uv__msan_unpoison(v->iov_base, v->iov_len);
    }

    if (h->msg_control != NULL)
      uv__msan_unpoison(h->msg_control, h->msg_controllen);
  }
}


int uv__recvmmsg(int fd, struct uv__mmsghdr* mmsg, unsigned int vlen) {
#if defined(__i386__)
  unsigned long args[5];
  int rc;

  args[0] = (unsigned long) fd;
  args[1] = (unsigned long) mmsg;
  args[2] = (unsigned long) vlen;
  args[3] = /* flags */ 0;
  args[4] = /* timeout */ 0;

  /* socketcall() raises EINVAL when SYS_RECVMMSG is not supported. */
  rc = syscall(/* __NR_socketcall */ 102, 19 /* SYS_RECVMMSG */, args);
  uv__recvmmsg_unpoison(mmsg, rc);
  if (rc == -1)
    if (errno == EINVAL)
      errno = ENOSYS;

  return rc;
#elif defined(__NR_recvmmsg)
  int rc;

  rc = syscall(__NR_recvmmsg, fd, mmsg, vlen, /* flags */ 0, /* timeout */ 0);
  uv__recvmmsg_unpoison(mmsg, rc);

  return rc;
#else
  return errno = ENOSYS, -1;
#endif
}


ssize_t uv__preadv(int fd, const struct iovec *iov, int iovcnt, int64_t offset) {
#if !defined(__NR_preadv) || defined(__ANDROID_API__) && __ANDROID_API__ < 24
  return errno = ENOSYS, -1;
#else
  ssize_t rc;
  size_t n;
  int i;

  rc = syscall(__NR_preadv, fd, iov, iovcnt, (long)offset, (long)(offset >> 32));
  for (i = 0, n = rc > 0 ? rc : 0; i < iovcnt && n > 0; i++) {
    size_t len = iov[i].iov_len < n ? iov[i].iov_len : n;
    uv__msan_unpoison(iov[i].iov_base, len);
    n -= len;
  }

  return rc;
#endif
}


ssize_t uv__pwritev(int fd, const struct iovec *iov, int iovcnt, int64_t offset) {
#if !defined(__NR_pwritev) || defined(__ANDROID_API__) && __ANDROID_API__ < 24
  return errno = ENOSYS, -1;
#else
  return syscall(__NR_pwritev, fd, iov, iovcnt, (long)offset, (long)(offset >> 32));
#endif
}


int uv__dup3(int oldfd, int newfd, int flags) {
#if !defined(__NR_dup3) || defined(__ANDROID_API__) && __ANDROID_API__ < 21
  return errno = ENOSYS, -1;
#else
  return syscall(__NR_dup3, oldfd, newfd, flags);
#endif
}


ssize_t
uv__fs_copy_file_range(int fd_in,
                       off_t* off_in,
                       int fd_out,
                       off_t* off_out,
                       size_t len,
                       unsigned int flags)
{
#ifdef __NR_copy_file_range
  return syscall(__NR_copy_file_range,
                 fd_in,
                 off_in,
                 fd_out,
                 off_out,
                 len,
                 flags);
#else
  return errno = ENOSYS, -1;
#endif
}


int uv__statx(int dirfd,
              const char* path,
              int flags,
              unsigned int mask,
              struct uv__statx* statxbuf) {
#if !defined(__NR_statx) || defined(__ANDROID_API__) && __ANDROID_API__ < 30
  return errno = ENOSYS, -1;
#else
  int rc;

  rc = syscall(__NR_statx, dirfd, path, flags, mask, statxbuf);
  if (rc >= 0)
    uv__msan_unpoison(statxbuf, sizeof(*statxbuf));

  return rc;
#endif
}


  ssize_t uv__getrandom(void* buf, size_t buflen, unsigned flags) {
#if !defined(__NR_getrandom) || defined(__ANDROID_API__) && __ANDROID_API__ < 28
  return errno = ENOSYS, -1;
#else
  ssize_t rc;

  rc = syscall(__NR_getrandom, buf, buflen, flags);
  if (rc >= 0)
    uv__msan_unpoison(buf, buflen);

  return rc;
#endif
}
