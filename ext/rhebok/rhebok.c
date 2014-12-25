#include <ruby.h>
#include <time.h>
#include <ctype.h>
#include <poll.h>
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

#define MAX_HEADER_SIZE 16384
#define MAX_HEADER_NAME_LEN 1024
#define MAX_HEADERS         128
#define BAD_REQUEST "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\n400 Bad Request\r\n"
#define TOU(ch) (('a' <= ch && ch <= 'z') ? ch - ('a' - 'A') : ch)

static const char *DoW[] = {
  "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
};
static const char *MoY[] = {
  "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};

/* stolen from HTTP::Status and Feersum */
/* Unmarked codes are from RFC 2616 */
/* See also: http://en.wikipedia.org/wiki/List_of_HTTP_status_codes */
static const char *
status_message (int code) {
  switch (code) {
    case 100: return "Continue";
    case 101: return "Switching Protocols";
    case 102: return "Processing";                      /* RFC 2518 (WebDAV) */
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 203: return "Non-Authoritative Information";
    case 204: return "No Content";
    case 205: return "Reset Content";
    case 206: return "Partial Content";
    case 207: return "Multi-Status";                    /* RFC 2518 (WebDAV) */
    case 208: return "Already Reported";              /* RFC 5842 */
    case 300: return "Multiple Choices";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 305: return "Use Proxy";
    case 307: return "Temporary Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Request Entity Too Large";
    case 414: return "Request-URI Too Large";
    case 415: return "Unsupported Media Type";
    case 416: return "Request Range Not Satisfiable";
    case 417: return "Expectation Failed";
    case 418: return "I'm a teapot";              /* RFC 2324 */
    case 422: return "Unprocessable Entity";            /* RFC 2518 (WebDAV) */
    case 423: return "Locked";                          /* RFC 2518 (WebDAV) */
    case 424: return "Failed Dependency";               /* RFC 2518 (WebDAV) */
    case 425: return "No code";                         /* WebDAV Advanced Collections */
    case 426: return "Upgrade Required";                /* RFC 2817 */
    case 428: return "Precondition Required";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 449: return "Retry with";                      /* unofficial Microsoft */
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 505: return "HTTP Version Not Supported";
    case 506: return "Variant Also Negotiates";         /* RFC 2295 */
    case 507: return "Insufficient Storage";            /* RFC 2518 (WebDAV) */
    case 509: return "Bandwidth Limit Exceeded";        /* unofficial */
    case 510: return "Not Extended";                    /* RFC 2774 */
    case 511: return "Network Authentication Required";
    default: break;
  }
  /* default to the Nxx group names in RFC 2616 */
  if (100 <= code && code <= 199) {
    return "Informational";
  }
  else if (200 <= code && code <= 299) {
    return "Success";
    }
    else if (300 <= code && code <= 399) {
        return "Redirection";
    }
    else if (400 <= code && code <= 499) {
        return "Client Error";
    }
    else {
        return "Error";
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

struct common_header {
  const char * name;
  size_t name_len;
  VALUE key;
};
static int common_headers_num = 0;
static struct common_header common_headers[20];

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
  for ( i=offset; i != len; ++i) {
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
  size_t dlen = 0, i = 0;
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
ssize_t _writev_timeout(const int fileno, const double timeout, struct iovec *iovec, const int iovcnt, const int do_select ) {
  int rv;
  int nfound;
  struct pollfd wfds[1];
  if ( do_select == 1) goto WAIT_WRITE;
 DO_WRITE:
  rv = writev(fileno, iovec, iovcnt);
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
    nfound = poll(wfds, 1, (int)timeout*1000);
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
ssize_t _read_timeout(const int fileno, const double timeout, char * read_buf, const int read_len ) {
  int rv;
  int nfound;
  struct pollfd rfds[1];
 DO_READ:
  rfds[0].fd = fileno;
  rfds[0].events = POLLIN;
  rv = read(fileno, read_buf, read_len);
  if ( rv >= 0 ) {
    return rv;
  }
  if ( rv < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK ) {
    return rv;
  }
  while (1) {
    nfound = poll(rfds, 1, (int)timeout*1000);
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
ssize_t _write_timeout(const int fileno, const double timeout, char * write_buf, const int write_len ) {
  int rv;
  int nfound;
  struct pollfd wfds[1];
 DO_WRITE:
  rv = write(fileno, write_buf, write_len);
  if ( rv >= 0 ) {
    return rv;
  }
  if ( rv < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK ) {
    return rv;
  }
  while (1) {
    wfds[0].fd = fileno;
    wfds[0].events = POLLOUT;
    nfound = poll(wfds, 1, (int)timeout*1000);
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
void str_s(char * dst, int *dst_len, const char * src, int src_len) {
  int i;
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
int _date_line(char * date_line) {
  struct tm gtm;
  time_t lt;
  int i = 0;
  time(&lt);
  gmtime_r(&lt, &gtm);
  date_line[i++] = 'D';
  date_line[i++] = 'a';
  date_line[i++] = 't';
  date_line[i++] = 'e';
  date_line[i++] = ':';
  date_line[i++] = ' ';
  str_s(date_line, &i, DoW[gtm.tm_wday], 3);
  date_line[i++] = ',';
  date_line[i++] = ' ';
  str_i(date_line, &i, gtm.tm_mday, 2);
  date_line[i++] = ' ';
  str_s(date_line, &i, MoY[gtm.tm_mon], 3);
  date_line[i++] = ' ';
  str_i(date_line, &i, gtm.tm_year + 1900, 4);
  date_line[i++] = ' ';
  str_i(date_line, &i, gtm.tm_hour,2);
  date_line[i++] = ':';
  str_i(date_line, &i, gtm.tm_min,2);
  date_line[i++] = ':';
  str_i(date_line, &i, gtm.tm_sec,2);
  date_line[i++] = ' ';
  date_line[i++] = 'G';
  date_line[i++] = 'M';
  date_line[i++] = 'T';
  date_line[i++] = 13;
  date_line[i++] = 10;
  return i;
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
  char tmp[MAX_HEADER_NAME_LEN + sizeof("HTTP_") - 1];
  VALUE last_value;

  num_headers = MAX_HEADERS;
  ret = phr_parse_request(buf, buf_len, &method, &method_len, &path,
                          &path_len, &minor_version, headers, &num_headers, 0);
  if (ret < 0)
    goto done;

  rb_hash_aset(env, request_method_key, rb_str_new(method,method_len));
  rb_hash_aset(env, request_uri_key, rb_str_new(path, path_len));
  rb_hash_aset(env, script_name_key, vacant_string_val);
  rb_hash_aset(env, server_protocol_key, (minor_version > 1 || minor_version < 0 ) ? http10_val : http11_val);

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
      if ( env_key == Qnil ) {
        const char* s;
        char* d;
        size_t n;
        if (sizeof(tmp) - 5 < headers[i].name_len) {
          rb_hash_clear(env);
          ret = -1;
          goto done;
        }
        strcpy(tmp, "HTTP_");
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
      if ( slot != Qnil ) {
        rb_str_cat2(slot, ", ");
        rb_str_cat(slot, headers[i].value, headers[i].value_len);
      } else {
        slot = rb_str_new(headers[i].value, headers[i].value_len);
        rb_hash_aset(env, env_key, slot);
        last_value = slot;
      }
    } else {
      /* continuing lines of a mulitiline header */
      if ( last_value != Qnil )
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
    rb_hash_aset(env, remote_port_key, rb_String(rb_int_new(ntohs(cliaddr.sin_port))));
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

  req = rb_ary_new2(2);
  rb_ary_push(req, rb_int_new(fd));
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
  d = ALLOC_N(char, len);
  rv = _read_timeout(fileno, timeout, &d[offset], len);
  if ( rv > 0 ) {
    rb_str_cat(rbuf, d, rv);
  }
  xfree(d);
  return rb_int_new(rv);
}

static
VALUE rhe_write_timeout(VALUE self, VALUE fileno, VALUE buf, VALUE len, VALUE offset, VALUE timeout) {
  char* d;
  ssize_t rv;

  d = RSTRING_PTR(buf);
  rv = _write_timeout(NUM2INT(fileno), NUM2DBL(timeout), &d[NUM2LONG(offset)], NUM2LONG(len));
  if ( rv < 0 ) {
    return Qnil;
  }
  return rb_int_new(rv);
}


static
VALUE rhe_write_all(VALUE self, VALUE fileno, VALUE buf, VALUE offsetv, VALUE timeout) {
  char * d;
  ssize_t buf_len;
  ssize_t rv = 0;
  ssize_t written = 0;

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
  return rb_int_new(written);
}

static
int header_to_array(VALUE key_obj, VALUE val_obj, VALUE ary) {
  ssize_t val_len;
  ssize_t val_offset;
  long val_lf;
  char * val;

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
VALUE rhe_write_response(VALUE self, VALUE filenov, VALUE timeoutv, VALUE status_codev, VALUE headers, VALUE body) {
  ssize_t hlen = 0;
  ssize_t blen = 0;

  ssize_t rv = 0;
  ssize_t iovcnt;
  ssize_t vec_offset;
  ssize_t written;
  int count;
  int i;
  char status_line[512];
  char date_line[512];
  char server_line[1032];
  int date_pushed = 0;
  VALUE harr;
  VALUE key_obj;
  VALUE val_obj;
  char * key;
  ssize_t key_len;
  const char * message;

  const char * s;
  char* d;
  ssize_t n;

  int fileno = NUM2INT(filenov);
  double timeout = NUM2DBL(timeoutv);
  int status_code = NUM2INT(status_codev);

  harr = rb_ary_new();
  RB_GC_GUARD(harr);
  rb_hash_foreach(headers, header_to_array, harr);
  hlen = RARRAY_LEN(harr);
  blen = RARRAY_LEN(body);
  iovcnt = 10 + (hlen * 2) + blen;

  {
    struct iovec v[iovcnt]; // Needs C99 compiler
    /* status line */
    iovcnt = 0;
    i=0;
    status_line[i++] = 'H';
    status_line[i++] = 'T';
    status_line[i++] = 'T';
    status_line[i++] = 'P';
    status_line[i++] = '/';
    status_line[i++] = '1';
    status_line[i++] = '.';
    status_line[i++] = '0';
    status_line[i++] = ' ';
    str_i(status_line,&i,status_code,3);
    status_line[i++] = ' ';
    message = status_message(status_code);
    str_s(status_line, &i, message, strlen(message));
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
        strcpy(server_line, "Server: ");
        for ( s=RSTRING_PTR(val_obj), n = RSTRING_LEN(val_obj), d=server_line+sizeof("Server: ")-1; n !=0; s++, --n, d++) {
          *d = *s;
        }
        server_line[sizeof("Server: ") -1 + RSTRING_LEN(val_obj)] = 13;
        server_line[sizeof("Server: ") -1 + RSTRING_LEN(val_obj) + 1] = 10;
        v[2].iov_base = server_line;
        v[2].iov_len = sizeof("Server: ") -1 + RSTRING_LEN(val_obj) + 2;
        continue;
      }

      v[iovcnt].iov_base = key;
      v[iovcnt].iov_len = key_len;
      iovcnt++;
      v[iovcnt].iov_base = ": ";
      v[iovcnt].iov_len = sizeof(": ") - 1;
      iovcnt++;

      /* value */
      v[iovcnt].iov_base = RSTRING_PTR(val_obj);
      v[iovcnt].iov_len = RSTRING_LEN(val_obj);
      iovcnt++;
      v[iovcnt].iov_base = "\r\n";
      v[iovcnt].iov_len = sizeof("\r\n") - 1;
      iovcnt++;
    }

    if ( date_pushed == 0 ) {
      v[1].iov_len = _date_line(date_line);
      v[1].iov_base = date_line;
    }

    v[iovcnt].iov_base = "Connection: close\r\n\r\n";
    v[iovcnt].iov_len = sizeof("Connection: close\r\n\r\n") - 1;
    iovcnt++;

    for ( i=0; i<blen; i++) {
      val_obj = rb_ary_entry(body, i);
      v[iovcnt].iov_base = RSTRING_PTR(val_obj);
      v[iovcnt].iov_len = RSTRING_LEN(val_obj);
      iovcnt++;
    }

    vec_offset = 0;
    written = 0;
    while ( iovcnt - vec_offset > 0 ) {
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
  return rb_int_new(written);
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

  set_common_header("ACCEPT",sizeof("ACCEPT") - 1, 0);
  set_common_header("ACCEPT-ENCODING",sizeof("ACCEPT-ENCODING") - 1, 0);
  set_common_header("ACCEPT-LANGUAGE",sizeof("ACCEPT-LANGUAGE") - 1, 0);
  set_common_header("CACHE-CONTROL",sizeof("CACHE-CONTROL") - 1, 0);
  set_common_header("CONNECTION",sizeof("CONNECTION") - 1, 0);
  set_common_header("CONTENT-LENGTH",sizeof("CONTENT-LENGTH") - 1, 1);
  set_common_header("CONTENT-TYPE",sizeof("CONTENT-TYPE") - 1, 1);
  set_common_header("COOKIE",sizeof("COOKIE") - 1, 0);
  set_common_header("HOST",sizeof("HOST") - 1, 0);
  set_common_header("IF-MODIFIED-SINCE",sizeof("IF-MODIFIED-SINCE") - 1, 0);
  set_common_header("REFERER",sizeof("REFERER") - 1, 0);
  set_common_header("USER-AGENT",sizeof("USER-AGENT") - 1, 0);
  set_common_header("X-FORWARDED-FOR",sizeof("X-FORWARDED-FOR") - 1, 0);

  cRhebok = rb_const_get(rb_cObject, rb_intern("Rhebok"));
  rb_define_module_function(cRhebok, "accept_rack", rhe_accept, 4);
  rb_define_module_function(cRhebok, "read_timeout", rhe_read_timeout, 5);
  rb_define_module_function(cRhebok, "write_timeout", rhe_write_timeout, 5);
  rb_define_module_function(cRhebok, "write_all", rhe_write_all, 4);
  rb_define_module_function(cRhebok, "close_rack", rhe_close, 1);
  rb_define_module_function(cRhebok, "write_response", rhe_write_response, 5);
}
