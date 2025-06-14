#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "io.h"
#include "vm.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

#define PREC_STEP 1

typedef struct {
  Scanner scanner;

  Token next;
  Token current;
  Token previous;
  Token penult;

  bool hadError;
  bool panicMode;
} Parser;

typedef void (*ParseFn)(Compiler* cmp, bool canAssign);
typedef bool (*DelimitFn)();

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence leftPrec;
  Precedence rightPrec;
} ParseRule;

Parser parser;
ClassCompiler* currentClass = NULL;

static Parser saveParser() {
  Parser checkpoint = parser;

  checkpoint.scanner = saveScanner();

  return checkpoint;
}

static void gotoParser(Parser checkpoint) {
  gotoScanner(checkpoint.scanner);
  parser = checkpoint;
}

static void errorAt(Compiler* cmp, Token* token, const char* message) {
  if (parser.panicMode)
    return;
  else
    parser.panicMode = true;

  fprintf(stderr, "Error at %s/%s:%d", cmp->module->dirName->chars,
          cmp->module->baseName->chars, token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
    // Nothing.
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

static void error(Compiler* cmp, const char* message) {
  errorAt(cmp, &parser.previous, message);
}

static void errorAtCurrent(Compiler* cmp, const char* message) {
  errorAt(cmp, &parser.current, message);
}

static void initIterator(Iterator* iter) {
  iter->var = 0;
  iter->obj = 0;
  iter->loopStart = 0;
}

static void initParser(Scanner scanner) {
  parser.scanner = scanner;

  parser.hadError = false;
  parser.panicMode = false;
  parser.current = scanToken();
  parser.next = scanToken();
}

static void shiftParser() {
  parser.penult = parser.previous;
  parser.previous = parser.current;
  parser.current = parser.next;
}

static void checkError(Compiler* cmp) {
  if (parser.current.type == TOKEN_ERROR)
    errorAtCurrent(cmp, parser.current.start);
}

static void advance(Compiler* cmp) {
  shiftParser();
  parser.next = scanToken();
  checkError(cmp);
}

void rescanPathIdentifier(Compiler* cmp) {
  parser.next = scanPathIdentifier();
  advance(cmp);
}

static bool prev(TokenType type) { return parser.previous.type == type; }
static bool check(TokenType type) { return parser.current.type == type; }
static bool peek(TokenType type) { return parser.next.type == type; }

static bool checkVariable() {
  return check(TOKEN_IDENTIFIER) || check(TOKEN_TYPE_VARIABLE);
}

// Keyword symbols that are also valid identifiers
// aren't tokenized, so we need to check for the
// bare string.

static bool checkStr(char* str) {
  return strncmp(parser.current.start, str, strlen(str)) == 0;
}

static void consume(Compiler* cmp, TokenType type, const char* message) {
  if (parser.current.type == type)
    advance(cmp);
  else
    errorAtCurrent(cmp, message);
}

static void consumeIdentifier(Compiler* cmp, const char* message) {
  if (parser.current.type == TOKEN_IDENTIFIER ||
      parser.current.type == TOKEN_TYPE_VARIABLE)
    advance(cmp);
  else
    errorAtCurrent(cmp, message);
}

static bool match(Compiler* cmp, TokenType type) {
  if (!check(type)) return false;
  advance(cmp);
  return true;
}

// Is there whitespace preceding the previous token?
static bool penultWhite() { return isWhite(*(parser.previous.start - 1)); }

// Is there whitespace preceding the current token?
static bool prevWhite() { return isWhite(*(parser.current.start - 1)); }

static void emitByte(Compiler* cmp, uint8_t byte) {
  writeChunk(&cmp->function->chunk, byte, parser.previous.line);
}

// Find a [token] that isn't nested within braces, brackets,
// or parentheses, for some initial [depth],
bool advanceTo(Compiler* cmp, TokenType token, TokenType closing,
               int initialDepth) {
  int depth = initialDepth;

  for (;;) {
    if (check(TOKEN_LEFT_BRACE) || check(TOKEN_LEFT_BRACKET) ||
        check(TOKEN_LEFT_PAREN))
      depth++;
    if (check(TOKEN_RIGHT_BRACE) || check(TOKEN_RIGHT_BRACKET) ||
        check(TOKEN_RIGHT_PAREN))
      depth--;
    // found one.
    if (check(token) && depth == initialDepth) return true;
    // found none.
    if (check(closing) && depth == 0) return false;
    advance(cmp);
  }
}

static void emitBytes(Compiler* cmp, uint8_t byte1, uint8_t byte2) {
  emitByte(cmp, byte1);
  emitByte(cmp, byte2);
}

static void emitConstant(Compiler* cmp, uint16_t constant) {
  emitBytes(cmp, constant >> 8, constant & 0xff);
}

static void emitConstInstr(Compiler* cmp, uint8_t instruction,
                           uint16_t constant) {
  emitByte(cmp, instruction);
  emitConstant(cmp, constant);
}

static void emitLoop(Compiler* cmp, int loopStart) {
  emitByte(cmp, OP_LOOP);

  int offset = cmp->function->chunk.count - loopStart + 2;
  if (offset > UINT16_MAX) error(cmp, "Loop body too large.");

  emitByte(cmp, (offset >> 8) & 0xff);
  emitByte(cmp, offset & 0xff);
}

static int emitJump(Compiler* cmp, uint8_t instruction) {
  emitByte(cmp, instruction);
  emitByte(cmp, 0xff);
  emitByte(cmp, 0xff);
  return cmp->function->chunk.count - 2;
}

static int getConstant(Compiler* cmp, Value value) {
  Value existing;

  if (vHashable(value) && mapGet(&cmp->function->constants, value, &existing) &&
      IS_NUMBER(existing)) {
    return (uint16_t)AS_NUMBER(existing);
  }

  return -1;
}

static uint16_t makeConstant(Compiler* cmp, Value value) {
  int existing = getConstant(cmp, value);
  if (existing != -1) return existing;

  int constant = addConstant(&cmp->function->chunk, value);

  if (constant > UINT16_MAX) {
    error(cmp, "Too many constants in one chunk.");
    return 0;
  }

  if (vHashable(value))
    mapSet(&cmp->function->constants, value, NUMBER_VAL(constant));

  return (uint16_t)constant;
}

static void loadConstant(Compiler* cmp, Value value) {
  uint16_t constant = makeConstant(cmp, value);
  emitConstInstr(cmp, OP_CONSTANT, constant);
}

static void patchJump(Compiler* cmp, int offset) {
  Chunk* chunk = &cmp->function->chunk;

  // -2 to adjust for the bytecode for the jump offset itself.
  int jump = chunk->count - offset - 2;

  if (jump > UINT16_MAX) {
    error(cmp, "Too much code to jump over.");
  }

  chunk->code[offset] = (jump >> 8) & 0xff;
  chunk->code[offset + 1] = jump & 0xff;
}

void initCompiler(Compiler* cmp, Compiler* enclosing, Compiler* signature,
                  FunctionType functionType, ObjModule* module, Token name) {
  cmp->enclosing = NULL;
  cmp->enclosing = enclosing;
  cmp->signature = NULL;
  cmp->signature = signature;
  cmp->module = NULL;
  cmp->module = module;
  cmp->function = NULL;
  cmp->function = newFunction(cmp->module);
  cmp->functionType = functionType;
  cmp->function->localCount = 0;
  cmp->scopeDepth = 0;

  vm.compiler = cmp;
  cmp->function->name = copyString(name.start, name.length);

  for (int i = 0; i < UINT8_COUNT; i++) {
    cmp->function->locals[i].depth = 0;
    cmp->function->locals[i].isCaptured = false;

    initToken(&cmp->function->locals[i].name);
  }

  Local* local = &cmp->function->locals[cmp->function->localCount++];
  local->depth = 0;
  local->isCaptured = false;
  local->name.type = TOKEN_IDENTIFIER;
  if (functionType == TYPE_METHOD || functionType == TYPE_INITIALIZER) {
    local->name.start = "this";
    local->name.length = 4;
  } else {
    local->name.start = "";
    local->name.length = 0;
  }
}

static void emitDefaultReturn(Compiler* cmp) {
  if (cmp->functionType == TYPE_INITIALIZER)
    emitConstInstr(cmp, OP_GET_LOCAL, 0);
  else
    emitByte(cmp, OP_NIL);
  emitByte(cmp, OP_IMPLICIT_RETURN);
}

#if defined(DEBUG_PRINT_CODE)
#define DEBUG_CHUNK()                                                        \
  if (!parser.hadError) {                                                    \
    disassembleChunk(&cmp->function->chunk, cmp->function->name != NULL      \
                                                ? cmp->function->name->chars \
                                                : "<script>");               \
  }
#else
#define DEBUG_CHUNK()
#endif

static ObjFunction* endCompiler(Compiler* cmp) {
  emitDefaultReturn(cmp);

  DEBUG_CHUNK()

  vm.compiler = cmp->enclosing;
  return cmp->function;
}

static void beginScope(Compiler* cmp) { cmp->scopeDepth++; }

static void endScope(Compiler* cmp) {
  cmp->scopeDepth--;

  while (cmp->function->localCount > 0 &&
         cmp->function->locals[cmp->function->localCount - 1].depth >
             cmp->scopeDepth) {
    if (cmp->function->locals[cmp->function->localCount - 1].isCaptured) {
      emitByte(cmp, OP_CLOSE_UPVALUE);
    } else {
      emitByte(cmp, OP_POP);
    }

    cmp->function->localCount--;
  }
}

static void closeUpvalues(ObjFunction* function, Compiler* cmp,
                          Compiler* enclosing) {
  for (int i = 0; i < function->upvalueCount; i++) {
    emitByte(enclosing, cmp->upvalues[i].isLocal ? 1 : 0);
    emitByte(enclosing, cmp->upvalues[i].index);
  }
}

static void closeFunction(Compiler* cmp, Compiler* enclosing, OpCode op) {
  ObjFunction* function = endCompiler(cmp);

  emitConstInstr(enclosing, op, makeConstant(enclosing, OBJ_VAL(function)));

  closeUpvalues(function, cmp, enclosing);
}

static void signFunction(Compiler* cmp, Compiler* sigCmp, Compiler* enclosing) {
  emitDefaultReturn(cmp);
  ObjFunction* function = cmp->function;
  emitConstInstr(enclosing, OP_CLOSURE,
                 makeConstant(enclosing, OBJ_VAL(function)));

  closeUpvalues(function, cmp, enclosing);
  closeFunction(sigCmp, enclosing, OP_SIGN);

  DEBUG_CHUNK()
}

static int infixPrecedence(Compiler* cmp);
static void function(Compiler* enclosing, FunctionType type, Token name);
static void nakedFunction(Compiler* enclosing, FunctionType type, Token name);
static void expression(Compiler* cmp);
static void boundExpression(Compiler* cmp, Token name);
static void typeExpression(Compiler* cmp);
static void statement(Compiler* cmp);
static void declaration(Compiler* cmp);
static void declarations(Compiler* cmp);
static void classDeclaration(Compiler* cmp);
static ParseRule* getPrefixRule(Compiler* cmp, Token token);
static ParseRule* getInfixRule(Compiler* cmp, Token token);
static void parseDelimitedPrecedence(Compiler* cmp, Precedence precedence,
                                     DelimitFn fn);
static void parsePrecedence(Compiler* cmp, Precedence precedence);

static Value identifierToken(Token token) {
  return OBJ_VAL(copyString(token.start, token.length));
}

static uint16_t identifierConstant(Compiler* cmp, Token* token) {
  return makeConstant(cmp, identifierToken(*token));
}

static bool identifiersEqual(Token* a, Token* b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* cmp, Token* name) {
  for (int i = cmp->function->localCount - 1; i >= 0; i--) {
    Local* local = &cmp->function->locals[i];

    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error(cmp, "Can't read local variable in its own initializer.");
      }
      return i;
    }
  }

  return -1;
}

static int addUpvalue(Compiler* cmp, uint8_t index, bool isLocal) {
  int upvalueCount = cmp->function->upvalueCount;

  for (int i = 0; i < upvalueCount; i++) {
    Upvalue* upvalue = &cmp->upvalues[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;
    }
  }

  if (upvalueCount == UINT8_COUNT) {
    error(cmp, "Too many closure variables in function.");
    return 0;
  }

  cmp->upvalues[upvalueCount].isLocal = isLocal;
  cmp->upvalues[upvalueCount].index = index;
  return cmp->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* cmp, Token* name) {
  if (cmp->enclosing == NULL) return -1;

  int local = resolveLocal(cmp->enclosing, name);
  if (local != -1) {
    cmp->enclosing->function->locals[local].isCaptured = true;
    return addUpvalue(cmp, (uint8_t)local, true);
  }

  int upvalue = resolveUpvalue(cmp->enclosing, name);
  if (upvalue != -1) {
    return addUpvalue(cmp, (uint8_t)upvalue, false);
  }

  return -1;
}

static uint8_t addLocal(Compiler* cmp, Token name) {
  if (cmp->function->localCount == UINT8_COUNT) {
    error(cmp, "Too many local variables in function.");
    return 0;
  }

  Local* local = &cmp->function->locals[cmp->function->localCount++];

  local->name = name;
  local->depth = -1;
  local->isCaptured = false;

  return cmp->function->localCount - 1;
}

static void markInitialized(Compiler* cmp) {
  if (cmp->scopeDepth == 0) return;

  cmp->function->locals[cmp->function->localCount - 1].depth = cmp->scopeDepth;
}

static uint8_t declareLocal(Compiler* cmp, Token* name) {
  for (int i = cmp->function->localCount - 1; i >= 0; i--) {
    Local* local = &cmp->function->locals[i];
    if (local->depth != -1 && local->depth < cmp->scopeDepth) {
      break;
    }

    if (identifiersEqual(name, &local->name)) {
      error(cmp, "Already a variable with this name in this scope.");
    }
  }

  return addLocal(cmp, *name);
}

static uint8_t declareVariable(Compiler* cmp) {
  if (cmp->scopeDepth == 0) return 0;

  return declareLocal(cmp, &parser.previous);
}

static void defineType(Compiler* cmp, int var) {
  emitConstInstr(
      cmp, cmp->scopeDepth > 0 ? OP_SET_TYPE_LOCAL : OP_SET_TYPE_GLOBAL, var);
}

static uint16_t getGlobalConstant(Compiler* cmp, char* name) {
  uint16_t var = makeConstant(cmp, INTERN(name));
  emitConstInstr(cmp, OP_GET_GLOBAL, var);
  return var;
}

void getProperty(Compiler* cmp, char* name) {
  uint16_t var = makeConstant(cmp, INTERN(name));
  emitConstInstr(cmp, OP_GET_PROPERTY, var);
}

static uint8_t argumentList(Compiler* cmp) {
  uint8_t argCount = 0;
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      if (match(cmp, TOKEN_DOUBLE_DOT)) {
        expression(cmp);
        emitByte(cmp, OP_SPREAD);
      } else {
        expression(cmp);
      }

      if (argCount == 255) error(cmp, "Can't have more than 255 arguments.");

      argCount++;
    } while (match(cmp, TOKEN_COMMA));
  }
  consume(cmp, TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
  return argCount;
}

static void binary(Compiler* cmp, bool canAssign) {
  TokenType operatorType = parser.previous.type;
  ParseRule* rule = getInfixRule(cmp, parser.previous);
  parsePrecedence(cmp, (Precedence)(rule->rightPrec));

  switch (operatorType) {
    case TOKEN_BANG_EQUAL:
      emitBytes(cmp, OP_EQUAL, OP_NOT);
      break;
    case TOKEN_EQUAL_EQUAL:
      emitByte(cmp, OP_EQUAL);
      break;
    case TOKEN_IN:
      emitByte(cmp, OP_MEMBER);
      break;
    default:
      return;  // Unreachable.
  }
}

static void call(Compiler* cmp, bool canAssign) {
  uint8_t argCount = argumentList(cmp);
  emitBytes(cmp, OP_CALL, argCount);
}

static void property(Compiler* cmp, bool canAssign) {
  consumeIdentifier(cmp, "Expect property name after '.'.");
  uint16_t name = identifierConstant(cmp, &parser.previous);

  if (canAssign && match(cmp, TOKEN_EQUAL)) {
    expression(cmp);
    emitConstInstr(cmp, OP_SET_PROPERTY, name);
  } else {
    emitConstInstr(cmp, OP_GET_PROPERTY, name);
  }
}

// A dot flush against an expression is property access;
// a dot with whitespace preceding and following it is
// function composition. We dont require the whitespace
// following composition to disambiguate the two, but we
// may as well enforce the symmetry.
static void dot(Compiler* cmp, bool canAssign) {
  if (penultWhite() && prevWhite()) {
    uint16_t name = getGlobalConstant(cmp, "compose");
    expression(cmp);
    emitConstInstr(cmp, OP_CALL_INFIX, name);
  } else {
    property(cmp, canAssign);
  }
}

static void literal(Compiler* cmp, bool canAssign) {
  switch (parser.previous.type) {
    case TOKEN_FALSE:
      emitByte(cmp, OP_FALSE);
      break;
    case TOKEN_NIL:
      emitByte(cmp, OP_NIL);
      break;
    case TOKEN_TRUE:
      emitByte(cmp, OP_TRUE);
      break;
    default:
      return;  // Unreachable.
  }
}

static void parentheses(Compiler* cmp, bool canAssign) {
  if (match(cmp, TOKEN_RIGHT_PAREN)) {
    emitByte(cmp, OP_UNIT);
    return;
  }

  expression(cmp);

  if (check(TOKEN_COMMA)) {
    int argCount = 1;
    do {
      advance(cmp);
      expression(cmp);
      argCount++;
    } while (check(TOKEN_COMMA));

    getGlobalConstant(cmp, S_TUPLE);
    emitBytes(cmp, OP_CALL_POSTFIX, argCount);
  }
  consume(cmp, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void and_(Compiler* cmp, bool canAssign) {
  int endJump = emitJump(cmp, OP_JUMP_IF_FALSE);

  emitByte(cmp, OP_POP);
  parsePrecedence(cmp, PREC_AND);

  patchJump(cmp, endJump);
}

static void or_(Compiler* cmp, bool canAssign) {
  int elseJump = emitJump(cmp, OP_JUMP_IF_FALSE);
  int endJump = emitJump(cmp, OP_JUMP);

  patchJump(cmp, elseJump);
  emitByte(cmp, OP_POP);

  parsePrecedence(cmp, PREC_OR);
  patchJump(cmp, endJump);
}

static void number(Compiler* cmp, bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  loadConstant(cmp, NUMBER_VAL(value));
}

static void string(Compiler* cmp, bool canAssign) {
  ObjString* str =
      copyString(parser.previous.start + 1, parser.previous.length - 2);
  loadConstant(cmp, OBJ_VAL(str));
}

static void texString(Compiler* cmp, bool canAssign) {
  ObjString* str =
      copyString(parser.previous.start + 4, parser.previous.length - 5);
  loadConstant(cmp, OBJ_VAL(str));
}

static void stringInterpolation(Compiler* cmp, bool canAssign);

static void interpolation(Compiler* cmp, bool canAssign, int startOffset,
                          int lengthOffset) {
  getGlobalConstant(cmp, "+");
  getGlobalConstant(cmp, "+");
  loadConstant(cmp, OBJ_VAL(copyString(parser.previous.start + startOffset,
                                       parser.previous.length - lengthOffset)));

  getGlobalConstant(cmp, "str");
  expression(cmp);

  emitBytes(cmp, OP_CALL, 1);
  emitBytes(cmp, OP_CALL, 2);

  // pretend the next token is an opening string literal.
  rewindScanner(parser.current);
  parser.next = scanVirtualToken('"');

  if (!match(cmp, TOKEN_RIGHT_BRACE)) return error(cmp, "Expecting '}'.");

  if (match(cmp, TOKEN_STRING))
    string(cmp, canAssign);
  else if (match(cmp, TOKEN_INTERPOLATION))
    stringInterpolation(cmp, canAssign);
  else
    return;

  emitBytes(cmp, OP_CALL, 2);
}

static void stringInterpolation(Compiler* cmp, bool canAssign) {
  interpolation(cmp, canAssign, 1, 3);
}

static void texInterpolation(Compiler* cmp, bool canAssign) {
  interpolation(cmp, canAssign, 4, 6);
}

static void undefined(Compiler* cmp, bool canAssign) {
  emitByte(cmp, OP_UNDEFINED);
}

static int namedVariable(Compiler* cmp, Token name, bool canAssign) {
  uint8_t getOp, setOp;

  int arg = resolveLocal(cmp, &name);

  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else if ((arg = resolveUpvalue(cmp, &name)) != -1) {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  } else {
    arg = identifierConstant(cmp, &name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  if (canAssign && match(cmp, TOKEN_EQUAL)) {
    boundExpression(cmp, name);
    emitConstInstr(cmp, setOp, arg);
  } else if (canAssign && match(cmp, TOKEN_ARROW_LEFT)) {
    expression(cmp);
    emitByte(cmp, OP_DESTRUCTURE);
    emitConstInstr(cmp, setOp, arg);
  } else {
    emitConstInstr(cmp, getOp, arg);
  }

  return arg;
}

static int nativeCall(Compiler* cmp, char* name) {
  int address = getGlobalConstant(cmp, name);
  emitBytes(cmp, OP_CALL, 0);
  return address;
}

static int nativePostfix(Compiler* cmp, char* name, int argCount) {
  int address = getGlobalConstant(cmp, name);
  emitBytes(cmp, OP_CALL_POSTFIX, argCount);
  return address;
}

static void variable(Compiler* cmp, bool canAssign) {
  if (parser.previous.type == TOKEN_TYPE_VARIABLE &&
      cmp->functionType == TYPE_IMPLICIT) {
    int i = resolveLocal(cmp, &parser.previous);

    if (i == -1) {
      cmp->function->arity++;
      declareVariable(cmp);
      markInitialized(cmp);
    }
  }

  namedVariable(cmp, parser.previous, canAssign);
}

static void infix(Compiler* cmp, bool canAssign) {
  ParseRule* rule = getInfixRule(cmp, parser.previous);
  uint16_t name = identifierConstant(cmp, &parser.previous);
  variable(cmp, canAssign);

  if (penultWhite() && prevWhite()) {
    parsePrecedence(cmp, (Precedence)(rule->rightPrec));
    emitConstInstr(cmp, OP_CALL_INFIX, name);
  }
}

static void prefix(Compiler* cmp, bool canAssign) {
  variable(cmp, canAssign);
  if (prevWhite()) {
    parsePrecedence(cmp, PREC_UNARY);
    emitBytes(cmp, OP_CALL, 1);
  }
}

static void super_(Compiler* cmp, bool canAssign) {
  if (currentClass == NULL) {
    error(cmp, "Can't use 'super' outside of a class.");
  }

  consume(cmp, TOKEN_DOT, "Expect '.' after 'super'.");
  consumeIdentifier(cmp, "Expect superclass method name.");
  uint16_t name = identifierConstant(cmp, &parser.previous);

  // load the instance first, which is necessary for
  // binding any method detached from the superclass.
  namedVariable(cmp, syntheticToken("this"), false);
  namedVariable(cmp, syntheticToken("super"), false);
  emitConstInstr(cmp, OP_GET_SUPER, name);
}

static void this_(Compiler* cmp, bool canAssign) {
  if (currentClass == NULL) {
    error(cmp, "Can't use 'this' outside of a class.");
    return;
  }
  variable(cmp, false);
}

static void unary(Compiler* cmp, bool canAssign) {
  TokenType operatorType = parser.previous.type;

  // compile the operand.
  parsePrecedence(cmp, PREC_UNARY);

  // emit the operator instruction.
  switch (operatorType) {
    case TOKEN_BANG:
      emitByte(cmp, OP_NOT);
      break;
    default:
      return;  // unreachable.
  }
}

static void prefixNot(Compiler* cmp, bool canAssign) {
  ParseRule* penultRule = getInfixRule(cmp, parser.penult);
  if (penultRule->infix != NULL && penultRule->infix != call) {
    getGlobalConstant(cmp, "negate");
    emitBytes(cmp, OP_CALL_POSTFIX, 1);
    parsePrecedence(cmp, penultRule->rightPrec);
  } else {
    error(cmp, "'not' must compose with an infix.");
  }
}

static void infixNot(Compiler* cmp, bool canAssign) {
  ParseRule* currentRule = getInfixRule(cmp, parser.current);

  // negation takes scope over infixes.
  if (currentRule->infix != NULL) {
    advance(cmp);
    binary(cmp, canAssign);
    emitByte(cmp, OP_NOT);
  } else {
    error(cmp, "'not' must compose with an infix.");
  }
}

static uint16_t parseVariable(Compiler* cmp, const char* errorMessage) {
  consumeIdentifier(cmp, errorMessage);
  uint8_t local = declareVariable(cmp);

  if (cmp->scopeDepth > 0) return local;

  return identifierConstant(cmp, &parser.previous);
}

static void defineVariable(Compiler* cmp, uint16_t var) {
  if (cmp->scopeDepth > 0) {
    markInitialized(cmp);
    return;
  }

  emitConstInstr(cmp, OP_DEFINE_GLOBAL, var);
}

static void setVariable(Compiler* cmp, uint16_t var) {
  if (cmp->scopeDepth > 0) {
    emitConstInstr(cmp, OP_SET_LOCAL, var);
  } else {
    emitConstInstr(cmp, OP_SET_GLOBAL, var);
  }
}

typedef enum { SIG_NAKED, SIG_PAREN, SIG_NOT } SignatureType;

bool matchParamOrPattern(Compiler* cmp) {
  return match(cmp, TOKEN_IDENTIFIER) || match(cmp, TOKEN_TYPE_VARIABLE) ||
         match(cmp, TOKEN_NUMBER) || match(cmp, TOKEN_TRUE) ||
         match(cmp, TOKEN_FALSE) || match(cmp, TOKEN_NIL) ||
         match(cmp, TOKEN_UNDEFINED) || match(cmp, TOKEN_STRING);
}

static SignatureType peekSignatureType(Compiler* cmp) {
  if (match(cmp, TOKEN_LEFT_PAREN)) {
    if (!check(TOKEN_RIGHT_PAREN)) {
      do {
        if (!matchParamOrPattern(cmp)) return SIG_NOT;
        if (check(TOKEN_COLON)) {
          advanceTo(cmp, TOKEN_COMMA, TOKEN_RIGHT_PAREN, 1);
        }

      } while (match(cmp, TOKEN_COMMA));
    }

    if (!match(cmp, TOKEN_RIGHT_PAREN)) return SIG_NOT;
    if (!match(cmp, TOKEN_FAT_ARROW)) return SIG_NOT;

    return SIG_PAREN;
  } else {
    // can only be naked.
    if (matchParamOrPattern(cmp)) return peekSignatureType(cmp);
    if (match(cmp, TOKEN_FAT_ARROW)) return SIG_NAKED;
  }

  return SIG_NOT;
}

static bool trySingleFunction(Compiler* cmp, FunctionType fnType, Token name) {
  Parser checkpoint = saveParser();
  SignatureType signatureType = peekSignatureType(cmp);
  gotoParser(checkpoint);

  switch (signatureType) {
    case SIG_NAKED:
      nakedFunction(cmp, fnType, name);
      return true;
    case SIG_PAREN:
      function(cmp, fnType, name);
      return true;
    case SIG_NOT:
      return false;
  }
  return false;
}

void overload(Compiler* cmp, FunctionType fnType, Token name) {
  int count = 1;

  do {
    trySingleFunction(cmp, fnType, name);
    count++;
  } while (match(cmp, TOKEN_PIPE));

  emitBytes(cmp, OP_OVERLOAD, count);
  emitConstant(cmp, identifierConstant(cmp, &name));
}

static bool tryFunction(Compiler* cmp, FunctionType fnType, Token name) {
  if (trySingleFunction(cmp, fnType, name)) {
    if (match(cmp, TOKEN_PIPE)) overload(cmp, fnType, name);
    return true;
  }

  return false;
}

static void boundExpression(Compiler* cmp, Token name) {
  if (tryFunction(cmp, TYPE_BOUND, name)) {
    return;
  }

  parsePrecedence(cmp, PREC_ASSIGNMENT);
}

static void whiteDelimitedExpression(Compiler* cmp) {
  if (tryFunction(cmp, TYPE_ANONYMOUS, syntheticToken("lambda"))) return;

  parseDelimitedPrecedence(cmp, PREC_ASSIGNMENT, prevWhite);
}

static void expression(Compiler* cmp) {
  if (tryFunction(cmp, TYPE_ANONYMOUS, syntheticToken("lambda"))) return;

  parsePrecedence(cmp, PREC_ASSIGNMENT);
}

static void block(Compiler* cmp) {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration(cmp);
  }

  consume(cmp, TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void blockOrExpression(Compiler* cmp) {
  if (check(TOKEN_LEFT_BRACE)) {
    advance(cmp);
    block(cmp);
  } else {
    expression(cmp);
    emitByte(cmp, OP_RETURN);
  }
}

static bool tryImplicitFunction(Compiler* enclosing) {
  Parser checkpoint = saveParser();

  Compiler cmp;

  initCompiler(&cmp, enclosing, NULL, TYPE_IMPLICIT, enclosing->module,
               syntheticToken("implicit"));
  beginScope(&cmp);

  parsePrecedence(&cmp, PREC_TYPE_ASSIGNMENT);

  if (cmp.function->arity > 0) {
    emitByte(&cmp, OP_RETURN);
    closeFunction(&cmp, enclosing, OP_CLOSURE);
    return true;
  }

  // discard the function if it has no parameters; it'll be gc'd.
  vm.compiler = enclosing;
  gotoParser(checkpoint);
  return false;
}

static void typeExpression(Compiler* cmp) {
  // the order here means that if there is explicit
  // quantification, all type variables must be explicit.
  if (tryFunction(cmp, TYPE_ANONYMOUS, syntheticToken("lambda")) ||
      tryImplicitFunction(cmp))
    return;

  parsePrecedence(cmp, PREC_TYPE_ASSIGNMENT);
}

static void variableParameter(Compiler* cmp, Compiler* sigCmp) {
  advance(cmp);
  declareVariable(cmp);
  markInitialized(cmp);

  // put the param in the signature's constant table.
  uint16_t constant = identifierConstant(sigCmp, &parser.previous);
  emitConstInstr(sigCmp, OP_VARIABLE, constant);
  // parse an optional type.
  if (match(sigCmp, TOKEN_COLON))
    expression(sigCmp);
  else
    emitByte(sigCmp, OP_UNDEFINED);
}

static void patternParameter(Compiler* cmp, Compiler* sigCmp) {
  cmp->function->patterned = true;
  // include the literal in the signature.
  parsePrecedence(sigCmp, PREC_ASSIGNMENT);
  // type defaults downstream to a tvar.
  emitByte(sigCmp, OP_UNDEFINED);
  // offset the local stack so that the literal
  // can be passed to the function as an argument
  // even if it's not bound.
  addLocal(cmp, syntheticToken("#pattern"));
}

static void parameter(Compiler* cmp, Compiler* sigCmp) {
  getGlobalConstant(sigCmp, "PatternElement");

  if (checkVariable()) {
    variableParameter(cmp, sigCmp);
  } else {
    patternParameter(cmp, sigCmp);
  }

  emitBytes(sigCmp, OP_CALL, 2);
}

static void openFunction(Compiler* enclosing, Compiler* cmp, Compiler* sigCmp,
                         FunctionType type, Token name) {
  Token sigToken = syntheticToken("signature");
  initCompiler(sigCmp, enclosing, NULL, TYPE_IMPLICIT, enclosing->module,
               sigToken);
  beginScope(sigCmp);

  initCompiler(cmp, enclosing, sigCmp, type, enclosing->module, name);
  beginScope(cmp);
}

static void openSignature(Compiler* sigCmp) {
  getGlobalConstant(sigCmp, "Signature");
  // domain pattern.
  getGlobalConstant(sigCmp, "Pattern");
}

static void closeSignature(Compiler* cmp, Compiler* sigCmp) {
  emitBytes(sigCmp, OP_CALL, cmp->function->arity);
  // range pattern.
  getGlobalConstant(sigCmp, "Pattern");
  emitBytes(sigCmp, OP_CALL, 0);
  // signature.
  emitBytes(sigCmp, OP_CALL, 2);
  emitByte(sigCmp, OP_RETURN);
}

static void variadicSignature(Compiler* cmp, Compiler* sigCmp) {
  openSignature(sigCmp);

  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      if (cmp->function->variadic)
        error(cmp, "Can only apply * to the final parameter.");

      cmp->function->arity++;
      if (cmp->function->arity > 255) {
        errorAtCurrent(cmp, "Can't have more than 255 parameters.");
      }

      if (checkStr("*")) {
        cmp->function->variadic = true;
        // shift the star off the parameter's token.
        parser.current.start++;
        parser.current.length--;
      }

      parameter(cmp, sigCmp);
    } while (match(cmp, TOKEN_COMMA));
  }
  closeSignature(cmp, sigCmp);
}

static void function(Compiler* enclosing, FunctionType type, Token name) {
  Compiler cmp, sigCmp;

  openFunction(enclosing, &cmp, &sigCmp, type, name);
  consume(&cmp, TOKEN_LEFT_PAREN, "Expect '(' after function name.");
  variadicSignature(&cmp, &sigCmp);
  consume(&cmp, TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
  consume(&cmp, TOKEN_FAT_ARROW, "Expect '=>' after signature.");
  blockOrExpression(&cmp);
  signFunction(&cmp, &sigCmp, enclosing);
}

static void nakedFunction(Compiler* enclosing, FunctionType type, Token name) {
  Compiler cmp, sigCmp;
  openFunction(enclosing, &cmp, &sigCmp, type, name);

  openSignature(&sigCmp);
  cmp.function->arity = 1;
  parameter(&cmp, &sigCmp);
  closeSignature(&cmp, &sigCmp);

  if (check(TOKEN_FAT_ARROW)) {
    advance(&cmp);
    blockOrExpression(&cmp);
  } else {
    nakedFunction(&cmp, TYPE_ANONYMOUS, syntheticToken("lambda"));
    emitByte(&cmp, OP_RETURN);
  }

  signFunction(&cmp, &sigCmp, enclosing);
}

static void method(Compiler* cmp) {
  int infixPrec = infixPrecedence(cmp);

  consumeIdentifier(cmp, "Expect method name.");
  uint16_t var = identifierConstant(cmp, &parser.previous);
  FunctionType type = TYPE_METHOD;

  if (parser.previous.length == 4 &&
      memcmp(parser.previous.start, S_INIT, 4) == 0) {
    type = TYPE_INITIALIZER;
  }

  function(cmp, type, parser.previous);
  emitConstInstr(cmp, OP_METHOD, var);

  if (!prev(TOKEN_RIGHT_BRACE)) {
    consume(cmp, TOKEN_SEMICOLON,
            "Expect ';' after method with expression body.");
  }

  if (infixPrec != 0) {
    Value name = cmp->function->chunk.constants.values[var];
    mapSet(&vm.methodInfixes, name, NUMBER_VAL(infixPrec));
  }
}

// Parse an iterator and store the details in an
// [Iterator] struct.
static Iterator iterator(Compiler* cmp) {
  Iterator iter;
  initIterator(&iter);

  // store the bound variable.
  iter.var = parseVariable(cmp, "Expect identifier.");
  emitByte(cmp, OP_NIL);
  defineVariable(cmp, iter.var);

  consume(cmp, TOKEN_IN, "Expect 'in' between identifier and iterable.");

  // the potentially iterable expression.
  expression(cmp);

  // turn the expression into an iterator instance and store it.
  getGlobalConstant(cmp, S_ITER);
  emitBytes(cmp, OP_CALL_POSTFIX, 1);
  iter.obj = addLocal(cmp, syntheticToken("#iter"));
  markInitialized(cmp);

  iter.loopStart = cmp->function->chunk.count;

  return iter;
}

static int iterationNext(Compiler* cmp, Iterator iter) {
  int exitJump = emitJump(cmp, OP_ITER);
  emitConstant(cmp, iter.var);

  return exitJump;
}

static void iterationEnd(Compiler* cmp, Iterator iter, int exitJump) {
  emitLoop(cmp, iter.loopStart);
  patchJump(cmp, exitJump);
}

static void forInStatement(Compiler* cmp) {
  Iterator iter = iterator(cmp);
  consume(cmp, TOKEN_RIGHT_PAREN, "Expect ')' after for clause.");
  int exitJump = iterationNext(cmp, iter);
  statement(cmp);
  iterationEnd(cmp, iter, exitJump);
}

static void forQuantification(Compiler* enclosing, bool canAssign) {
  getGlobalConstant(enclosing, "Quantification");
  consumeIdentifier(enclosing, "Expect quantifier.");
  uint16_t quantifier = identifierConstant(enclosing, &parser.previous);
  emitConstInstr(enclosing, OP_GET_PROPERTY, quantifier);

  Compiler cmp, sigCmp;
  Token name = syntheticToken("#quantifierBody");
  openFunction(enclosing, &cmp, &sigCmp, TYPE_ANONYMOUS, name);

  consume(enclosing, TOKEN_LEFT_PAREN, "Expect '(' after quantifier.");
  openSignature(&sigCmp);
  if (!check(TOKEN_IN)) {
    do {
      cmp.function->arity++;
      if (cmp.function->arity > 255)
        errorAtCurrent(&cmp, "Can't have more than 255 parameters.");

      getGlobalConstant(&sigCmp, "PatternElement");
      variableParameter(&cmp, &sigCmp);
      emitBytes(&sigCmp, OP_CALL, 2);
    } while (match(enclosing, TOKEN_COMMA));
  }

  consume(enclosing, TOKEN_IN, "Expect restriction.");
  expression(enclosing);
  consume(enclosing, TOKEN_RIGHT_PAREN, "Expect ')' after restriction.");
  closeSignature(&cmp, &sigCmp);

  blockOrExpression(&cmp);
  signFunction(&cmp, &sigCmp, enclosing);

  emitByte(enclosing, OP_QUANTIFY);
}

// Parse a comprehension, given a [Parser] that points at the body,
// the stack address [var] of the object under construction, and the
// type of [closingToken].
Parser comprehension(Compiler* cmp, Parser checkpointA,
                     TokenType closingToken) {
  Iterator iter;

  int iterJump = -1;
  int predJump = -1;

  Parser checkpointB = checkpointA;

  if ((checkVariable()) && peek(TOKEN_IN)) {
    // bound variable and iterable to draw from.
    beginScope(cmp);
    iter = iterator(cmp);
    iterJump = emitJump(cmp, OP_COMPREHENSION_ITER);
    emitConstant(cmp, iter.var);
  } else {
    // a predicate to test against.
    expression(cmp);
    predJump = emitJump(cmp, OP_COMPREHENSION_PRED);
    emitByte(cmp, OP_POP);
  }

  if (match(cmp, TOKEN_COMMA)) {
    // parse the conditions recursively, since this
    // makes scope management simple. in particular,
    // variables will be in scope in every condition
    // to their right, and all we have to do is conclude
    // each scope at the end of this function.
    checkpointB = comprehension(cmp, checkpointA, closingToken);
  } else if (check(closingToken)) {
    // now we parse the body, first saving the point
    // where the generator ends.
    checkpointB = saveParser();
    gotoParser(checkpointA);

    // parse the expression, load the comprehension, and append.
    expression(cmp);
    emitByte(cmp, OP_COMPREHENSION_BODY);
    emitByte(cmp, OP_POP);
  }

  if (iterJump != -1) {
    iterationEnd(cmp, iter, iterJump);
    endScope(cmp);
  } else if (predJump != -1) {
    // we need to jump over this last condition.
    // pop if the condition was truthy.
    int elseJump = emitJump(cmp, OP_JUMP);
    patchJump(cmp, predJump);
    emitByte(cmp, OP_POP);
    patchJump(cmp, elseJump);
  } else {
    error(cmp, "Comprehension requires at least one clause.");
  }

  return checkpointB;
}

// Here we check if we're at a comprehension, and if we are, we parse it.
// We wrap the comprehension in a closure so that it has its own frame
// of locals during construction. We then invoke the closure immediately,
// returning the actual comprehension object.
static bool tryComprehension(Compiler* enclosing, char* klass,
                             TokenType openingToken, TokenType closingToken) {
  // we need to save our location for two reasons:
  // (a) if it *is* a comprehension, we can't parse the body
  //  until we've parsed the conditions;
  // (b) if it *isn't*, we need to rewind after we've looked ahead
  //  and established that it isn't.
  Parser checkpointA = saveParser();

  if (advanceTo(enclosing, TOKEN_PIPE, closingToken, 1)) {
    advance(enclosing);  // eat the pipe.

    Compiler cmp;
    initCompiler(&cmp, enclosing, NULL, TYPE_ANONYMOUS, enclosing->module,
                 syntheticToken("#comprehension"));
    beginScope(&cmp);

    Parser checkpointB = comprehension(&cmp, checkpointA, closingToken);

    nativeCall(enclosing, klass);

    ObjFunction* function = endCompiler(&cmp);
    emitConstInstr(enclosing, OP_COMPREHENSION,
                   makeConstant(enclosing, OBJ_VAL(function)));
    closeUpvalues(function, &cmp, enclosing);

    // pick up at the end of the expression.
    gotoParser(checkpointB);

    return true;
  }

  // otherwise rewind.
  gotoParser(checkpointA);
  return false;
}

static bool trySetComprehension(Compiler* cmp) {
  return tryComprehension(cmp, S_SET, TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE);
}

static bool trySeqComprehension(Compiler* cmp) {
  return tryComprehension(cmp, S_SEQUENCE, TOKEN_LEFT_BRACKET,
                          TOKEN_RIGHT_BRACKET);
}

// A map literal, set literal, or set comprehension.
static void braces(Compiler* cmp, bool canAssign) {
  int elements = 0;

  // empty braces is an empty set.
  if (check(TOKEN_RIGHT_BRACE)) {
    advance(cmp);
    nativeCall(cmp, S_SET);
    return;
  }

  if (trySetComprehension(cmp)) {
    consume(cmp, TOKEN_RIGHT_BRACE, "Expect closing '}'.");
    return;
  }

  // first element: either a map key or a set element.
  // we need to advance this far in order to check the
  // next token.
  expression(cmp);
  elements++;

  // it's a singleton set.
  if (check(TOKEN_RIGHT_BRACE)) {
    nativePostfix(cmp, S_SET, elements);

  } else if (check(TOKEN_COMMA)) {
    // it's a |set| > 1 .
    advance(cmp);

    do {
      expression(cmp);
      elements++;

    } while (match(cmp, TOKEN_COMMA));

    nativePostfix(cmp, S_SET, elements);
  } else if (check(TOKEN_COLON)) {
    // it's a map.

    advance(cmp);     // colon.
    expression(cmp);  // value.
    nativePostfix(cmp, S_TUPLE, 2);

    while (match(cmp, TOKEN_COMMA)) {
      expression(cmp);  // key
      consume(cmp, TOKEN_COLON, "Expect ':' after map key.");
      expression(cmp);  // value.
      nativePostfix(cmp, S_TUPLE, 2);
      elements++;
    }

    nativePostfix(cmp, S_MAP, elements);
  }

  consume(cmp, TOKEN_RIGHT_BRACE, "Expect closing '}'.");
}

// Parse a sequence if appropriate and indicate if it is.
static bool sequence(Compiler* cmp) {
  int elements = 0;

  // empty seq.
  if (check(TOKEN_RIGHT_BRACKET)) {
    nativeCall(cmp, S_SEQUENCE);
    return true;
  }

  if (trySeqComprehension(cmp)) return true;

  // first datum. could be a tree node or a seq element.
  whiteDelimitedExpression(cmp);
  elements++;

  // singleton.
  if (check(TOKEN_RIGHT_BRACKET)) {
    nativePostfix(cmp, S_SEQUENCE, elements);
    return true;
  }

  if (match(cmp, TOKEN_COMMA)) {
    do {
      expression(cmp);
      elements++;
    } while (match(cmp, TOKEN_COMMA));
    nativePostfix(cmp, S_SEQUENCE, elements);

    return true;
  }

  return false;
}

void tree(Compiler* cmp, char* fn, int children) {
  // make a node of the first child.
  nativePostfix(cmp, "Node", 1);

  // parse the children.
  while (!check(TOKEN_RIGHT_BRACKET)) {
    whiteDelimitedExpression(cmp);
    nativePostfix(cmp, "Node", 1);
    children++;
  }

  // construct the root.
  nativePostfix(cmp, fn, children);
}

// A sequence literal, sequence comprehension, or tree.
static void brackets(Compiler* cmp, bool canAssign) {
  if (check(TOKEN_DOT)) {
    advance(cmp);
    whiteDelimitedExpression(cmp);
    whiteDelimitedExpression(cmp);
    tree(cmp, "Interior", 2);
  } else if (!sequence(cmp)) {
    tree(cmp, "Root", 1);
  }

  consume(cmp, TOKEN_RIGHT_BRACKET, "Expect closing ']'.");
}

// Subscript or "array indexing" operator like `foo[bar]`.
static void subscript(Compiler* cmp, bool canAssign) {
  expression(cmp);
  consume(cmp, TOKEN_RIGHT_BRACKET, "Expect ']' after arguments.");

  if (canAssign && match(cmp, TOKEN_EQUAL)) {
    expression(cmp);
    emitByte(cmp, OP_SUBSCRIPT_SET);
  } else {
    emitByte(cmp, OP_SUBSCRIPT_GET);
  }
}

static void classDeclaration(Compiler* cmp) {
  consumeIdentifier(cmp, "Expect class name.");
  Token className = parser.previous;
  uint16_t nameConstant = identifierConstant(cmp, &parser.previous);
  declareVariable(cmp);

  emitConstInstr(cmp, OP_CLASS, nameConstant);
  defineVariable(cmp, nameConstant);

  ClassCompiler classCompiler;
  classCompiler.enclosing = currentClass;
  currentClass = &classCompiler;

  if (match(cmp, TOKEN_EXTENDS)) {
    consumeIdentifier(cmp, "Expect superclass name.");
    variable(cmp, false);

    if (identifiersEqual(&className, &parser.previous)) {
      error(cmp, "A class can't inherit from itself.");
    }
  } else {
    // all classes inherit from [Object] unless they explicitly
    // inherit from another class.
    getGlobalConstant(cmp, S_OBJECT);
  }

  // "super" requires independent scope for adjacent
  // class declarations in order not to clash.
  beginScope(cmp);
  addLocal(cmp, syntheticToken("super"));
  defineVariable(cmp, 0);

  namedVariable(cmp, className, false);
  emitByte(cmp, OP_INHERIT);

  namedVariable(cmp, className, false);

  consume(cmp, TOKEN_LEFT_BRACE, "Expect '{' before class body.");
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    method(cmp);
  }
  consume(cmp, TOKEN_RIGHT_BRACE, "Expect '}' after class body.");

  // pop the classname.
  emitByte(cmp, OP_POP);
  endScope(cmp);

  currentClass = currentClass->enclosing;
}

static bool checkInfix() {
  return check(TOKEN_INFIX) || check(TOKEN_INFIX_LEFT) ||
         check(TOKEN_INFIX_RIGHT);
}

static int infixPrecedence(Compiler* cmp) {
  int prec = 0;
  int sign;

  // infix associativity defaults to left, so the infixl
  // declaration is vacuous, but we support it for symmetry.
  if (match(cmp, TOKEN_INFIX) || match(cmp, TOKEN_INFIX_LEFT))
    sign = 1;
  else if (match(cmp, TOKEN_INFIX_RIGHT))
    sign = -1;
  else
    return prec;

  if (currentClass == NULL && cmp->scopeDepth > 0)
    error(cmp, "Can only infix globals.");

  if (match(cmp, TOKEN_LEFT_PAREN)) {
    consume(cmp, TOKEN_NUMBER, "Expect numeral precedence.");

    prec = strtod(parser.previous.start, NULL);

    if (prec == 0) error(cmp, "Precedence must be > 0.");

    consume(cmp, TOKEN_RIGHT_PAREN, "Expect closing ')'.");
  } else {
    prec = PREC_FACTOR;
  }

  return prec * sign;
}

static bool checkPrefix(Compiler* cmp) {
  if (match(cmp, TOKEN_PREFIX)) return true;

  mapDelete(&vm.prefixes, identifierToken(parser.current));
  return false;
}

static void letDeclaration(Compiler* cmp) {
  int infixPrec = 0, prefixPrec = 0;

  if (checkPrefix(cmp))
    prefixPrec = 1;
  else if (checkInfix())
    infixPrec = infixPrecedence(cmp);

  uint16_t var = parseVariable(cmp, "Expect variable name.");
  Token name = parser.previous;
  emitByte(cmp, OP_UNDEFINED);
  defineVariable(cmp, var);

  bool annotated = false;
  if (match(cmp, TOKEN_COLON)) {
    typeExpression(cmp);
    annotated = true;
  }

  if (match(cmp, TOKEN_EQUAL)) {
    boundExpression(cmp, name);
  } else if (match(cmp, TOKEN_ARROW_LEFT)) {
    boundExpression(cmp, name);
    emitByte(cmp, OP_DESTRUCTURE);
  } else {
    emitByte(cmp, OP_NIL);
  }

  setVariable(cmp, var);
  emitByte(cmp, OP_POP);

  if (annotated) {
    // after the value assignment so that we annotate the value.
    defineType(cmp, var);
    emitByte(cmp, OP_POP);  // the type.
  }

  if (prefixPrec == 1)
    mapSet(&vm.prefixes, cmp->function->chunk.constants.values[var],
           NUMBER_VAL(1));
  else if (infixPrec != 0)
    mapSet(&vm.infixes, cmp->function->chunk.constants.values[var],
           NUMBER_VAL(infixPrec));
}

static void singleLetDeclaration(Compiler* cmp) {
  letDeclaration(cmp);
  consume(cmp, TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
}

static void multiLetDeclaration(Compiler* cmp) {
  do {
    letDeclaration(cmp);
  } while (match(cmp, TOKEN_COMMA));
  consume(cmp, TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
}

static void symbolDeclaration(Compiler* cmp) {
  do {
    uint16_t var = parseVariable(cmp, "Expect symbol name.");

    getGlobalConstant(cmp, "Symbol");
    Token name = parser.previous;
    loadConstant(cmp, identifierToken(name));
    emitBytes(cmp, OP_CALL, 1);
    defineVariable(cmp, var);

  } while (match(cmp, TOKEN_COMMA));

  consume(cmp, TOKEN_SEMICOLON, "Expect ';' after symbol declaration.");
}

static void domainDeclaration(Compiler* cmp) {
  uint16_t var = parseVariable(cmp, "Expect domain name.");
  Token name = parser.previous;
  emitByte(cmp, OP_UNDEFINED);
  defineVariable(cmp, var);

  consume(cmp, TOKEN_EQUAL, "Expect domain assignment.");
  getGlobalConstant(cmp, "Domain");
  loadConstant(cmp, identifierToken(name));
  consume(cmp, TOKEN_LEFT_BRACE, "Expect domain elements.");
  braces(cmp, false);

  emitBytes(cmp, OP_CALL, 2);

  setVariable(cmp, var);
  emitByte(cmp, OP_POP);
  consume(cmp, TOKEN_SEMICOLON, "Expect ';' after domain declaration.");
}

static void expressionStatement(Compiler* cmp) {
  expression(cmp);
  consume(cmp, TOKEN_SEMICOLON, "Expect ';' after expression.");
  emitByte(cmp, OP_EXPR_STATEMENT);
}

static int loopCondition(Compiler* cmp) {
  expression(cmp);
  consume(cmp, TOKEN_SEMICOLON, "Expect ';' after loop condition.");

  // jump out of the loop if the condition is false.
  int exitJump = emitJump(cmp, OP_JUMP_IF_FALSE);
  // condition.
  emitByte(cmp, OP_POP);

  return exitJump;
}

static int loopIncrement(Compiler* cmp, int loopStart) {
  int bodyJump = emitJump(cmp, OP_JUMP);
  int incrementStart = cmp->function->chunk.count;
  expression(cmp);
  emitByte(cmp, OP_POP);
  consume(cmp, TOKEN_RIGHT_PAREN, "Expect ')' after for clause.");

  emitLoop(cmp, loopStart);
  patchJump(cmp, bodyJump);

  return incrementStart;
}

static void forConditionStatement(Compiler* cmp) {
  if (match(cmp, TOKEN_SEMICOLON)) {
    // no initializer.
  } else if (match(cmp, TOKEN_LET)) {
    singleLetDeclaration(cmp);
  } else {
    expressionStatement(cmp);
  }

  int loopStart = cmp->function->chunk.count;
  int exitJump = match(cmp, TOKEN_SEMICOLON) ? -1 : loopCondition(cmp);

  if (!match(cmp, TOKEN_RIGHT_PAREN)) loopStart = loopIncrement(cmp, loopStart);

  statement(cmp);
  emitLoop(cmp, loopStart);

  if (exitJump != -1) {
    patchJump(cmp, exitJump);
    // condition.
    emitByte(cmp, OP_POP);
  }
}

static void forStatement(Compiler* cmp) {
  beginScope(cmp);
  consume(cmp, TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

  if (checkVariable()) {
    forInStatement(cmp);
  } else {
    forConditionStatement(cmp);
  }

  endScope(cmp);
}

static void ifStatement(Compiler* cmp) {
  consume(cmp, TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
  expression(cmp);
  consume(cmp, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int thenJump = emitJump(cmp, OP_JUMP_IF_FALSE);
  emitByte(cmp, OP_POP);

  statement(cmp);
  int elseJump = emitJump(cmp, OP_JUMP);

  patchJump(cmp, thenJump);
  emitByte(cmp, OP_POP);

  if (match(cmp, TOKEN_ELSE)) statement(cmp);
  patchJump(cmp, elseJump);
}

#define MAX_IMPORT_VARS 25

void useStatement(Compiler* cmp) {
  Token vars[MAX_IMPORT_VARS];
  int varcount = 0;

  rescanPathIdentifier(cmp);
  consume(cmp, TOKEN_IDENTIFIER, "Expect identifier.");
  Token token = parser.previous;

  while (check(TOKEN_COMMA)) {
    if (++varcount == MAX_IMPORT_VARS)
      return error(cmp, "Too many identifiers to import.");

    rescanPathIdentifier(cmp);
    consume(cmp, TOKEN_IDENTIFIER, "Expect identifier.");

    vars[varcount] = parser.previous;
  }

  if (match(cmp, TOKEN_FROM)) {
    varcount++;
    vars[0] = token;

    consumeIdentifier(cmp, "Expect path.");
  }

  Parser checkpoint = saveParser();
  ObjModule* module = vmCompileModule(cmp->function->module->dirName->chars,
                                      parser.previous, MODULE_IMPORT);
  gotoParser(checkpoint);

  if (module == NULL) return error(cmp, "Failed to compile import.");
  uint16_t moduleConst = makeConstant(cmp, OBJ_VAL(module));

  if (varcount > 0) {
    emitConstInstr(cmp, OP_IMPORT_FROM, moduleConst);
    emitByte(cmp, varcount);
    for (int i = 0; i < varcount; i++)
      emitConstant(cmp, identifierConstant(cmp, &vars[i]));
    return;
  }

  if (match(cmp, TOKEN_AS)) {
    consumeIdentifier(cmp, "Expect identifier alias.");
    uint16_t alias = identifierConstant(cmp, &parser.previous);
    emitConstInstr(cmp, OP_IMPORT_AS, moduleConst);
    emitConstant(cmp, alias);
    return;
  }

  emitConstInstr(cmp, OP_IMPORT, moduleConst);
}

static void printStatement(Compiler* cmp) {
  expression(cmp);
  consume(cmp, TOKEN_SEMICOLON, "Expect ';' after value.");
  emitByte(cmp, OP_PRINT);
}

static void returnStatement(Compiler* cmp) {
  if (cmp->functionType == TYPE_MODULE) {
    error(cmp, "Can't return from top-level code.");
  }
  if (cmp->functionType == TYPE_INITIALIZER) {
    error(cmp, "Can't return from an initializer.");
  }

  if (match(cmp, TOKEN_SEMICOLON)) {
    emitByte(cmp, OP_NIL);
  } else {
    expression(cmp);
    consume(cmp, TOKEN_SEMICOLON, "Expect ';' after return value.");
  }

  emitByte(cmp, OP_RETURN);
}

static void throwStatement(Compiler* cmp) {
  expression(cmp);
  emitByte(cmp, OP_THROW);
  consume(cmp, TOKEN_SEMICOLON, "Expect ';' after statement.");
}

static void whileStatement(Compiler* cmp) {
  int loopStart = cmp->function->chunk.count;

  consume(cmp, TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
  expression(cmp);
  consume(cmp, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int exitJump = emitJump(cmp, OP_JUMP_IF_FALSE);
  emitByte(cmp, OP_POP);
  statement(cmp);
  emitLoop(cmp, loopStart);

  patchJump(cmp, exitJump);
  emitByte(cmp, OP_POP);
}

static void synchronize(Compiler* cmp) {
  parser.panicMode = false;

  while (parser.current.type != TOKEN_EOF) {
    if (parser.previous.type == TOKEN_SEMICOLON) return;

    switch (parser.current.type) {
      case TOKEN_CLASS:
      case TOKEN_FOR:
      case TOKEN_IF:
      case TOKEN_LET:
      case TOKEN_WHILE:
      case TOKEN_PRINT:
      case TOKEN_RETURN:
        return;

      default:;  // Do nothing.
    }

    advance(cmp);
  }
}

static void declaration(Compiler* cmp) {
  if (match(cmp, TOKEN_CLASS)) {
    classDeclaration(cmp);
  } else if (match(cmp, TOKEN_LET)) {
    multiLetDeclaration(cmp);
  } else if (match(cmp, TOKEN_SYM)) {
    symbolDeclaration(cmp);
  } else if (match(cmp, TOKEN_DOM)) {
    domainDeclaration(cmp);
  } else {
    statement(cmp);
  }

  if (parser.panicMode) synchronize(cmp);
}

static void statement(Compiler* cmp) {
  if (match(cmp, TOKEN_FOR)) {
    forStatement(cmp);
  } else if (match(cmp, TOKEN_IF)) {
    ifStatement(cmp);
  } else if (check(TOKEN_USE)) {
    useStatement(cmp);
  } else if (match(cmp, TOKEN_LEFT_BRACE)) {
    beginScope(cmp);
    block(cmp);
    endScope(cmp);
  } else if (match(cmp, TOKEN_PRINT)) {
    printStatement(cmp);
  } else if (match(cmp, TOKEN_RETURN)) {
    returnStatement(cmp);
  } else if (match(cmp, TOKEN_THROW)) {
    throwStatement(cmp);
  } else if (match(cmp, TOKEN_WHILE)) {
    whileStatement(cmp);
  } else {
    expressionStatement(cmp);
  }
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] = {parentheses, call, PREC_CALL, PREC_NONE},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {braces, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_LEFT_BRACKET] = {brackets, subscript, PREC_CALL, PREC_NONE},
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_DOT] = {NULL, dot, PREC_CALL, PREC_NONE},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_BANG] = {unary, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY,
                          PREC_EQUALITY + PREC_STEP},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY,
                           PREC_EQUALITY + PREC_STEP},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_TYPE_VARIABLE] = {variable, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_TEX_STRING] = {texString, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_INTERPOLATION] = {stringInterpolation, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_TEX_INTERPOLATION] = {texInterpolation, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_AND] = {NULL, and_, PREC_AND, PREC_NONE},
    [TOKEN_OR] = {NULL, or_, PREC_OR, PREC_NONE},
    [TOKEN_UNDEFINED] = {undefined, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_FOR] = {forQuantification, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_IN] = {NULL, binary, PREC_COMPARISON, PREC_COMPARISON + PREC_STEP},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_NOT] = {prefixNot, infixNot, PREC_CALL, PREC_CALL},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_SUPER] = {super_, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_THIS] = {this_, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_LET] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_USER_PREFIX] = {prefix, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_USER_INFIX] = {NULL, infix, PREC_NONE, PREC_NONE},
};

static ParseRule* getPrefixRule(Compiler* cmp, Token token) {
  if (token.type == TOKEN_IDENTIFIER) {
    Value name = identifierToken(token);

    if (mapHas(&vm.prefixes, name)) return &rules[TOKEN_USER_PREFIX];
  }

  return &rules[token.type];
}

int sign(int x) { return (x > 0) - (x < 0); }

static void setPrecedence(Compiler* cmp, ParseRule* rule, int prec) {
  // the sign indicates associativity: -1 right, 1 left. 0 is not allowed.
  switch (sign(prec)) {
    case 1: {
      rule->leftPrec = prec;
      rule->rightPrec = prec + PREC_STEP;
      break;
    }
    case -1:
      rule->leftPrec = rule->rightPrec = prec * -1;
      break;
    default:
      error(cmp, "Unexpected precedence");
  }
}

// Look up the rule for the [token]'s type, unless the
// [token] is an identifier, in which case check the vm's
// infix tables for a user-defined infixation precedence.
static ParseRule* getInfixRule(Compiler* cmp, Token token) {
  if (token.type == TOKEN_IDENTIFIER) {
    Value name = identifierToken(token);
    Value prec;

    if (mapGet(&vm.infixes, name, &prec) ||
        mapGet(&vm.methodInfixes, name, &prec)) {
      setPrecedence(cmp, &rules[TOKEN_USER_INFIX], AS_NUMBER(prec));
      return &rules[TOKEN_USER_INFIX];
    }
  }

  return &rules[token.type];
}

static void parseDelimitedPrecedence(Compiler* cmp, Precedence precedence,
                                     DelimitFn delimit) {
  advance(cmp);

  ParseFn prefixRule = getPrefixRule(cmp, parser.previous)->prefix;

  if (prefixRule == NULL) {
    error(cmp, "Expect expression pre.");
    return;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(cmp, canAssign);

  while (delimit == NULL || !delimit()) {
    ParseRule* infixRule = getInfixRule(cmp, parser.current);

    if (precedence > infixRule->leftPrec) break;

    advance(cmp);

    if (infixRule->infix == NULL) {
      error(cmp, "Expect expression in.");
      return;
    }

    infixRule->infix(cmp, canAssign);
  }

  if (canAssign && match(cmp, TOKEN_EQUAL)) {
    error(cmp, "Invalid assignment target.");
  }
}

static void parsePrecedence(Compiler* cmp, Precedence precedence) {
  parseDelimitedPrecedence(cmp, precedence, NULL);
}

static void declarations(Compiler* cmp) {
  while (!match(cmp, TOKEN_EOF)) declaration(cmp);
}

ObjFunction* compileModule(Compiler* enclosing, const char* source, Token path,
                           ObjModule* module) {
  Scanner sc = initScanner(source);
  initParser(sc);

  Compiler cmp;
  initCompiler(&cmp, enclosing, NULL, TYPE_MODULE, module, path);

  declarations(&cmp);

  ObjFunction* function = endCompiler(&cmp);

  return parser.hadError ? NULL : function;
}

void markCompilerRoots(Compiler* cmp) {
  while (cmp != NULL) {
    markObject((Obj*)cmp->function);
    if (cmp->signature != NULL) markObject((Obj*)cmp->signature->function);
    cmp = cmp->enclosing;
  }
}