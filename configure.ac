AC_INIT(dem,1.0)
AM_INIT_AUTOMAKE
AC_OUTPUT(Makefile)

AC_CHECK_LIB([curl], [curl_easy_init], [],
  [AC_MSG_ERROR(Install libcurl-devel)])
AC_CHECK_LIB([rdmacm], [rdma_create_id], [],
  [AC_MSG_ERROR(Install librdmacm-devel)])
AC_CHECK_LIB([ibverbs], [ibv_create_cq], [],
  [AC_MSG_ERROR(Install libibverbs-devel)])
AC_CHECK_LIB([pthread], [pthread_create], [],
  [AC_MSG_ERROR(Install libpthread-devel)])

AC_ARG_ENABLE(debug,
  AS_HELP_STRING([--enable-debug],[Turn on debugging]),
  [case "${enableval}" in
     yes) debug=true ;;
     no)  debug=false ;;
     *) AC_MSG_ERROR(bad value ${enableval} for --enable-debug) ;;
   esac],[debug=false])
AM_CONDITIONAL(DEBUG, test x$debug = xtrue)

AC_ARG_ENABLE(nvmeof-tracing,
  AS_HELP_STRING([--enable-nvmeof-tracing],[Turn on tracing nvme-of commands]),
  [case "${enableval}" in
     yes) nvmeof_tracing=true ;;
     no)  nvmeof_tracing=false ;;
     *) AC_MSG_ERROR(bad value ${enableval} for --enable-nvmeof-tracing) ;;
   esac],[nvmeof_tracing=false])
AM_CONDITIONAL(NVMEOF_TRACING, test x$nvmeof_tracing = xtrue)

AC_ARG_ENABLE(extra-security-cflags,
  AS_HELP_STRING([--enable-extra-security-cflags],
		 [Turn on extra_security_cflags]),
  [case "${enableval}" in
    yes) extra_security_cflags=true ;;
    no)  extra_security_cflags=false ;;
    *) AC_MSG_ERROR(bad value ${enableval} for --enable-extra-security-cflags)
       ;;
   esac],[extra_security_cflags=false])
AM_CONDITIONAL(EXTRA_SECURITY_CFLAGS, test x$extra_security_cflags = xtrue)

AC_ARG_WITH(spdk, AS_HELP_STRING([--with-spdk],
		 [enable endpoint manager to support spdk target]))

AC_ARG_WITH(configfs, AS_HELP_STRING([--with-configfs],
		 [enable endpoint manager to support in-kernel target]),
	    [], [with_configfs=yes])

AM_CONDITIONAL(SPDK, test x$with_spdk = xyes)
AM_CONDITIONAL(CONFIGFS, test x$with_configfs = xyes)

AS_IF(test x$with_spdk = xyes, [
  AC_CHECK_FILE([/usr/lib/libspdk.so], [],
    [AC_MSG_ERROR(Install spdk)])
  AC_CHECK_FILE([/usr/lib/libspdk_rpc.so], [],
    [AC_MSG_ERROR(Install spdk --with-rdma)])
  AC_CHECK_FILE([/usr/lib/libdpdk.a], [],
    [AC_MSG_ERROR(Install dpdk)])
  AC_CHECK_FILE([/usr/lib64/libpciaccess.so], [],
    AC_CHECK_FILE([/usr/lib64/libpciaccess.so], [],
      [AC_MSG_ERROR(Install libpciaccess)]))
])

AS_IF(test x$with_configfs = xyes, [], AS_IF(test x$with_spdk = xyes, [],
  [AC_MSG_ERROR(Must provide --with-configfs and/or --with-spdk)]))

AC_OUTPUT
