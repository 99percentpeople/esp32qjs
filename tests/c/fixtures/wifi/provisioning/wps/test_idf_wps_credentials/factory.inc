
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static unsigned unsafe_formats;
static int os_snprintf(char *out,size_t size,const char *format,...) {
    /* Observe the real formatter boundary without executing the original UB. */
    if(strcmp(format,"%s")){unsafe_formats++;return 0;}
    va_list args;va_start(args,format);
    int result=vsnprintf(out,size,format,args);va_end(args);return result;
}
