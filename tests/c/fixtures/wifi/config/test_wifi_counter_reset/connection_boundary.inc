
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static unsigned locked;
#define portENTER_CRITICAL(p) do {(void)(p);assert(!locked);++locked;} while(0)
#define portEXIT_CRITICAL(p) do {(void)(p);assert(locked==1);--locked;} while(0)
