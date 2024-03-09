#ifndef core_h
#define core_h

#include "compiler.h"
#include "debug.h"
#include "io.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#define BINARY_NATIVE(name, valueType, op)                  \
  static bool __##name(int argCount, Value* args) {         \
    do {                                                    \
      if (!IS_NUMBER(vmPeek(0)) || !IS_NUMBER(vmPeek(1))) { \
        runtimeError("Operands must be numbers.");          \
        return false;                                       \
      }                                                     \
      double b = AS_NUMBER(vmPop());                        \
      double a = AS_NUMBER(vmPop());                        \
      vmPop();                                              \
      vmPush(valueType(a op b));                            \
    } while (false);                                        \
    return true;                                            \
  }

InterpretResult initializeCore();

#endif
