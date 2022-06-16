#include <execinfo.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

#define BT_BUF_SIZE 100

void print_backtrace(FILE *fp)
{
    int nptrs;
    void *bt_buf[BT_BUF_SIZE];
    char **strings;
    int j;

    nptrs = backtrace(bt_buf, BT_BUF_SIZE);
    printf("backtrace() returned %d addresses\n", nptrs);
    strings = backtrace_symbols(bt_buf, nptrs);
    if (strings == NULL) {
        fprintf(fp, "XXX: Error with backtrace_symbols");
        return;
    }
    for (j = 0; j < nptrs; j++)
        fprintf(fp, "%s\n", strings[j]);
    fprintf(fp, "-------------- backtrace bottom -------------------\n\n");

    free(strings);
}

void log_msg(FILE *fp, int bt, const char *fn, int line, const char *func, const char *fmt, ...)
{
    char buffer[1024];
    int rc;
    va_list args;

    va_start(args, fmt);
    rc = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (rc <= 0) {
        fprintf(fp, "XXX: Error print log message.");
        return;
    }
    fprintf(fp, "ERROR: %s@%d:%s %s", fn, line, func, buffer);

    if (bt) {
        print_backtrace(fp);
    }
}

void dbg_msg(const char *fmt, ...)
{
    char buffer[1024];
    int rc;
    va_list args;

    va_start(args, fmt);
    rc = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (rc <= 0) {
        fprintf(stdout, "XXX: Error print log message.");
        return;
    }
    fprintf(stdout, "===DEBUG=== %s", buffer);
}
