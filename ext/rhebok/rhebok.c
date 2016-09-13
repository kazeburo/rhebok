#include <ruby.h>
#include <time.h>
#include <ctype.h>
#include <poll.h>
#ifndef __need_IOV_MAX
#define __need_IOV_MAX
#endif
#include <sys/uio.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include "picohttpparser/picohttpparser.c"

#ifndef IOV_MAX
#if defined(__FreeBSD__) || defined(__APPLE__)
# define IOV_MAX 128
#endif
#endif

#ifndef IOV_MAX
#  error "Unable to determine IOV_MAX from system headers"
#endif

#define MAX_HEADER_SIZE 16384
#define MAX_HEADER_NAME_LEN 1024
#define MAX_HEADERS         128
#define BAD_REQUEST "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\n400 Bad Request\r\n"
#define EXPECT_CONTINUE "HTTP/1.1 100 Continue\r\n\r\n"
#define EXPECT_FAILED "HTTP/1.1 417 Expectation Failed\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nExpectation Failed\r\n"
#define READ_BUF 16384
#define TOU(ch) (('a' <= ch && ch <= 'z') ? ch - ('a' - 'A') : ch)
#define RETURN_STATUS_MESSAGE(s, l) l = sizeof(s) - 1; return s;

static const char *DoW[] = {
  "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
};
static const char *MoY[] = {
  "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};
static const char xdigit[16] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};


/* stolen from HTTP::Status and Feersum */
/* Unmarked codes are from RFC 2616 */
/* See also: http://en.wikipedia.org/wiki/List_of_HTTP_status_codes */
static const char *
status_message (int code, size_t *mlen) {
  switch (code) {
    case 100: RETURN_STATUS_MESSAGE("Continue", *mlen);
    case 101: RETURN_STATUS_MESSAGE("Switching Protocols", *mlen);
    case 102: RETURN_STATUS_MESSAGE("Processing", *mlen);                      /* RFC 2518 (WebDAV) */
    case 200: RETURN_STATUS_MESSAGE("OK", *mlen);
    case 201: RETURN_STATUS_MESSAGE("Created", *mlen);
    case 202: RETURN_STATUS_MESSAGE("Accepted", *mlen);
    case 203: RETURN_STATUS_MESSAGE("Non-Authoritative Information", *mlen);
    case 204: RETURN_STATUS_MESSAGE("No Content", *mlen);
    case 205: RETURN_STATUS_MESSAGE("Reset Content", *mlen);
    case 206: RETURN_STATUS_MESSAGE("Partial Content", *mlen);
    case 207: RETURN_STATUS_MESSAGE("Multi-Status", *mlen);                    /* RFC 2518 (WebDAV) */
    case 208: RETURN_STATUS_MESSAGE("Already Reported", *mlen);                /* RFC 5842 */
    case 300: RETURN_STATUS_MESSAGE("Multiple Choices", *mlen);
    case 301: RETURN_STATUS_MESSAGE("Moved Permanently", *mlen);
    case 302: RETURN_STATUS_MESSAGE("Found", *mlen);
    case 303: RETURN_STATUS_MESSAGE("See Other", *mlen);
    case 304: RETURN_STATUS_MESSAGE("Not Modified", *mlen);
    case 305: RETURN_STATUS_MESSAGE("Use Proxy", *mlen);
    case 307: RETURN_STATUS_MESSAGE("Temporary Redirect", *mlen);
    case 400: RETURN_STATUS_MESSAGE("Bad Request", *mlen);
    case 401: RETURN_STATUS_MESSAGE("Unauthorized", *mlen);
    case 402: RETURN_STATUS_MESSAGE("Payment Required", *mlen);
    case 403: RETURN_STATUS_MESSAGE("Forbidden", *mlen);
    case 404: RETURN_STATUS_MESSAGE("Not Found", *mlen);
    case 405: RETURN_STATUS_MESSAGE("Method Not Allowed", *mlen);
    case 406: RETURN_STATUS_MESSAGE("Not Acceptable", *mlen);
    case 407: RETURN_STATUS_MESSAGE("Proxy Authentication Required", *mlen);
    case 408: RETURN_STATUS_MESSAGE("Request Timeout", *mlen);
    case 409: RETURN_STATUS_MESSAGE("Conflict", *mlen);
    case 410: RETURN_STATUS_MESSAGE("Gone", *mlen);
    case 411: RETURN_STATUS_MESSAGE("Length Required", *mlen);
    case 412: RETURN_STATUS_MESSAGE("Precondition Failed", *mlen);
    case 413: RETURN_STATUS_MESSAGE("Request Entity Too Large", *mlen);
    case 414: RETURN_STATUS_MESSAGE("Request-URI Too Large", *mlen);
    case 415: RETURN_STATUS_MESSAGE("Unsupported Media Type", *mlen);
    case 416: RETURN_STATUS_MESSAGE("Request Range Not Satisfiable", *mlen);
    case 417: RETURN_STATUS_MESSAGE("Expectation Failed", *mlen);
    case 418: RETURN_STATUS_MESSAGE("I'm a teapot", *mlen);                    /* RFC 2324 */
    case 422: RETURN_STATUS_MESSAGE("Unprocessable Entity", *mlen);            /* RFC 2518 (WebDAV) */
    case 423: RETURN_STATUS_MESSAGE("Locked", *mlen);                          /* RFC 2518 (WebDAV) */
    case 424: RETURN_STATUS_MESSAGE("Failed Dependency", *mlen);               /* RFC 2518 (WebDAV) */
    case 425: RETURN_STATUS_MESSAGE("No code", *mlen);                         /* WebDAV Advanced Collections */
    case 426: RETURN_STATUS_MESSAGE("Upgrade Required", *mlen);                /* RFC 2817 */
    case 428: RETURN_STATUS_MESSAGE("Precondition Required", *mlen);
    case 429: RETURN_STATUS_MESSAGE("Too Many Requests", *mlen);
    case 431: RETURN_STATUS_MESSAGE("Request Header Fields Too Large", *mlen);
    case 449: RETURN_STATUS_MESSAGE("Retry with", *mlen);                      /* unofficial Microsoft */
    case 500: RETURN_STATUS_MESSAGE("Internal Server Error", *mlen);
    case 501: RETURN_STATUS_MESSAGE("Not Implemented", *mlen);
    case 502: RETURN_STATUS_MESSAGE("Bad Gateway", *mlen);
    case 503: RETURN_STATUS_MESSAGE("Service Unavailable", *mlen);
    case 504: RETURN_STATUS_MESSAGE("Gateway Timeout", *mlen);
    case 505: RETURN_STATUS_MESSAGE("HTTP Version Not Supported", *mlen);
    case 506: RETURN_STATUS_MESSAGE("Variant Also Negotiates", *mlen);         /* RFC 2295 */
    case 507: RETURN_STATUS_MESSAGE("Insufficient Storage", *mlen);            /* RFC 2518 (WebDAV) */
    case 509: RETURN_STATUS_MESSAGE("Bandwidth Limit Exceeded", *mlen);        /* unofficial */
    case 510: RETURN_STATUS_MESSAGE("Not Extended", *mlen);                    /* RFC 2774 */
    case 511: RETURN_STATUS_MESSAGE("Network Authentication Required", *mlen);
    default: break;
  }
  /* default to the Nxx group names in RFC 2616 */
  if (100 <= code && code <= 199) {
    RETURN_STATUS_MESSAGE("Informational", *mlen);
  }
  else if (200 <= code && code <= 299) {
    RETURN_STATUS_MESSAGE("Success", *mlen);
  }
  else if (300 <= code && code <= 399) {
    RETURN_STATUS_MESSAGE("Redirection", *mlen);
  }
  else if (400 <= code && code <= 499) {
    RETURN_STATUS_MESSAGE("Client Error", *mlen);
  }
  else {
    RETURN_STATUS_MESSAGE("Error", *mlen);
  }
}

static VALUE cRhebok;

static VALUE request_method_key;
static VALUE request_uri_key;
static VALUE script_name_key;
static VALUE server_protocol_key;
static VALUE query_string_key;
static VALUE remote_addr_key;
static VALUE remote_port_key;
static VALUE path_info_key;

static VALUE vacant_string_val;
static VALUE zero_string_val;

static VALUE http10_val;
static VALUE http11_val;

static VALUE expect_key;

struct common_header {
  const char * name;
  size_t name_len;
  VALUE key;
};
static int common_headers_num = 0;
static struct common_header common_headers[20];

static char date_buf[sizeof("Date: Sat, 19 Dec 2015 14:16:27 GMT\r\n")-1];

static
void set_common_header(const char * key, int key_len, const int raw)
{
  char tmp[MAX_HEADER_NAME_LEN + sizeof("HTTP_") - 1];
  const char* name;
  size_t name_len = 0;
  const char * s;
  char* d;
  size_t n;
  VALUE env_key;

  if ( raw == 1) {
    for (s = key, n = key_len, d = tmp;
      n != 0;
      s++, --n, d++) {
      *d = *s == '-' ? '_' : TOU(*s);
      name = tmp;
      name_len = key_len;
    }
  } else {
    strcpy(tmp, "HTTP_");
    for (s = key, n = key_len, d = tmp + 5;
      n != 0;
      s++, --n, d++) {
      *d = *s == '-' ? '_' : TOU(*s);
      name = tmp;
      name_len = key_len + 5;
    }
  }
  env_key = rb_obj_freeze(rb_str_new(name,name_len));
  common_headers[common_headers_num].name = key;
  common_headers[common_headers_num].name_len = key_len;
  common_headers[common_headers_num].key = env_key;
  rb_gc_register_address(&common_headers[common_headers_num].key);
  common_headers_num++;
}

static
long find_lf(const char* v, ssize_t offset, ssize_t len)
{
  ssize_t i;
  for ( i=offset; i < len; i++) {
    if (v[i] == 10) {
      return i;
    }
  }
  return len;
}

static
size_t find_ch(const char* s, size_t len, char ch)
{
  size_t i;
  for (i = 0; i != len; ++i, ++s)
    if (*s == ch)
      break;
  return i;
}

static
int header_is(const struct phr_header* header, const char* name,
                    size_t len)
{
  const char* x, * y;
  if (header->name_len != len)
    return 0;
  for (x = header->name, y = name; len != 0; --len, ++x, ++y)
    if (TOU(*x) != *y)
      return 0;
  return 1;
}

static
VALUE find_common_header(const struct phr_header* header) {
  int i;
  for ( i = 0; i < common_headers_num; i++ ) {
    if ( header_is(header, common_headers[i].name, common_headers[i].name_len) ) {
      return common_headers[i].key;
    }
  }
  return Qnil;
}

static
int store_path_info(VALUE env, const char* src, size_t src_len) {
  int dlen = 0;
  size_t i = 0;
  char *d;
  char s2, s3;
  d = ALLOC_N(char, src_len * 3 + 1);
  for (i = 0; i < src_len; i++ ) {
    if ( src[i] == '%' ) {
      if ( !isxdigit(src[i+1]) || !isxdigit(src[i+2]) ) {
        free(d);
        return -1;
      }
      s2 = src[i+1];
      s3 = src[i+2];
      s2 -= s2 <= '9' ? '0'
          : s2 <= 'F' ? 'A' - 10
          : 'a' - 10;
      s3 -= s3 <= '9' ? '0'
          : s3 <= 'F' ? 'A' - 10
          : 'a' - 10;
       d[dlen++] = s2 * 16 + s3;
       i += 2;
    }
    else {
      d[dlen++] = src[i];
    }
  }
  d[dlen]='0';
  rb_hash_aset(env, path_info_key, rb_str_new(d, dlen));
  xfree(d);
  return dlen;
}

static
int _accept(int fileno, struct sockaddr *addr, unsigned int addrlen) {
  int fd;
#ifdef SOCK_NONBLOCK
  fd = accept4(fileno, addr, &addrlen, SOCK_CLOEXEC|SOCK_NONBLOCK);
#else
  fd = accept(fileno, addr, &addrlen);
#endif
  if (fd < 0) {
    if ( errno == EINTR ) {
      rb_thread_sleep(1);
    }
    return fd;
  }
#ifndef SOCK_NONBLOCK
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
#endif
  return fd;
}

static
ssize_t _writev_timeout(const int fileno, const double timeout, struct iovec *iovec, const long iovcnt, const int do_select ) {
  ssize_t rv;
  int nfound;
  int iovcnt_len;
  struct pollfd wfds[1];
  if ( iovcnt < 0 ){
      return -1;
  }
  if ( iovcnt > UINT_MAX ) {
      iovcnt_len = UINT_MAX;
  }
  else {
      iovcnt_len = (int)iovcnt;
  }
  if ( do_select == 1) goto WAIT_WRITE;
 DO_WRITE:
  rv = writev(fileno, iovec, iovcnt_len);
  if ( rv >= 0 ) {
    return rv;
  }
  if ( rv < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK ) {
    return rv;
  }
 WAIT_WRITE:
  while (1) {
    wfds[0].fd = fileno;
    wfds[0].events = POLLOUT;
    nfound = poll(wfds, 1, (int)(timeout*1000));
    if ( nfound == 1 ) {
      break;
    }
    if ( nfound == 0 && errno != EINTR ) {
      return -1;
    }
  }
  goto DO_WRITE;
}

static
ssize_t _read_timeout(const int fileno, const double timeout, char * read_buf, const ssize_t read_len ) {
  ssize_t rv;
  int nfound;
  struct pollfd rfds[1];
 DO_READ:
  rfds[0].fd = fileno;
  rfds[0].events = POLLIN;
  //rv = read(fileno, read_buf, read_len);
  rv = recvfrom(fileno, read_buf, read_len, 0, NULL, NULL);
  if ( rv >= 0 ) {
    return rv;
  }
  if ( rv < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK ) {
    return rv;
  }
  while (1) {
    nfound = poll(rfds, 1, (int)(timeout*1000));
    if ( nfound == 1 ) {
      break;
    }
    if ( nfound == 0 && errno != EINTR ) {
      return -1;
    }
  }
  goto DO_READ;
}

static
ssize_t _write_timeout(const int fileno, const double timeout, char * write_buf, const long write_len ) {
  ssize_t rv;
  int nfound;
  struct pollfd wfds[1];
  size_t write_buf_len;
  if ( write_len < 0 ) {
      return -1;
  }
  if ( write_len > UINT_MAX ) {
      write_buf_len = UINT_MAX;
  }
  else {
      write_buf_len = (unsigned int)write_len;
  }
  
 DO_WRITE:
  rv = write(fileno, write_buf, write_buf_len);
  if ( rv >= 0 ) {
    return rv;
  }
  if ( rv < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK ) {
    return rv;
  }
  while (1) {
    wfds[0].fd = fileno;
    wfds[0].events = POLLOUT;
    nfound = poll(wfds, 1, (int)(timeout*1000));
    if ( nfound == 1 ) {
      break;
    }
    if ( nfound == 0 && errno != EINTR ) {
      return -1;
    }
  }
  goto DO_WRITE;
}

static
void str_s(char * dst, int *dst_len, const char * src, const unsigned long src_len) {
  unsigned long i;
  int dlen = *dst_len;
  for ( i=0; i<src_len; i++) {
    dst[dlen++] = src[i];
  }
  *dst_len = dlen;
}

static
void str_i(char * dst, int * dst_len, int src, int fig) {
  int dlen = *dst_len + fig - 1;
  do {
    dst[dlen] = '0' + (src % 10);
    dlen--;
    src /= 10;
  } while( dlen >= *dst_len );
  *dst_len += fig;
}

static
int _chunked_header(char *buf, ssize_t len) {
    int dlen = 0, i;
    ssize_t l = len;
    while ( l > 0 ) {
        dlen++;
        l /= 16;
    }
    i = dlen;
    buf[i++] = 13;
    buf[i++] = 10;
    while ( len > 0 ) {
        buf[--dlen] = xdigit[len % 16];
        len /= 16;
    }
    return i;
}

static
char * _date_header(void) {
  static time_t last;
  struct tm gtm;
  time_t lt;
  int i = 0;
  time(&lt);
  if ( last == lt ) return date_buf;
  last = lt;
  gmtime_r(&lt, &gtm);
  date_buf[i++] = 'D';
  date_buf[i++] = 'a';
  date_buf[i++] = 't';
  date_buf[i++] = 'e';
  date_buf[i++] = ':';
  date_buf[i++] = ' ';
  str_s(date_buf, &i, DoW[gtm.tm_wday], 3);
  date_buf[i++] = ',';
  date_buf[i++] = ' ';
  str_i(date_buf, &i, gtm.tm_mday, 2);
  date_buf[i++] = ' ';
  str_s(date_buf, &i, MoY[gtm.tm_mon], 3);
  date_buf[i++] = ' ';
  str_i(date_buf, &i, gtm.tm_year + 1900, 4);
  date_buf[i++] = ' ';
  str_i(date_buf, &i, gtm.tm_hour,2);
  date_buf[i++] = ':';
  str_i(date_buf, &i, gtm.tm_min,2);
  date_buf[i++] = ':';
  str_i(date_buf, &i, gtm.tm_sec,2);
  date_buf[i++] = ' ';
  date_buf[i++] = 'G';
  date_buf[i++] = 'M';
  date_buf[i++] = 'T';
  date_buf[i++] = 13;
  date_buf[i++] = 10;
  return date_buf;
}

static
int _parse_http_request(char *buf, ssize_t buf_len, VALUE env) {
  const char* method;
  size_t method_len;
  const char* path;
  size_t path_len;
  int minor_version;
  struct phr_header headers[MAX_HEADERS];
  size_t num_headers, question_at;
  size_t i;
  int ret;
  char tmp[MAX_HEADER_NAME_LEN + sizeof("HTTP_") - 1] = "HTTP_";
  VALUE last_value;

  num_headers = MAX_HEADERS;
  ret = phr_parse_request(buf, buf_len, &method, &method_len, &path,
                          &path_len, &minor_version, headers, &num_headers, 0);
  if (ret < 0)
    goto done;

  rb_hash_aset(env, request_method_key, rb_str_new(method,method_len));
  rb_hash_aset(env, request_uri_key, rb_str_new(path, path_len));
  rb_hash_aset(env, script_name_key, vacant_string_val);
  rb_hash_aset(env, server_protocol_key, (minor_version == 1) ? http11_val : http10_val);

  /* PATH_INFO QUERY_STRING */
  path_len = find_ch(path, path_len, '#'); /* strip off all text after # after storing request_uri */
  question_at = find_ch(path, path_len, '?');
  if ( store_path_info(env, path, question_at) < 0 ) {
    rb_hash_clear(env);
    ret = -1;
    goto done;
  }
  if (question_at != path_len) ++question_at;
  rb_hash_aset(env, query_string_key, rb_str_new(path + question_at, path_len - question_at));
  last_value = Qnil;

  for (i = 0; i < num_headers; ++i) {
    if (headers[i].name != NULL) {
      const char* name;
      size_t name_len;
      VALUE slot;
      VALUE env_key;
      env_key = find_common_header(headers + i);
      if ( NIL_P(env_key) ) {
        const char* s;
        char* d;
        size_t n;
        if (sizeof(tmp) - 5 < headers[i].name_len) {
          rb_hash_clear(env);
          ret = -1;
          goto done;
        }
        for (s = headers[i].name, n = headers[i].name_len, d = tmp + 5;
          n != 0;
          s++, --n, d++) {
            *d = *s == '-' ? '_' : TOU(*s);
            name = tmp;
            name_len = headers[i].name_len + 5;
            env_key = rb_str_new(name, name_len);
        }
      }
      slot = rb_hash_aref(env, env_key);
      if ( !NIL_P(slot) ) {
        rb_str_cat2(slot, ", ");
        rb_str_cat(slot, headers[i].value, headers[i].value_len);
      } else {
        slot = rb_str_new(headers[i].value, headers[i].value_len);
        rb_hash_aset(env, env_key, slot);
        last_value = slot;
      }
    } else {
      // continuing lines of a mulitiline header
        if ( !NIL_P(last_value) )
          rb_str_cat(last_value, headers[i].value, headers[i].value_len);
    }
  }
 done:
  return ret;
}



static
VALUE rhe_accept(VALUE self, VALUE fileno, VALUE timeoutv, VALUE tcp, VALUE env) {
  struct sockaddr_in cliaddr;
  unsigned int len;
  char read_buf[MAX_HEADER_SIZE];
  VALUE req;
  int flag = 1;
  ssize_t rv = 0;
  ssize_t buf_len;
  ssize_t reqlen;
  int fd;
  double timeout = NUM2DBL(timeoutv);

  len = sizeof(cliaddr);
  fd = _accept(NUM2INT(fileno), (struct sockaddr *)&cliaddr, len);

  /* endif */
  if (fd < 0) {
    goto badexit;
  }

  rv = _read_timeout(fd, timeout, &read_buf[0], MAX_HEADER_SIZE);
  if ( rv <= 0 ) {
    close(fd);
    goto badexit;
  }

  if ( tcp == Qtrue ) {
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));
    rb_hash_aset(env, remote_addr_key, rb_str_new2(inet_ntoa(cliaddr.sin_addr)));
    rb_hash_aset(env, remote_port_key, rb_fix2str(INT2FIX(ntohs(cliaddr.sin_port)),10));
  }
  else {
    rb_hash_aset(env, remote_addr_key, vacant_string_val);
    rb_hash_aset(env, remote_port_key, zero_string_val);
  }

  buf_len = rv;
  while (1) {
    reqlen = _parse_http_request(&read_buf[0],buf_len,env);
    if ( reqlen >= 0 ) {
      break;
    }
    else if ( reqlen == -1 ) {
      /* error */
      close(fd);
      goto badexit;
    }
    if ( MAX_HEADER_SIZE - buf_len == 0 ) {
      /* too large header  */
     char* badreq;
     badreq = BAD_REQUEST;
     rv = _write_timeout(fd, timeout, badreq, sizeof(BAD_REQUEST) - 1);
     close(fd);
     goto badexit;
    }
    /* request is incomplete */
    rv = _read_timeout(fd, timeout, &read_buf[buf_len], MAX_HEADER_SIZE - buf_len);
    if ( rv <= 0 ) {
      close(fd);
      goto badexit;
    }
    buf_len += rv;
  }

  //rv = _write_timeout(fd, timeout, "HTTP/1.0 200 OK\r\nConnection: close\r\n\r\n200 OK\r\n",
  //                    sizeof("HTTP/1.0 200 OK\r\nConnection: close\r\n\r\n200 OK\r\n") - 1);
  //close(fd);
  //goto badexit;

  VALUE expect_val = rb_hash_aref(env, expect_key);
  if ( !NIL_P(expect_val) ) {
      if ( strncmp(RSTRING_PTR(expect_val), "100-continue", RSTRING_LEN(expect_val)) == 0 ) {
          rv = _write_timeout(fd, timeout, EXPECT_CONTINUE, sizeof(EXPECT_CONTINUE) - 1);
          if ( rv <= 0 ) {
              close(fd);
              goto badexit;
          }
      } else {
          rv = _write_timeout(fd, timeout, EXPECT_FAILED, sizeof(EXPECT_FAILED) - 1);
          close(fd);
          goto badexit;
      }
  }

  req = rb_ary_new2(2);
  rb_ary_push(req, INT2NUM(fd));
  rb_ary_push(req, rb_str_new(&read_buf[reqlen],buf_len - reqlen));
  return req;
 badexit:
  return Qnil;
}

static
VALUE rhe_read_timeout(VALUE self, VALUE filenov, VALUE rbuf, VALUE lenv, VALUE offsetv, VALUE timeoutv) {
  char * d;
  ssize_t rv;
  int fileno;
  double timeout;
  ssize_t offset;
  ssize_t len;
  fileno = NUM2INT(filenov);
  timeout = NUM2DBL(timeoutv);
  offset = NUM2LONG(offsetv);
  len = NUM2LONG(lenv);
  if ( len > READ_BUF )
    len = READ_BUF;
  d = ALLOC_N(char, len);
  rv = _read_timeout(fileno, timeout, &d[offset], len);
  if ( rv > 0 ) {
    rb_str_cat(rbuf, d, rv);
  }
  xfree(d);
  return SSIZET2NUM(rv);
}

static
VALUE rhe_write_timeout(VALUE self, VALUE fileno, VALUE buf, VALUE len, VALUE offset, VALUE timeout) {
  char* d;
  ssize_t rv;
  buf = rb_String(buf);
  d = RSTRING_PTR(buf);
  rv = _write_timeout(NUM2INT(fileno), NUM2DBL(timeout), &d[NUM2LONG(offset)], NUM2LONG(len));
  if ( rv < 0 ) {
    return Qnil;
  }
  return SSIZET2NUM(rv);
}


static
VALUE rhe_write_all(VALUE self, VALUE fileno, VALUE buf, VALUE offsetv, VALUE timeout) {
  char * d;
  ssize_t buf_len;
  ssize_t rv = 0;
  ssize_t written = 0;

  buf = rb_String(buf);
  d = RSTRING_PTR(buf);
  buf_len = RSTRING_LEN(buf);

  written = 0;
  while ( buf_len > written ) {
    rv = _write_timeout(NUM2INT(fileno), NUM2DBL(timeout), &d[written], buf_len - written);
    if ( rv <= 0 ) {
      break;
    }
    written += rv;
  }
  if (rv < 0) {
    return Qnil;
  }
  return SSIZET2NUM(written);
}

static
VALUE rhe_write_chunk(VALUE self, VALUE fileno, VALUE buf, VALUE offsetv, VALUE timeout) {
  ssize_t buf_len;
  ssize_t rv = 0;
  ssize_t written = 0;
  ssize_t vec_offset = 0;
  ssize_t count =0;
  ssize_t remain;
  ssize_t iovcnt = 3;
  char chunked_header_buf[18];

  buf = rb_String(buf);
  buf_len = RSTRING_LEN(buf);

  if ( buf_len == 0 ){
      return INT2FIX(0);
  }

  {
    struct iovec v[iovcnt]; // Needs C99 compiler
    v[0].iov_len = _chunked_header(chunked_header_buf,buf_len);
    v[0].iov_base = chunked_header_buf;
    v[1].iov_len = buf_len;
    v[1].iov_base = RSTRING_PTR(buf);
    v[2].iov_base = "\r\n";
    v[2].iov_len = sizeof("\r\n") -1;

    vec_offset = 0;
    written = 0;
    remain = iovcnt;
    while ( remain > 0 ) {
      count = (iovcnt > IOV_MAX) ? IOV_MAX : iovcnt;
      rv = _writev_timeout(NUM2INT(fileno), NUM2DBL(timeout),  &v[vec_offset], count - vec_offset, (vec_offset == 0) ? 0 : 1);
      if ( rv <= 0 ) {
        // error or disconnected
        break;
      }
      written += rv;
      while ( rv > 0 ) {
        if ( (unsigned int)rv >= v[vec_offset].iov_len ) {
          rv -= v[vec_offset].iov_len;
          vec_offset++;
          remain--;
        }
        else {
          v[vec_offset].iov_base = (char*)v[vec_offset].iov_base + rv;
          v[vec_offset].iov_len -= rv;
          rv = 0;
        }
      }
    }
  }
  if ( rv < 0 ) {
    return Qnil;
  }
  return SSIZET2NUM(written);
}

static
int header_to_array(VALUE key_obj, VALUE val_obj, VALUE ary) {
  ssize_t val_len;
  ssize_t val_offset;
  long val_lf;
  char * val;

  val_obj = rb_String(val_obj);
  val = RSTRING_PTR(val_obj);
  val_len = RSTRING_LEN(val_obj);
  val_offset = 0;
  val_lf = find_lf(val, val_offset, val_len);
  if ( val_lf < val_len ) {
    /* contain "\n" */
    while ( val_offset < val_len ) {
      if ( val_offset != val_lf ) {
        rb_ary_push(ary, key_obj);
        rb_ary_push(ary, rb_str_new(&val[val_offset],val_lf - val_offset));
      }
      val_offset = val_lf + 1;
      val_lf = find_lf(val, val_offset, val_len);
    }
  }
  else {
    rb_ary_push(ary, key_obj);
    rb_ary_push(ary, val_obj);
  }
  return ST_CONTINUE;
}

static
VALUE rhe_close(VALUE self, VALUE fileno) {
  close(NUM2INT(fileno));
  return Qnil;
}

static
VALUE rhe_write_response(VALUE self, VALUE filenov, VALUE timeoutv, VALUE status_codev, VALUE headers, VALUE body, VALUE use_chunkedv, VALUE header_onlyv) {
  ssize_t hlen = 0;
  ssize_t blen = 0;

  ssize_t rv = 0;
  ssize_t iovcnt;
  ssize_t vec_offset;
  ssize_t written;
  ssize_t count;
  int i;
  ssize_t remain;
  char status_line[512] = "HTTP/1.1 ";
  char * date_line;
  int date_pushed = 0;
  char * server_line;
  int server_pushed = 0;
  VALUE harr;
  VALUE key_obj;
  VALUE val_obj;
  char * key;
  ssize_t key_len;
  const char * message;

  const char * s;
  char * d;
  ssize_t n;

  char * chunked_header_buf;
  
  int fileno = NUM2INT(filenov);
  double timeout = NUM2DBL(timeoutv);
  int status_code = NUM2INT(status_codev);
  int use_chunked = NUM2INT(use_chunkedv);
  int header_only = NUM2INT(header_onlyv);

  /* status_with_no_entity_body */
  if ( status_code < 200 || status_code == 204 || status_code == 304 ) {
    use_chunked = 0;
  }
  
  harr = rb_ary_new2(RHASH_SIZE(headers) * 2);
  RB_GC_GUARD(harr);
  rb_hash_foreach(headers, header_to_array, harr);
  hlen = RARRAY_LEN(harr);
  blen = RARRAY_LEN(body);
  iovcnt = 10 + (hlen * 2) + blen;
  if ( use_chunked ) {
      iovcnt += blen*2;
      chunked_header_buf = ALLOC_N(char, 32 * blen);
  }

  {
    struct iovec v[iovcnt]; // Needs C99 compiler
    size_t mlen;
    /* status line */
    iovcnt = 0;
    i = sizeof("HTTP/1.1 ") - 1;
    str_i(status_line,&i,status_code,3);
    status_line[i++] = ' ';
    message = status_message(status_code, &mlen);
    str_s(status_line, &i, message, mlen);
    status_line[i++] = 13;
    status_line[i++] = 10;
    v[iovcnt].iov_base = status_line;
    v[iovcnt].iov_len = i;
    iovcnt++;

    /* for date header */
    iovcnt++;

    v[iovcnt].iov_base = "Server: Rhebok\r\n";
    v[iovcnt].iov_len = sizeof("Server: Rhebok\r\n")-1;
    iovcnt++;

    date_pushed = 0;

    for ( i = 0; i < hlen; i++ ) {
      key_obj = rb_ary_entry(harr, i);
      key = RSTRING_PTR(key_obj);
      key_len = RSTRING_LEN(key_obj);
      i++;

      if ( strncasecmp(key,"Connection",key_len) == 0 ) {
        continue;
      }

      val_obj = rb_ary_entry(harr, i);

      if ( strncasecmp(key,"Date",key_len) == 0 ) {
        date_line = ALLOC_N(char, sizeof("Date: ")-1 + RSTRING_LEN(val_obj) + sizeof("\r\n")-1);
        strcpy(date_line, "Date: ");
        for ( s=RSTRING_PTR(val_obj), n = RSTRING_LEN(val_obj), d=date_line+sizeof("Date: ")-1; n !=0; s++, --n, d++) {
          *d = *s;
        }
        date_line[sizeof("Date: ") -1 + RSTRING_LEN(val_obj)] = 13;
        date_line[sizeof("Date: ") -1 + RSTRING_LEN(val_obj) + 1] = 10;
        v[1].iov_base = date_line;
        v[1].iov_len = sizeof("Date: ") -1 + RSTRING_LEN(val_obj) + 2;
        date_pushed = 1;
        continue;
      }
      if ( strncasecmp(key,"Server",key_len) == 0 ) {
        server_line = ALLOC_N(char, sizeof("Server: ")-1 + RSTRING_LEN(val_obj) + sizeof("\r\n")-1);
        strcpy(server_line, "Server: ");
        for ( s=RSTRING_PTR(val_obj), n = RSTRING_LEN(val_obj), d=server_line+sizeof("Server: ")-1; n !=0; s++, --n, d++) {
          *d = *s;
        }
        server_line[sizeof("Server: ") -1 + RSTRING_LEN(val_obj)] = 13;
        server_line[sizeof("Server: ") -1 + RSTRING_LEN(val_obj) + 1] = 10;
        v[2].iov_base = server_line;
        v[2].iov_len = sizeof("Server: ") -1 + RSTRING_LEN(val_obj) + 2;
        server_pushed = 1;
        continue;
      }

      v[iovcnt].iov_base = key;
      v[iovcnt].iov_len = key_len;
      iovcnt++;
      v[iovcnt].iov_base = ": ";
      v[iovcnt].iov_len = sizeof(": ") - 1;
      iovcnt++;

      // value
      v[iovcnt].iov_base = RSTRING_PTR(val_obj);
      v[iovcnt].iov_len = RSTRING_LEN(val_obj);
      iovcnt++;
      v[iovcnt].iov_base = "\r\n";
      v[iovcnt].iov_len = sizeof("\r\n") - 1;
      iovcnt++;
    }

    if ( date_pushed == 0 ) {
        v[1].iov_len = sizeof("Date: Sat, 19 Dec 2015 14:16:27 GMT\r\n") - 1;
        v[1].iov_base = _date_header();
    }

    if ( use_chunked ) {
      v[iovcnt].iov_base = "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
      v[iovcnt].iov_len = sizeof("Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n") - 1;
      iovcnt++;
    }
    else {
        v[iovcnt].iov_base = "Connection: close\r\n\r\n";
        v[iovcnt].iov_len = sizeof("Connection: close\r\n\r\n") - 1;
        iovcnt++;
    }

    ssize_t chb_offset = 0;
    for ( i=0; i<blen; i++) {
      val_obj = rb_String(rb_ary_entry(body, i));
      if ( RSTRING_LEN(val_obj) == 0 ) {
          continue;
      }
      if ( use_chunked ) {
          v[iovcnt].iov_len = _chunked_header(&chunked_header_buf[chb_offset],RSTRING_LEN(val_obj));
          v[iovcnt].iov_base = &chunked_header_buf[chb_offset];
          chb_offset += v[iovcnt].iov_len;
          iovcnt++;
      }
      v[iovcnt].iov_base = RSTRING_PTR(val_obj);
      v[iovcnt].iov_len = RSTRING_LEN(val_obj);
      iovcnt++;
      if ( use_chunked ) {
          v[iovcnt].iov_base = "\r\n";
          v[iovcnt].iov_len = sizeof("\r\n") -1;
          iovcnt++;
      }
    }
    if ( use_chunked && header_only == 0 ) {
      v[iovcnt].iov_base = "0\r\n\r\n";
      v[iovcnt].iov_len = sizeof("0\r\n\r\n") - 1;
      iovcnt++;
    }

    vec_offset = 0;
    written = 0;
    remain = iovcnt;
    while ( remain > 0 ) {
      count = (iovcnt > IOV_MAX) ? IOV_MAX : iovcnt;
      rv = _writev_timeout(fileno, timeout,  &v[vec_offset], count - vec_offset, (vec_offset == 0) ? 0 : 1);
      if ( rv <= 0 ) {
        // error or disconnected
        break;
      }
      written += rv;
      while ( rv > 0 ) {
        if ( (unsigned int)rv >= v[vec_offset].iov_len ) {
          rv -= v[vec_offset].iov_len;
          vec_offset++;
          remain--;
        }
        else {
          v[vec_offset].iov_base = (char*)v[vec_offset].iov_base + rv;
          v[vec_offset].iov_len -= rv;
          rv = 0;
        }
      }
    }
  }
  if ( use_chunked )
      xfree(chunked_header_buf);
  if ( server_pushed )
      xfree(server_line);
  if ( date_pushed )
      xfree(date_line);
  if ( rv < 0 ) {
    return Qnil;
  }
  return SSIZET2NUM(written);
}

void Init_rhebok()
{
  request_method_key = rb_obj_freeze(rb_str_new2("REQUEST_METHOD"));
  rb_gc_register_address(&request_method_key);
  path_info_key = rb_obj_freeze(rb_str_new2("PATH_INFO"));
  rb_gc_register_address(&path_info_key);
  request_uri_key = rb_obj_freeze(rb_str_new2("REQUEST_URI"));
  rb_gc_register_address(&request_uri_key);
  script_name_key = rb_obj_freeze(rb_str_new2("SCRIPT_NAME"));
  rb_gc_register_address(&script_name_key);
  server_protocol_key = rb_obj_freeze(rb_str_new2("SERVER_PROTOCOL"));
  rb_gc_register_address(&server_protocol_key);
  query_string_key = rb_obj_freeze(rb_str_new2("QUERY_STRING"));
  rb_gc_register_address(&query_string_key);
  remote_addr_key = rb_obj_freeze(rb_str_new2("REMOTE_ADDR"));
  rb_gc_register_address(&remote_addr_key);
  remote_port_key = rb_obj_freeze(rb_str_new2("REMOTE_PORT"));
  rb_gc_register_address(&remote_port_key);

  vacant_string_val = rb_obj_freeze(rb_str_new("",0));
  rb_gc_register_address(&vacant_string_val);
  zero_string_val = rb_obj_freeze(rb_str_new("0",1));
  rb_gc_register_address(&zero_string_val);

  http10_val = rb_obj_freeze(rb_str_new2("HTTP/1.0"));
  rb_gc_register_address(&http10_val);
  http11_val = rb_obj_freeze(rb_str_new2("HTTP/1.1"));
  rb_gc_register_address(&http11_val);

  expect_key = rb_obj_freeze(rb_str_new2("HTTP_EXPECT"));
  rb_gc_register_address(&expect_key);

  set_common_header("HOST",sizeof("HOST") - 1, 0);
  set_common_header("ACCEPT",sizeof("ACCEPT") - 1, 0);
  set_common_header("ACCEPT-ENCODING",sizeof("ACCEPT-ENCODING") - 1, 0);
  set_common_header("ACCEPT-LANGUAGE",sizeof("ACCEPT-LANGUAGE") - 1, 0);
  set_common_header("CACHE-CONTROL",sizeof("CACHE-CONTROL") - 1, 0);
  set_common_header("CONNECTION",sizeof("CONNECTION") - 1, 0);
  set_common_header("CONTENT-LENGTH",sizeof("CONTENT-LENGTH") - 1, 1);
  set_common_header("CONTENT-TYPE",sizeof("CONTENT-TYPE") - 1, 1);
  set_common_header("COOKIE",sizeof("COOKIE") - 1, 0);
  set_common_header("IF-MODIFIED-SINCE",sizeof("IF-MODIFIED-SINCE") - 1, 0);
  set_common_header("REFERER",sizeof("REFERER") - 1, 0);
  set_common_header("USER-AGENT",sizeof("USER-AGENT") - 1, 0);
  set_common_header("X-FORWARDED-FOR",sizeof("X-FORWARDED-FOR") - 1, 0);

  cRhebok = rb_const_get(rb_cObject, rb_intern("Rhebok"));
  rb_define_module_function(cRhebok, "accept_rack", rhe_accept, 4);
  rb_define_module_function(cRhebok, "read_timeout", rhe_read_timeout, 5);
  rb_define_module_function(cRhebok, "write_timeout", rhe_write_timeout, 5);
  rb_define_module_function(cRhebok, "write_all", rhe_write_all, 4);
  rb_define_module_function(cRhebok, "write_chunk", rhe_write_chunk, 4);
  rb_define_module_function(cRhebok, "close_rack", rhe_close, 1);
  rb_define_module_function(cRhebok, "write_response", rhe_write_response, 7);
}
