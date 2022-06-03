#ifndef PERFTEST_LOGGING_H
#define PERFTEST_LOGGING_H

void log_msg(FILE *fp, int bt, const char *fn, int line, const char *func, const char *fmt, ...);

#define log_err(...) \
  log_msg(stderr, 0, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

#define log_ebt(...) \
  log_msg(stderr, 1, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

#endif
