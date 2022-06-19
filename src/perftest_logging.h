#ifndef PERFTEST_LOGGING_H
#define PERFTEST_LOGGING_H

/*
 * --- Debugging -------------------------------------------------------------
 */
#define ENABLE_DEBUG

void dbg_msg(const char *fmt, ...);

void log_msg(FILE *fp, int bt, const char *fn, int line, const char *func, const char *fmt, ...);

#define log_err(...) \
  log_msg(stderr, 0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

#define log_ebt(...) \
  log_msg(stderr, 1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

#ifndef ENABLE_DEBUG
#define dbg_msg(...)
#endif

/*
 * --- Tracing ----------------------------------------------------------------
 */
#define ENABLE_TRACE
//#undef  ENABLE_TRACE

#ifdef ENABLE_TRACE

void function_trace(char *flag, int eno, const char *func_name);

#define FUNCTION_TRACE(flag, n) function_trace(flag, (n), __FUNCTION__)
#define FUNCTION_ENTER FUNCTION_TRACE("ENTER", 0x7FFFFFFF)
#define FUNCTION_EXIT(eno)  FUNCTION_TRACE("EXIT", (eno))
#define FUNCTION_LOG(msg, n)  FUNCTION_TRACE(msg, (n))

#else

#define FUNCTION_ENTER
#define FUNCTION_EXIT(eno)
#define FUNCTION_LOG(msg, n)

#endif

#endif
