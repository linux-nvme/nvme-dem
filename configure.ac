AC_INIT(dem,0.1)
AM_INIT_AUTOMAKE
AC_OUTPUT(Makefile)

AC_CHECK_LIB([curl], [curl_easy_init],
	[], [AC_MSG_ERROR(Install libcurl-devel)])
AC_CHECK_LIB([rdmacm], [rdma_create_id],
	[], [AC_MSG_ERROR(Install librdmacm-devel)])
AC_CHECK_LIB([ibverbs], [ibv_create_cq],
	[], [AC_MSG_ERROR(Install libibverbs-devel)])
AC_CHECK_LIB([pthread], [pthread_create],
	[], [AC_MSG_ERROR(Install libpthread-devel)])

AC_ARG_ENABLE(debug,
[  --enable-debug    Turn on debugging],
[case "${enableval}" in
  yes) debug=true ;;
  no)  debug=false ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-debug) ;;
esac],[debug=false])
AM_CONDITIONAL(DEBUG, test x$debug = xtrue)

AC_ARG_ENABLE(extra-security-cflags,
[  --enable-extra-security-cflags    Turn on extra_security_cflags],
[case "${enableval}" in
  yes) extra_security_cflags=true ;;
  no)  extra_security_cflags=false ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-extra-security-cflags) ;;
esac],[extra_security_cflags=false])
AM_CONDITIONAL(EXTRA_SECURITY_CFLAGS, test x$extra_security_cflags = xtrue)

AC_OUTPUT
