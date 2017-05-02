#include <stddef.h>
#include <sys/types.h>

typedef int sock_t;

/* Macro for initializing mg_str. */
#define MG_MK_STR(str_literal) \
  { str_literal, sizeof(str_literal) - 1 }

/* Describes chunk of memory */
struct mg_str {
  const char *p; /* Memory chunk pointer */
  size_t len;    /* Memory chunk length */
};

/* HTTP and websocket events. void *ev_data is described in a comment. */
#define MG_EV_HTTP_REQUEST 100 /* struct http_message * */
#define MG_EV_HTTP_CHUNK 102   /* struct http_message * */

#define DIRSEP '\\'

#define MG_MAX_HTTP_HEADERS 20
#define MG_MAX_HTTP_REQUEST_SIZE 1024
#define MG_MAX_PATH 256
#define MG_MAX_HTTP_SEND_MBUF 1024
#define MG_CGI_ENVIRONMENT_SIZE 8192

#define MG_ENABLE_FILESYSTEM 1

/* HTTP message */
struct http_message {
  struct mg_str message; /* Whole message: request line + headers + body */

  /* HTTP Request line (or HTTP response line) */
  struct mg_str method; /* "GET" */
  struct mg_str uri;    /* "/my_file.html" */
  struct mg_str proto;  /* "HTTP/1.1" -- for both request and response */

  /* For responses, code and response status message are set */
  int resp_code;
  struct mg_str resp_status_msg;

  /*
   * Query-string part of the URI. For example, for HTTP request
   *    GET /foo/bar?param1=val1&param2=val2
   *    |    uri    |     query_string     |
   *
   * Note that question mark character doesn't belong neither to the uri,
   * nor to the query_string
   */
  struct mg_str query_string;

  /* Headers */
  struct mg_str header_names[MG_MAX_HTTP_HEADERS];
  struct mg_str header_values[MG_MAX_HTTP_HEADERS];

  /* Message body */
  struct mg_str body; /* Zero-length for requests with no body */
};

union socket_address {
  struct sockaddr sa;
  struct sockaddr_in sin;
#if MG_ENABLE_IPV6
  struct sockaddr_in6 sin6;
#else
  struct sockaddr sin6;
#endif
};

/*
 * Sends `printf`-style formatted data to the connection.
 *
 * See `mg_send` for more details on send semantics.
 */
int mg_printf(struct mg_connection *, const char *fmt, ...);

struct mg_connection;

/*
 * Callback function (event handler) prototype. Must be defined by the user.
 * Mongoose calls the event handler, passing the events defined below.
 */
typedef void (*mg_event_handler_t)(struct mg_connection *nc, int ev,
                                   void *ev_data);

/* Events. Meaning of event parameter (evp) is given in the comment. */
#define MG_EV_POLL 0    /* Sent to each connection on each mg_mgr_poll() call */
#define MG_EV_ACCEPT 1  /* New connection accepted. union socket_address * */
#define MG_EV_CONNECT 2 /* connect() succeeded or failed. int *  */
#define MG_EV_RECV 3    /* Data has benn received. int *num_bytes */
#define MG_EV_SEND 4    /* Data has been written to a socket. int *num_bytes */
#define MG_EV_CLOSE 5   /* Connection is closed. NULL */
#define MG_EV_TIMER 6   /* now >= conn->ev_timer_time. double * */

/*
 * Mongoose event manager.
 */
struct mg_mgr {
  struct mg_connection *active_connections;
#if MG_ENABLE_HEXDUMP
  const char *hexdump_file; /* Debug hexdump file path */
#endif
#if MG_ENABLE_BROADCAST
  sock_t ctl[2]; /* Socketpair for mg_broadcast() */
#endif
  void *user_data; /* User data */
  int num_ifaces;
  struct mg_iface **ifaces; /* network interfaces */
#if MG_ENABLE_JAVASCRIPT
  struct v7 *v7;
#endif
};

/*
 * Mongoose connection.
 */
struct mg_connection {
  struct mg_connection *next, *prev; /* mg_mgr::active_connections linkage */
  struct mg_connection *listener;    /* Set only for accept()-ed connections */
  struct mg_mgr *mgr;                /* Pointer to containing manager */

  sock_t sock; /* Socket to the remote peer */
  int err;
  union socket_address sa; /* Remote peer address */
  size_t recv_mbuf_limit;  /* Max size of recv buffer */
  struct mbuf recv_mbuf;   /* Received data */
  struct mbuf send_mbuf;   /* Data scheduled for sending */
  time_t last_io_time;     /* Timestamp of the last socket IO */
  double ev_timer_time;    /* Timestamp of the future MG_EV_TIMER */
#if MG_ENABLE_SSL
  void *ssl_if_data; /* SSL library data. */
#endif
  mg_event_handler_t proto_handler; /* Protocol-specific event handler */
  void *proto_data;                 /* Protocol-specific data */
  void (*proto_data_destructor)(void *proto_data);
  mg_event_handler_t handler; /* Event handler function */
  void *user_data;            /* User-specific data */
  union {
    void *v;
    /*
     * the C standard is fussy about fitting function pointers into
     * void pointers, since some archs might have fat pointers for functions.
     */
    mg_event_handler_t f;
  } priv_1;
  void *priv_2;
  void *mgr_data; /* Implementation-specific event manager's data. */
  struct mg_iface *iface;
  unsigned long flags;
/* Flags set by Mongoose */
#define MG_F_LISTENING (1 << 0)          /* This connection is listening */
#define MG_F_UDP (1 << 1)                /* This connection is UDP */
#define MG_F_RESOLVING (1 << 2)          /* Waiting for async resolver */
#define MG_F_CONNECTING (1 << 3)         /* connect() call in progress */
#define MG_F_SSL (1 << 4)                /* SSL is enabled on the connection */
#define MG_F_SSL_HANDSHAKE_DONE (1 << 5) /* SSL hanshake has completed */
#define MG_F_WANT_READ (1 << 6)          /* SSL specific */
#define MG_F_WANT_WRITE (1 << 7)         /* SSL specific */
#define MG_F_IS_WEBSOCKET (1 << 8)       /* Websocket specific */

/* Flags that are settable by user */
#define MG_F_SEND_AND_CLOSE (1 << 10)       /* Push remaining data and close  */
#define MG_F_CLOSE_IMMEDIATELY (1 << 11)    /* Disconnect */
#define MG_F_WEBSOCKET_NO_DEFRAG (1 << 12)  /* Websocket specific */
#define MG_F_DELETE_CHUNK (1 << 13)         /* HTTP specific */
#define MG_F_ENABLE_BROADCAST (1 << 14)     /* Allow broadcast address usage */
#define MG_F_TUN_DO_NOT_RECONNECT (1 << 15) /* Don't reconnect tunnel */

#define MG_F_USER_1 (1 << 20) /* Flags left for application */
#define MG_F_USER_2 (1 << 21)
#define MG_F_USER_3 (1 << 22)
#define MG_F_USER_4 (1 << 23)
#define MG_F_USER_5 (1 << 24)
#define MG_F_USER_6 (1 << 25)
};

#if MG_ENABLE_FILESYSTEM
/*
 * This structure defines how `mg_serve_http()` works.
 * Best practice is to set only required settings, and leave the rest as NULL.
 */
struct mg_serve_http_opts {
  /* Path to web root directory */
  const char *document_root;

  /* List of index files. Default is "" */
  const char *index_files;

  /*
   * Leave as NULL to disable authentication.
   * To enable directory protection with authentication, set this to ".htpasswd"
   * Then, creating ".htpasswd" file in any directory automatically protects
   * it with digest authentication.
   * Use `mongoose` web server binary, or `htdigest` Apache utility to
   * create/manipulate passwords file.
   * Make sure `auth_domain` is set to a valid domain name.
   */
  const char *per_directory_auth_file;

  /* Authorization domain (domain name of this web server) */
  const char *auth_domain;

  /*
   * Leave as NULL to disable authentication.
   * Normally, only selected directories in the document root are protected.
   * If absolutely every access to the web server needs to be authenticated,
   * regardless of the URI, set this option to the path to the passwords file.
   * Format of that file is the same as ".htpasswd" file. Make sure that file
   * is located outside document root to prevent people fetching it.
   */
  const char *global_auth_file;

  /* Set to "no" to disable directory listing. Enabled by default. */
  const char *enable_directory_listing;

  /*
   * SSI files pattern. If not set, "**.shtml$|**.shtm$" is used.
   *
   * All files that match ssi_pattern are treated as SSI.
   *
   * Server Side Includes (SSI) is a simple interpreted server-side scripting
   * language which is most commonly used to include the contents of a file
   * into a web page. It can be useful when it is desirable to include a common
   * piece of code throughout a website, for example, headers and footers.
   *
   * In order for a webpage to recognize an SSI-enabled HTML file, the
   * filename should end with a special extension, by default the extension
   * should be either .shtml or .shtm
   *
   * Unknown SSI directives are silently ignored by Mongoose. Currently,
   * the following SSI directives are supported:
   *    &lt;!--#include FILE_TO_INCLUDE --&gt;
   *    &lt;!--#exec "COMMAND_TO_EXECUTE" --&gt;
   *    &lt;!--#call COMMAND --&gt;
   *
   * Note that &lt;!--#include ...> directive supports three path
   *specifications:
   *
   * &lt;!--#include virtual="path" --&gt;  Path is relative to web server root
   * &lt;!--#include abspath="path" --&gt;  Path is absolute or relative to the
   *                                  web server working dir
   * &lt;!--#include file="path" --&gt;,    Path is relative to current document
   * &lt;!--#include "path" --&gt;
   *
   * The include directive may be used to include the contents of a file or
   * the result of running a CGI script.
   *
   * The exec directive is used to execute
   * a command on a server, and show command's output. Example:
   *
   * &lt;!--#exec "ls -l" --&gt;
   *
   * The call directive is a way to invoke a C handler from the HTML page.
   * On each occurence of &lt;!--#call COMMAND OPTIONAL_PARAMS> directive,
   * Mongoose calls a registered event handler with MG_EV_SSI_CALL event,
   * and event parameter will point to the COMMAND OPTIONAL_PARAMS string.
   * An event handler can output any text, for example by calling
   * `mg_printf()`. This is a flexible way of generating a web page on
   * server side by calling a C event handler. Example:
   *
   * &lt;!--#call foo --&gt; ... &lt;!--#call bar --&gt;
   *
   * In the event handler:
   *    case MG_EV_SSI_CALL: {
   *      const char *param = (const char *) ev_data;
   *      if (strcmp(param, "foo") == 0) {
   *        mg_printf(c, "hello from foo");
   *      } else if (strcmp(param, "bar") == 0) {
   *        mg_printf(c, "hello from bar");
   *      }
   *      break;
   *    }
   */
  const char *ssi_pattern;

  /* IP ACL. By default, NULL, meaning all IPs are allowed to connect */
  const char *ip_acl;

#if MG_ENABLE_HTTP_URL_REWRITES
  /* URL rewrites.
   *
   * Comma-separated list of `uri_pattern=url_file_or_directory_path` rewrites.
   * When HTTP request is received, Mongoose constructs a file name from the
   * requested URI by combining `document_root` and the URI. However, if the
   * rewrite option is used and `uri_pattern` matches requested URI, then
   * `document_root` is ignored. Instead, `url_file_or_directory_path` is used,
   * which should be a full path name or a path relative to the web server's
   * current working directory. It can also be an URI (http:// or https://)
   * in which case mongoose will behave as a reverse proxy for that destination.
   *
   * Note that `uri_pattern`, as all Mongoose patterns, is a prefix pattern.
   *
   * If uri_pattern starts with `@` symbol, then Mongoose compares it with the
   * HOST header of the request. If they are equal, Mongoose sets document root
   * to `file_or_directory_path`, implementing virtual hosts support.
   * Example: `@foo.com=/document/root/for/foo.com`
   *
   * If `uri_pattern` starts with `%` symbol, then Mongoose compares it with
   * the listening port. If they match, then Mongoose issues a 301 redirect.
   * For example, to redirect all HTTP requests to the
   * HTTPS port, do `%80=https://my.site.com`. Note that the request URI is
   * automatically appended to the redirect location.
   */
  const char *url_rewrites;
#endif

  /* DAV document root. If NULL, DAV requests are going to fail. */
  const char *dav_document_root;

  /*
   * DAV passwords file. If NULL, DAV requests are going to fail.
   * If passwords file is set to "-", then DAV auth is disabled.
   */
  const char *dav_auth_file;

  /* Glob pattern for the files to hide. */
  const char *hidden_file_pattern;

  /* Set to non-NULL to enable CGI, e.g. **.cgi$|**.php$" */
  const char *cgi_file_pattern;

  /* If not NULL, ignore CGI script hashbang and use this interpreter */
  const char *cgi_interpreter;

  /*
   * Comma-separated list of Content-Type overrides for path suffixes, e.g.
   * ".txt=text/plain; charset=utf-8,.c=text/plain"
   */
  const char *custom_mime_types;

  /*
   * Extra HTTP headers to add to each server response.
   * Example: to enable CORS, set this to "Access-Control-Allow-Origin: *".
   */
  const char *extra_headers;
};
#endif

/*
 * Optional parameters to `mg_bind_opt()`.
 *
 * `flags` is an initial `struct mg_connection::flags` bitmask to set,
 * see `MG_F_*` flags definitions.
 */
struct mg_bind_opts {
  void *user_data;           /* Initial value for connection's user_data */
  unsigned int flags;        /* Extra connection flags */
  const char **error_string; /* Placeholder for the error string */
  struct mg_iface *iface;    /* Interface instance */
#if MG_ENABLE_SSL
  /*
   * SSL settings.
   *
   * Server certificate to present to clients or client certificate to
   * present to tunnel dispatcher (for tunneled connections).
   */
  const char *ssl_cert;
  /* Private key corresponding to the certificate. If ssl_cert is set but
   * ssl_key is not, ssl_cert is used. */
  const char *ssl_key;
  /* CA bundle used to verify client certificates or tunnel dispatchers. */
  const char *ssl_ca_cert;
  /* Colon-delimited list of acceptable cipher suites.
   * Names depend on the library used, for example:
   *
   * ECDH-ECDSA-AES128-GCM-SHA256:DHE-RSA-AES128-SHA256 (OpenSSL)
   * TLS-ECDH-ECDSA-WITH-AES-128-GCM-SHA256:TLS-DHE-RSA-WITH-AES-128-GCM-SHA256
   *   (mbedTLS)
   *
   * For OpenSSL the list can be obtained by running "openssl ciphers".
   * For mbedTLS, names can be found in library/ssl_ciphersuites.c
   * If NULL, a reasonable default is used.
   */
  const char *ssl_cipher_suites;
#endif
};

struct mg_connection *mg_bind_opt(struct mg_mgr *mgr, const char *address,
                                  mg_event_handler_t handler,
                                  struct mg_bind_opts opts);

/* Optional parameters to `mg_connect_opt()` */
struct mg_connect_opts {
  void *user_data;           /* Initial value for connection's user_data */
  unsigned int flags;        /* Extra connection flags */
  const char **error_string; /* Placeholder for the error string */
  struct mg_iface *iface;    /* Interface instance */
#if MG_ENABLE_SSL
  /*
   * SSL settings.
   * Client certificate to present to the server.
   */
  const char *ssl_cert;
  /*
   * Private key corresponding to the certificate.
   * If ssl_cert is set but ssl_key is not, ssl_cert is used.
   */
  const char *ssl_key;
  /*
   * Verify server certificate using this CA bundle. If set to "*", then SSL
   * is enabled but no cert verification is performed.
   */
  const char *ssl_ca_cert;
  /* Colon-delimited list of acceptable cipher suites.
   * Names depend on the library used, for example:
   *
   * ECDH-ECDSA-AES128-GCM-SHA256:DHE-RSA-AES128-SHA256 (OpenSSL)
   * TLS-ECDH-ECDSA-WITH-AES-128-GCM-SHA256:TLS-DHE-RSA-WITH-AES-128-GCM-SHA256
   *   (mbedTLS)
   *
   * For OpenSSL the list can be obtained by running "openssl ciphers".
   * For mbedTLS, names can be found in library/ssl_ciphersuites.c
   * If NULL, a reasonable default is used.
   */
  const char *ssl_cipher_suites;
  /*
   * Server name verification. If ssl_ca_cert is set and the certificate has
   * passed verification, its subject will be verified against this string.
   * By default (if ssl_server_name is NULL) hostname part of the address will
   * be used. Wildcard matching is supported. A special value of "*" disables
   * name verification.
   */
  const char *ssl_server_name;
  /*
   * PSK identity and key. Identity is a NUL-terminated string and key is a hex
   * string. Key must be either 16 or 32 bytes (32 or 64 hex digits) for AES-128
   * or AES-256 respectively.
   * Note: Default list of cipher suites does not include PSK suites, if you
   * want to use PSK you will need to set ssl_cipher_suites as well.
   */
  const char *ssl_psk_identity;
  const char *ssl_psk_key;
#endif
};

/*
 * Initialise Mongoose manager. Side effect: ignores SIGPIPE signal.
 * `mgr->user_data` field will be initialised with a `user_data` parameter.
 * That is an arbitrary pointer, where the user code can associate some data
 * with the particular Mongoose manager. For example, a C++ wrapper class
 * could be written in which case `user_data` can hold a pointer to the
 * class instance.
 */
void mg_mgr_init(struct mg_mgr *mgr, void *user_data);

/*
 * De-initialises Mongoose manager.
 *
 * Closes and deallocates all active connections.
 */
void mg_mgr_free(struct mg_mgr *);

/*
 * This function performs the actual IO and must be called in a loop
 * (an event loop). It returns the current timestamp.
 * `milli` is the maximum number of milliseconds to sleep.
 * `mg_mgr_poll()` checks all connections for IO readiness. If at least one
 * of the connections is IO-ready, `mg_mgr_poll()` triggers the respective
 * event handlers and returns.
 */
time_t mg_mgr_poll(struct mg_mgr *, int milli);

void mg_set_protocol_http_websocket(struct mg_connection *nc);
