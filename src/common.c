#include <stdlib.h>
#define NAT_BASE_DIR "NAT_BASE_DIR"
char* getBaseDir() { return getenv(NAT_BASE_DIR); }