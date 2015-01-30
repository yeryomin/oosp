AC_PREREQ([2.68])
AC_INIT([oosp], [0.0.1], [roman@advem.lv])
#AC_CONFIG_SRCDIR([oosp.c])
#AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB([curl], [curl_easy_setopt],,
	AC_MSG_ERROR([cannot link to libcurl]))
AC_CHECK_LIB([xml2], [xmlParseDoc],,
	AC_MSG_ERROR([cannot link to libxml2]))

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h string.h ])
AC_CHECK_HEADERS([curl/curl.h],,
	AC_MSG_ERROR([cannot find headers for libcurl]))
AC_CHECK_HEADERS([libxml/parser.h],,
	AC_MSG_ERROR([cannot find headers for libxml2]))

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memset strdup strrchr])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

