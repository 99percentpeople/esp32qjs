
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) do {(void)(p);assert(!counter_lock_depth);++counter_lock_depth;} while(0)
#define portEXIT_CRITICAL(p) do {(void)(p);assert(counter_lock_depth==1);--counter_lock_depth;} while(0)
