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
