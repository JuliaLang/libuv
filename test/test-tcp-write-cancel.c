/* Copyright libuv project contributors. All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "uv.h"
#include "task.h"

#define REQ_COUNT 100

static uv_tcp_t server;
static uv_tcp_t client;
static uv_tcp_t incoming;
static int close_cb_called;
static int write_cb_called;
static int cancelled_count;
static int expected_cb_count;
static size_t last_nwritten;

static uv_write_t write_reqs[REQ_COUNT];

static void close_cb(uv_handle_t* handle) {
  close_cb_called++;
}

/*
 * TEST 1: tcp_write3_basic
 * - Basic uv_write3 works, callback receives correct nwritten on success
 */
static char write3_basic_buf_data[1024];

static void write3_basic_write_cb(uv_write_t* req, int status, size_t nwritten) {
  write_cb_called++;
  last_nwritten = nwritten;
  if (status == UV_ECANCELED)
    cancelled_count++;

  if (write_cb_called == expected_cb_count) {
    uv_close((uv_handle_t*) &client, close_cb);
    uv_close((uv_handle_t*) &server, close_cb);
    uv_close((uv_handle_t*) &incoming, close_cb);
  }
}

static void write3_basic_connect_cb(uv_connect_t* req, int status) {
  uv_buf_t buf;
  int r;

  ASSERT_OK(status);

  buf = uv_buf_init(write3_basic_buf_data, sizeof(write3_basic_buf_data));
  r = uv_write3(&write_reqs[0],
                req->handle,
                &buf,
                1,
                NULL,
                0,
                write3_basic_write_cb);
  ASSERT_OK(r);
}

static void write3_basic_connection_cb(uv_stream_t* tcp, int status) {
  ASSERT_OK(status);
  ASSERT_OK(uv_tcp_init(tcp->loop, &incoming));
  ASSERT_OK(uv_accept(tcp, (uv_stream_t*) &incoming));
}

TEST_IMPL(tcp_write3_basic) {
  uv_connect_t connect_req;
  struct sockaddr_in addr;
  uv_loop_t* loop;

  loop = uv_default_loop();

  write_cb_called = 0;
  cancelled_count = 0;
  last_nwritten = 0;
  close_cb_called = 0;
  expected_cb_count = 1;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));
  ASSERT_OK(uv_tcp_init(loop, &server));
  ASSERT_OK(uv_tcp_bind(&server, (struct sockaddr*) &addr, 0));
  ASSERT_OK(uv_listen((uv_stream_t*) &server, 128, write3_basic_connection_cb));

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  ASSERT_OK(uv_tcp_init(loop, &client));
  ASSERT_OK(uv_tcp_connect(&connect_req,
                           &client,
                           (struct sockaddr*) &addr,
                           write3_basic_connect_cb));

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  ASSERT_EQ(1, write_cb_called);
  ASSERT_EQ(0, cancelled_count);
  ASSERT_EQ(1024, last_nwritten);
  ASSERT_EQ(3, close_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


/*
 * TEST 2: tcp_write3_cancel_queued
 * - Queue multiple writes, cancel some that are queued
 * - Verify UV_ECANCELED status in callbacks
 */
static char cancel_queued_buf_data[16 * 1024];
static int cancel_queued_cancelled;
static int cancel_queued_closing;

static void cancel_queued_write_cb(uv_write_t* req, int status, size_t nwritten) {
  write_cb_called++;
  if (status == UV_ECANCELED && !cancel_queued_closing)
    cancelled_count++;

  if (cancelled_count == expected_cb_count && !cancel_queued_closing) {
    cancel_queued_closing = 1;
    uv_close((uv_handle_t*) &client, close_cb);
    uv_close((uv_handle_t*) &server, close_cb);
    uv_close((uv_handle_t*) &incoming, close_cb);
  }
}

static void cancel_queued_connect_cb(uv_connect_t* req, int status) {
  uv_buf_t buf;
  int r;
  int i;

  ASSERT_OK(status);

  buf = uv_buf_init(cancel_queued_buf_data, sizeof(cancel_queued_buf_data));

  /* Queue many writes to fill the socket buffer */
  for (i = 0; i < REQ_COUNT; i++) {
    r = uv_write3(&write_reqs[i],
                  req->handle,
                  &buf,
                  1,
                  NULL,
                  0,
                  cancel_queued_write_cb);
    ASSERT_OK(r);
  }

  /* Cancel the last few writes which should still be queued */
  cancel_queued_cancelled = 0;
  for (i = REQ_COUNT - 5; i < REQ_COUNT; i++) {
    r = uv_cancel((uv_req_t*) &write_reqs[i]);
    ASSERT_OK(r);
    cancel_queued_cancelled++;
  }

  expected_cb_count = cancel_queued_cancelled;
}

static void cancel_queued_connection_cb(uv_stream_t* tcp, int status) {
  ASSERT_OK(status);
  ASSERT_OK(uv_tcp_init(tcp->loop, &incoming));
  ASSERT_OK(uv_accept(tcp, (uv_stream_t*) &incoming));
}

TEST_IMPL(tcp_write3_cancel_queued) {
  uv_connect_t connect_req;
  struct sockaddr_in addr;
  uv_loop_t* loop;
  int buffer_size = 16 * 1024;

  loop = uv_default_loop();

  write_cb_called = 0;
  cancelled_count = 0;
  close_cb_called = 0;
  expected_cb_count = 0;
  cancel_queued_closing = 0;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));
  ASSERT_OK(uv_tcp_init(loop, &server));
  ASSERT_OK(uv_tcp_bind(&server, (struct sockaddr*) &addr, 0));
  ASSERT_OK(uv_listen((uv_stream_t*) &server, 128, cancel_queued_connection_cb));

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  ASSERT_OK(uv_tcp_init(loop, &client));
  ASSERT_OK(uv_tcp_connect(&connect_req,
                           &client,
                           (struct sockaddr*) &addr,
                           cancel_queued_connect_cb));
  ASSERT_OK(uv_send_buffer_size((uv_handle_t*) &client, &buffer_size));

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  /* The writes we explicitly cancelled should have gotten callbacks */
  ASSERT_EQ(cancel_queued_cancelled, cancelled_count);
  ASSERT_EQ(3, close_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}


/*
 * TEST 3: tcp_write_cancel_old_api
 * - Old API (uv_write) cancel succeeds if bytes_written == 0
 */
static char cancel_old_api_buf_data[16 * 1024];
static int cancel_old_api_cancelled;
static int cancel_old_api_closing;

static void cancel_old_api_write_cb(uv_write_t* req, int status) {
  write_cb_called++;
  if (status == UV_ECANCELED && !cancel_old_api_closing)
    cancelled_count++;

  if (cancelled_count == expected_cb_count &&
      expected_cb_count > 0 &&
      !cancel_old_api_closing) {
    cancel_old_api_closing = 1;
    uv_close((uv_handle_t*) &client, close_cb);
    uv_close((uv_handle_t*) &server, close_cb);
    uv_close((uv_handle_t*) &incoming, close_cb);
  }
}

static void cancel_old_api_connect_cb(uv_connect_t* req, int status) {
  uv_buf_t buf;
  int r;
  int i;

  ASSERT_OK(status);

  buf = uv_buf_init(cancel_old_api_buf_data, sizeof(cancel_old_api_buf_data));

  /* Queue many writes */
  for (i = 0; i < REQ_COUNT; i++) {
    r = uv_write(&write_reqs[i],
                 req->handle,
                 &buf,
                 1,
                 cancel_old_api_write_cb);
    ASSERT_OK(r);
  }

  /* Try to cancel the last few writes */
  cancel_old_api_cancelled = 0;
  for (i = REQ_COUNT - 5; i < REQ_COUNT; i++) {
    r = uv_cancel((uv_req_t*) &write_reqs[i]);
    /* May succeed or fail with UV_EBUSY depending on timing */
    ASSERT(r == 0 || r == UV_EBUSY);
    if (r == 0)
      cancel_old_api_cancelled++;
  }

  expected_cb_count = cancel_old_api_cancelled;

  /* If no cancels succeeded, close handles now */
  if (expected_cb_count == 0) {
    cancel_old_api_closing = 1;
    uv_close((uv_handle_t*) &client, close_cb);
    uv_close((uv_handle_t*) &server, close_cb);
    uv_close((uv_handle_t*) &incoming, close_cb);
  }
}

static void cancel_old_api_connection_cb(uv_stream_t* tcp, int status) {
  ASSERT_OK(status);
  ASSERT_OK(uv_tcp_init(tcp->loop, &incoming));
  ASSERT_OK(uv_accept(tcp, (uv_stream_t*) &incoming));
}

TEST_IMPL(tcp_write_cancel_old_api) {
  uv_connect_t connect_req;
  struct sockaddr_in addr;
  uv_loop_t* loop;
  int buffer_size = 16 * 1024;

  loop = uv_default_loop();

  write_cb_called = 0;
  cancelled_count = 0;
  close_cb_called = 0;
  expected_cb_count = 0;
  cancel_old_api_closing = 0;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", TEST_PORT, &addr));
  ASSERT_OK(uv_tcp_init(loop, &server));
  ASSERT_OK(uv_tcp_bind(&server, (struct sockaddr*) &addr, 0));
  ASSERT_OK(uv_listen((uv_stream_t*) &server, 128, cancel_old_api_connection_cb));

  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));
  ASSERT_OK(uv_tcp_init(loop, &client));
  ASSERT_OK(uv_tcp_connect(&connect_req,
                           &client,
                           (struct sockaddr*) &addr,
                           cancel_old_api_connect_cb));
  ASSERT_OK(uv_send_buffer_size((uv_handle_t*) &client, &buffer_size));

  ASSERT_OK(uv_run(loop, UV_RUN_DEFAULT));

  /* Cancelled count should match what we successfully cancelled */
  ASSERT_EQ(cancel_old_api_cancelled, cancelled_count);
  ASSERT_EQ(3, close_cb_called);

  MAKE_VALGRIND_HAPPY(loop);
  return 0;
}
