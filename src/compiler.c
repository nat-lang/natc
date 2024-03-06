#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "memory.h"
#include "scanner.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
  Scanner scanner;

  Token next;
  Token current;
  Token previous;
  Token penult;

  bool hadError;
  bool panicMode;
} Parser;

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,  // =
  PREC_OR,          // or
  PREC_AND,         // and
  PREC_EQUALITY,    // == !=
  PREC_COMPARISON,  // < > <= >=
  PREC_TERM,        // + -
  PREC_FACTOR,      // * /
  PREC_UNARY,       // ! -
  PREC_CALL,        // . ()
  PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

typedef struct {
  Token name;
  int depth;
  bool isCaptured;
} Local;

typedef struct {
  uint8_t index;
  bool isLocal;
} Upvalue;

typedef enum {
  TYPE_ANONYMOUS,
  TYPE_BOUND,
  TYPE_METHOD,
  TYPE_SCRIPT,
  TYPE_INITIALIZER,
} FunctionType;

typedef struct {
  // local slot of the bound variable.
  int var;
  // local slot of the object that implements the protocol.
  int iter;
  // stack offset of the first instruction of the body.
  int loopStart;
} Iterator;

typedef struct Compiler {
  struct Compiler* enclosing;
  ObjFunction* function;
  FunctionType type;

  Local locals[UINT8_COUNT];
  int localCount;
  Upvalue upvalues[UINT8_COUNT];
  int scopeDepth;

  ObjMap* constants;
} Compiler;

typedef struct ClassCompiler {
  struct ClassCompiler* enclosing;
  bool hasSuperclass;
} ClassCompiler;

Parser parser;
Compiler* current = NULL;
ClassCompiler* currentClass = NULL;

static Chunk* currentChunk() { return &current->function->chunk; }

static Parser saveParser() {
  Parser checkpoint = parser;

  checkpoint.scanner = saveScanner();

  return checkpoint;
}

static void gotoParser(Parser checkpoint) {
  gotoScanner(checkpoint.scanner);
  parser = checkpoint;
}

static void errorAt(Token* token, const char* message) {
  if (parser.panicMode)
    return;
  else
    parser.panicMode = true;

  fprintf(stderr, "[line %d] in %s, Error", token->line,
          current->function->name->chars);

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

static void error(const char* message) { errorAt(&parser.previous, message); }

static void errorAtCurrent(const char* message) {
  errorAt(&parser.current, message);
}

static void initIterator(Iterator* iter) {
  iter->var = 0;
  iter->iter = 0;
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

static void checkError() {
  if (parser.current.type == TOKEN_ERROR) errorAtCurrent(parser.current.start);
}

static void advance() {
  shiftParser();
  parser.next = scanToken();
  checkError();
}

static void advanceDottedIdentifier() {
  shiftParser();
  parser.next = dottedIdentifier();
  checkError();
}

static void consume(TokenType type, const char* message) {
  if (parser.current.type == type)
    advance();
  else
    errorAtCurrent(message);
}

static void consumeQuiet(TokenType type) {
  if (parser.current.type == type)
    advance();
  else
    parser.hadError = true;
}

static bool check(TokenType type) { return parser.current.type == type; }
static bool peek(TokenType type) { return parser.next.type == type; }

static bool match(TokenType type) {
  if (!check(type)) return false;
  advance();
  return true;
}

static bool prevWhite() {
  int offset = parser.previous.length + parser.current.length + 1;

  return isWhite(charAt(-offset));
}

static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

static void emitConstant(uint16_t constant) {
  emitBytes(constant >> 8, constant & 0xff);
}

static void emitConstInstr(uint8_t instruction, uint16_t constant) {
  emitByte(instruction);
  emitConstant(constant);
}

static void emitLoop(int loopStart) {
  emitByte(OP_LOOP);

  int offset = currentChunk()->count - loopStart + 2;
  if (offset > UINT16_MAX) error("Loop body too large.");

  emitByte((offset >> 8) & 0xff);
  emitByte(offset & 0xff);
}

static int emitJump(uint8_t instruction) {
  emitByte(instruction);
  emitByte(0xff);
  emitByte(0xff);
  return currentChunk()->count - 2;
}

static void emitReturn() {
  if (current->type == TYPE_INITIALIZER) {
    emitConstInstr(OP_GET_LOCAL, 0);
  } else {
    emitByte(OP_NIL);
  }

  emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value) {
  bool hashable = (value.type == VAL_BOOL || value.type == VAL_NIL ||
                   value.type == VAL_NUMBER || IS_STRING(value));
  Value existing;

  if (hashable && mapGet(current->constants, value, &existing) &&
      IS_NUMBER(existing)) {
    return (uint8_t)AS_NUMBER(existing);
  }

  int constant = addConstant(currentChunk(), value);
  if (constant > UINT16_MAX) {
    error("Too many constants in one chunk.");
    return 0;
  }

  if (hashable) mapSet(current->constants, value, NUMBER_VAL(constant));

  return (uint16_t)constant;
}

static void loadConstant(Value value) {
  int constant = makeConstant(value);
  emitConstInstr(OP_CONSTANT, constant);
}

static void patchJump(int offset) {
  // -2 to adjust for the bytecode for the jump offset itself.
  int jump = currentChunk()->count - offset - 2;

  if (jump > UINT16_MAX) {
    error("Too much code to jump over.");
  }

  currentChunk()->code[offset] = (jump >> 8) & 0xff;
  currentChunk()->code[offset + 1] = jump & 0xff;
}

static ObjString* functionName(FunctionType type, char* name) {
  switch (type) {
    case TYPE_INITIALIZER:
    case TYPE_METHOD:
      return copyString(parser.previous.start, parser.previous.length);
    case TYPE_BOUND:
      return copyString(parser.penult.start, parser.penult.length);
    case TYPE_ANONYMOUS:
      return copyString("lambda", 6);
    case TYPE_SCRIPT:
    default:
      return intern(name);
  }
}

static void initCompiler(Compiler* compiler, FunctionType type, char* name) {
  compiler->enclosing = current;
  compiler->function = NULL;
  compiler->type = type;
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  compiler->constants = NULL;
  compiler->function = newFunction();
  compiler->constants = newMap();

  current = compiler;
  current->function->name = functionName(type, name);

  Local* local = &current->locals[current->localCount++];
  local->depth = 0;
  if (type == TYPE_METHOD || type == TYPE_INITIALIZER) {
    local->name.start = "this";
    local->name.length = 4;
  } else {
    local->name.start = "";
    local->name.length = 0;
  }
}

static ObjFunction* endCompiler() {
  emitReturn();
  ObjFunction* function = current->function;

#ifdef DEBUG_PRINT_CODE
  if (!parser.hadError) {
    disassembleChunk(currentChunk(), function->name != NULL
                                         ? function->name->chars
                                         : "<script>");
  }
#endif

  current = current->enclosing;
  return function;
}

static void beginScope() { current->scopeDepth++; }

static void endScope() {
  current->scopeDepth--;

  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth > current->scopeDepth) {
    if (current->locals[current->localCount - 1].isCaptured) {
      emitByte(OP_CLOSE_UPVALUE);
    } else {
      emitByte(OP_POP);
    }

    current->localCount--;
  }
}

static void function(FunctionType type);
static void expression();
static void boundExpression();
static void statement();
static void declaration();
static void classDeclaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence, bool whiteSensitive);

static Value identifier(char* name) { return OBJ_VAL(intern(name)); }

static uint8_t identifierConstant(Token* name) {
  return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token* a, Token* b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name) {
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local* local = &compiler->locals[i];

    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error("Can't read local variable in its own initializer.");
      }
      return i;
    }
  }

  return -1;
}

static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
  int upvalueCount = compiler->function->upvalueCount;

  for (int i = 0; i < upvalueCount; i++) {
    Upvalue* upvalue = &compiler->upvalues[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;
    }
  }

  if (upvalueCount == UINT8_COUNT) {
    error("Too many closure variables in function.");
    return 0;
  }

  compiler->upvalues[upvalueCount].isLocal = isLocal;
  compiler->upvalues[upvalueCount].index = index;
  return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, Token* name) {
  if (compiler->enclosing == NULL) return -1;

  int local = resolveLocal(compiler->enclosing, name);
  if (local != -1) {
    compiler->enclosing->locals[local].isCaptured = true;
    return addUpvalue(compiler, (uint8_t)local, true);
  }

  int upvalue = resolveUpvalue(compiler->enclosing, name);
  if (upvalue != -1) {
    return addUpvalue(compiler, (uint8_t)upvalue, false);
  }

  return -1;
}

static int addLocal(Token name) {
  if (current->localCount == UINT8_COUNT) {
    error("Too many local variables in function.");
    return 0;
  }

  Local* local = &current->locals[current->localCount++];

  local->name = name;
  local->depth = -1;
  local->isCaptured = false;

  return current->localCount - 1;
}

static int declareVariable() {
  if (current->scopeDepth == 0) return 0;

  Token* name = &parser.previous;

  for (int i = current->localCount - 1; i >= 0; i--) {
    Local* local = &current->locals[i];
    if (local->depth != -1 && local->depth < current->scopeDepth) {
      break;
    }

    if (identifiersEqual(name, &local->name)) {
      error("Already a variable with this name in this scope.");
    }
  }

  return addLocal(*name);
}

static uint8_t argumentList() {
  uint8_t argCount = 0;
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      expression();

      if (argCount == 255) error("Can't have more than 255 arguments.");

      argCount++;
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
  return argCount;
}

static void binary(bool canAssign) {
  TokenType operatorType = parser.previous.type;
  ParseRule* rule = getRule(operatorType);
  parsePrecedence((Precedence)(rule->precedence + 1), false);

  switch (operatorType) {
    case TOKEN_BANG_EQUAL:
      emitBytes(OP_EQUAL, OP_NOT);
      break;
    case TOKEN_EQUAL_EQUAL:
      emitByte(OP_EQUAL);
      break;
    case TOKEN_GREATER:
      emitByte(OP_GREATER);
      break;
    case TOKEN_GREATER_EQUAL:
      emitBytes(OP_LESS, OP_NOT);
      break;
    case TOKEN_LESS:
      emitByte(OP_LESS);
      break;
    case TOKEN_LESS_EQUAL:
      emitBytes(OP_GREATER, OP_NOT);
      break;
    case TOKEN_PLUS:
      emitByte(OP_ADD);
      break;
    case TOKEN_MINUS:
      emitByte(OP_SUBTRACT);
      break;
    case TOKEN_STAR:
      emitByte(OP_MULTIPLY);
      break;
    case TOKEN_SLASH:
      emitByte(OP_DIVIDE);
      break;
    case TOKEN_IN:
      emitByte(OP_MEMBER);
      break;
    default:
      return;  // Unreachable.
  }
}

static void call(bool canAssign) {
  uint8_t argCount = argumentList();
  emitBytes(OP_CALL, argCount);
}

static void dot(bool canAssign) {
  consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
  uint8_t name = identifierConstant(&parser.previous);

  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitConstInstr(OP_SET_PROPERTY, name);
  } else if (match(TOKEN_LEFT_PAREN)) {
    uint8_t argCount = argumentList();
    emitConstInstr(OP_INVOKE, name);
    emitByte(argCount);
  } else {
    emitConstInstr(OP_GET_PROPERTY, name);
  }
}

static void literal(bool canAssign) {
  switch (parser.previous.type) {
    case TOKEN_FALSE:
      emitByte(OP_FALSE);
      break;
    case TOKEN_NIL:
      emitByte(OP_NIL);
      break;
    case TOKEN_TRUE:
      emitByte(OP_TRUE);
      break;
    default:
      return;  // Unreachable.
  }
}

static void grouping(bool canAssign) {
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  loadConstant(NUMBER_VAL(value));
}

static void and_(bool canAssign) {
  int endJump = emitJump(OP_JUMP_IF_FALSE);

  emitByte(OP_POP);
  parsePrecedence(PREC_AND, false);

  patchJump(endJump);
}

static void or_(bool canAssign) {
  int elseJump = emitJump(OP_JUMP_IF_FALSE);
  int endJump = emitJump(OP_JUMP);

  patchJump(elseJump);
  emitByte(OP_POP);

  parsePrecedence(PREC_OR, false);
  patchJump(endJump);
}

static void string(bool canAssign) {
  loadConstant(OBJ_VAL(
      copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

static void bareString() {
  loadConstant(
      OBJ_VAL(copyString(parser.previous.start, parser.previous.length)));
}

static void namedVariable(Token name, bool canAssign) {
  uint8_t getOp, setOp;
  int arg = resolveLocal(current, &name);

  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else if ((arg = resolveUpvalue(current, &name)) != -1) {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  } else {
    arg = identifierConstant(&name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitConstInstr(setOp, arg);
  } else {
    emitConstInstr(getOp, arg);
  }
}

static uint8_t nativeVariable(char* name) {
  uint8_t var = makeConstant(identifier(name));
  emitConstInstr(OP_GET_GLOBAL, var);
  return var;
}

static int nativeCall(char* name, int argCount) {
  int address = nativeVariable(name);
  emitBytes(OP_CALL, argCount);
  return address;
}

static void methodCall(char* name, int argCount) {
  Value method = identifier(name);
  uint8_t constant = makeConstant(method);
  emitConstInstr(OP_INVOKE, constant);
  emitByte(argCount);
}

static void variable(bool canAssign) {
  namedVariable(parser.previous, canAssign);
}

static Token syntheticToken(const char* text) {
  Token token;
  token.start = text;
  token.length = (int)strlen(text);
  return token;
}

static void super_(bool canAssign) {
  if (currentClass == NULL) {
    error("Can't use 'super' outside of a class.");
  } else if (!currentClass->hasSuperclass) {
    error("Can't use 'super' in a class with no superclass.");
  }

  consume(TOKEN_DOT, "Expect '.' after 'super'.");
  consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
  uint8_t name = identifierConstant(&parser.previous);

  namedVariable(syntheticToken("this"), false);

  if (match(TOKEN_LEFT_PAREN)) {
    uint8_t argCount = argumentList();
    namedVariable(syntheticToken("super"), false);
    emitConstInstr(OP_SUPER_INVOKE, name);
    emitByte(argCount);
  } else {
    namedVariable(syntheticToken("super"), false);
    emitConstInstr(OP_GET_SUPER, name);
  }
}

static void this_(bool canAssign) {
  if (currentClass == NULL) {
    error("Can't use 'this' outside of a class.");
    return;
  }
  variable(false);
}

static void unary(bool canAssign) {
  TokenType operatorType = parser.previous.type;

  // Compile the operand.
  parsePrecedence(PREC_UNARY, false);

  // Emit the operator instruction.
  switch (operatorType) {
    case TOKEN_BANG:
      emitByte(OP_NOT);
      break;
    case TOKEN_MINUS:
      emitByte(OP_NEGATE);
      break;
    default:
      return;  // Unreachable.
  }
}

static uint8_t parseVariable(const char* errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);
  declareVariable();

  if (current->scopeDepth > 0) return 0;

  return identifierConstant(&parser.previous);
}

static void markInitialized() {
  if (current->scopeDepth == 0) return;

  current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void defineVariable(uint8_t global) {
  if (current->scopeDepth > 0) {
    markInitialized();
    return;
  }

  emitConstInstr(OP_DEFINE_GLOBAL, global);
}

static bool peekSignature() {
  bool found;

  Parser checkpoint = saveParser();

  consumeQuiet(TOKEN_LEFT_PAREN);

  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      consumeQuiet(TOKEN_IDENTIFIER);
    } while (match(TOKEN_COMMA));
  }
  consumeQuiet(TOKEN_RIGHT_PAREN);
  consumeQuiet(TOKEN_ARROW);

  found = !parser.hadError;

  gotoParser(checkpoint);

  return found;
}

static bool tryFn(FunctionType fnType) {
  if (peekSignature()) {
    function(fnType);
    markInitialized();
    return true;
  }
  return false;
}

static void boundExpression() {
  if (tryFn(TYPE_BOUND)) return;

  parsePrecedence(PREC_ASSIGNMENT, false);
}

static void whiteDelimitedExpression() {
  if (tryFn(TYPE_ANONYMOUS)) return;

  parsePrecedence(PREC_ASSIGNMENT, true);
}

static void expression() {
  if (tryFn(TYPE_ANONYMOUS)) return;

  parsePrecedence(PREC_ASSIGNMENT, false);
}

static void block() {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void blockOrExpression() {
  if (check(TOKEN_LEFT_BRACE)) {
    advance();
    block();
  } else {
    expression();
    emitByte(OP_RETURN);
  }
}

static void function(FunctionType type) {
  Compiler compiler;
  initCompiler(&compiler, type, NULL);
  beginScope();

  consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      current->function->arity++;
      if (current->function->arity > 255) {
        errorAtCurrent("Can't have more than 255 parameters.");
      }
      uint8_t constant = parseVariable("Expect parameter name.");
      defineVariable(constant);
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
  consume(TOKEN_ARROW, "Expect '=>' after signature.");

  blockOrExpression();

  ObjFunction* function = endCompiler();
  emitConstInstr(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

  for (int i = 0; i < function->upvalueCount; i++) {
    emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
    emitByte(compiler.upvalues[i].index);
  }
}

static void method() {
  consume(TOKEN_IDENTIFIER, "Expect method name.");
  uint8_t constant = identifierConstant(&parser.previous);
  FunctionType type = TYPE_METHOD;
  if (parser.previous.length == 4 &&
      memcmp(parser.previous.start, "init", 4) == 0) {
    type = TYPE_INITIALIZER;
  }

  function(type);
  emitConstInstr(OP_METHOD, constant);
}

// Parse an iterator and store the details in an
// [Iterator] struct.
static Iterator iterator() {
  Iterator iter;
  initIterator(&iter);

  // consume the identifier.
  consume(TOKEN_IDENTIFIER, "Expect variable name.");

  // store the bound variable.
  loadConstant(NIL_VAL);
  iter.var = declareVariable();
  markInitialized();

  consume(TOKEN_IN, "Expect 'in' between identifier and sequence.");

  // initialize the iterator object, adjusting the local
  // count to reflect the iter fn's presence on the stack
  // during the iterator-yielding expression.
  nativeVariable(vm.iterString->chars);
  current->localCount++;
  expression();
  emitBytes(OP_CALL, 1);
  current->localCount--;

  // store it.
  iter.iter = addLocal(syntheticToken("#iter"));
  markInitialized();

  iter.loopStart = currentChunk()->count;
  return iter;
}

static int iterationNext(Iterator iter) {
  // call "more" on the iterator and bail if false.
  emitConstInstr(OP_GET_LOCAL, iter.iter);

  methodCall("more", 0);
  int exitJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);

  // otherwise continue iterating.
  emitConstInstr(OP_GET_LOCAL, iter.iter);
  methodCall("next", 0);
  emitConstInstr(OP_SET_LOCAL, iter.var);
  emitByte(OP_POP);

  return exitJump;
}

static void iterationEnd(Iterator iter, int exitJump) {
  emitLoop(iter.loopStart);
  patchJump(exitJump);
  emitByte(OP_POP);  // jump condition.
}

// Parse a comprehension, given the stack address [var] of the
// sequence under construction and the type of [closingToken].
Parser comprehension(Parser checkpointA, int var, TokenType closingToken) {
  Iterator iter;
  initIterator(&iter);

  int exitJump = 0;
  int predJump = 0;
  bool isIteration = false;
  bool isPredicate = false;
  Parser checkpointB = checkpointA;

  if (check(TOKEN_IDENTIFIER) && peek(TOKEN_IN)) {
    // bound variable and iterable to draw from.
    isIteration = true;

    beginScope();
    iter = iterator();
    exitJump = iterationNext(iter);
  } else {
    // a predicate to test against.
    isPredicate = true;
    expression();

    predJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
  }

  if (match(TOKEN_COMMA)) {
    // parse the conditions recursively, since this
    // makes scope management simple. in particular,
    // variables will be in scope in every condition
    // to their right, and all we have to do is conclude
    // each scope at the end of this function.
    checkpointB = comprehension(checkpointA, var, closingToken);
  } else if (check(closingToken)) {
    // now we parse the body, first saving the point
    // where the generator ends.
    checkpointB = saveParser();
    gotoParser(checkpointA);

    // load the comprehension instance, offsetting the local
    // count appropriately, and append the expression.
    emitConstInstr(OP_GET_LOCAL, var);
    current->localCount++;
    expression();
    methodCall("add", 1);
    // pop the comprehension instance.
    current->localCount--;
    emitByte(OP_POP);
  }

  if (isIteration) {
    iterationEnd(iter, exitJump);
    endScope();
  } else if (isPredicate) {
    // we need to jump over this last condition.
    // pop if the condition was truthy.
    int elseJump = emitJump(OP_JUMP);
    patchJump(predJump);
    emitByte(OP_POP);
    patchJump(elseJump);
  }

  return checkpointB;
}

// Find an occurance of '|' that isn't nested within
// braces or brackets, given some [initialBraceDepth]
// and [initialBracketDepth].
bool advanceToPipe(int initialBraceDepth, int initialBracketDepth) {
  int braceDepth = initialBraceDepth;
  int bracketDepth = initialBracketDepth;

  bool nested;

  for (;;) {
    if (check(TOKEN_LEFT_BRACE)) braceDepth++;
    if (check(TOKEN_RIGHT_BRACE)) braceDepth--;

    if (check(TOKEN_LEFT_BRACKET)) bracketDepth++;
    if (check(TOKEN_RIGHT_BRACKET)) bracketDepth--;

    nested = (braceDepth > initialBraceDepth) ||
             (bracketDepth > initialBracketDepth);

    if (check(TOKEN_PIPE) && !nested) return true;

    if (check(TOKEN_RIGHT_BRACE) && braceDepth == initialBraceDepth - 1)
      return false;
    if (check(TOKEN_RIGHT_BRACKET) && bracketDepth == initialBracketDepth - 1)
      return false;

    advance();
  }
}

static bool tryComprehension(TokenType closingToken, int initialBraceDepth,
                             int initialBracketDepth) {
  Parser checkpointA = saveParser();

  if (advanceToPipe(initialBraceDepth, initialBracketDepth)) {
    consume(TOKEN_PIPE, "Expect '|' between body and conditions.");

    // the comprehension instance is on top of the stack now;
    // we'll need to refer to it when we hit the bottom
    // of the condition clauses. we'll also need the local
    // slots to be offset by 1.
    int var = addLocal(syntheticToken("#comprehension"));
    markInitialized();

    Parser checkpointB = comprehension(checkpointA, var, closingToken);

    // reclaim the local slot but leave the sequence instance
    // on the stack.
    current->localCount--;

    // finally we pick up at the end of the expression.
    gotoParser(checkpointB);

    return true;
  }

  gotoParser(checkpointA);
  return false;
}

static bool trySetComprehension() {
  return tryComprehension(TOKEN_RIGHT_BRACE, 1, 0);
}

static bool trySeqComprehension() {
  return tryComprehension(TOKEN_RIGHT_BRACKET, 0, 1);
}

static void finishMapVal() {
  consume(TOKEN_COLON, "Expect ':' after map key.");
  // value.
  expression();
  emitByte(OP_SUBSCRIPT_SET);
}

static void finishSetVal() {
  loadConstant(BOOL_VAL(true));
  emitByte(OP_SUBSCRIPT_SET);
}

// A map literal, set literal, or set comprehension.
static void braces(bool canAssign) {
  int klass = nativeCall("Set", 0);

  // empty braces is an empty set.
  if (check(TOKEN_RIGHT_BRACE)) return advance();
  if (trySetComprehension()) {
    consume(TOKEN_RIGHT_BRACE, "Expect closing '}'.");
    return;
  }
  // first element: either a map key or a set element.
  // we need to advance this far in order to check the
  // next token.
  expression();

  // it's a singleton set.
  if (check(TOKEN_RIGHT_BRACE)) {
    finishSetVal();

  } else if (check(TOKEN_COMMA)) {
    // it's a |set| > 1 .
    advance();

    finishSetVal();
    do {
      expression();
      finishSetVal();
    } while (match(TOKEN_COMMA));

  } else if (check(TOKEN_COLON)) {
    // otherwise it's a map: backpatch the class.
    currentChunk()->constants.values[klass] = identifier("Map");

    // finish the first key/val pair, then any remaining elements.
    finishMapVal();

    while (match(TOKEN_COMMA)) {
      expression();
      finishMapVal();
    }
  }

  consume(TOKEN_RIGHT_BRACE, "Expect closing '}'.");
}

// A tree literal, sequence literal, or sequence comprehension.
static void brackets(bool canAssign) {
  int klass = nativeCall("Sequence", 0);

  // empty brackets is an empty sequence.
  if (check(TOKEN_RIGHT_BRACKET)) return advance();
  if (trySeqComprehension()) {
    consume(TOKEN_RIGHT_BRACKET, "Expect closing ']'.");
    return;
  }

  // first datum.
  whiteDelimitedExpression();

  // it's a sequence.
  if (check(TOKEN_COMMA) || check(TOKEN_RIGHT_BRACKET)) {
    methodCall("add", 1);

    while (match(TOKEN_COMMA)) {
      expression();
      methodCall("add", 1);
    }
  } else {
    // it's a tree.
    currentChunk()->constants.values[klass] = identifier("Tree");
    methodCall("addChild", 1);

    // and it may have branches.
    while (!check(TOKEN_RIGHT_BRACKET)) {
      whiteDelimitedExpression();
      methodCall("addChild", 1);
    }
  }

  consume(TOKEN_RIGHT_BRACKET, "Expect closing ']'.");
}

// Subscript or "array indexing" operator like `foo[bar]`.
static void subscript(bool canAssign) {
  expression();
  consume(TOKEN_RIGHT_BRACKET, "Expect ']' after arguments.");

  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitByte(OP_SUBSCRIPT_SET);
  } else {
    emitByte(OP_SUBSCRIPT_GET);
  }
}

static void classDeclaration() {
  consume(TOKEN_IDENTIFIER, "Expect class name.");
  Token className = parser.previous;
  uint8_t nameConstant = identifierConstant(&parser.previous);
  declareVariable();

  emitConstInstr(OP_CLASS, nameConstant);
  defineVariable(nameConstant);

  ClassCompiler classCompiler;
  classCompiler.hasSuperclass = false;
  classCompiler.enclosing = currentClass;
  currentClass = &classCompiler;

  if (match(TOKEN_LESS)) {
    consume(TOKEN_IDENTIFIER, "Expect superclass name.");
    variable(false);

    if (identifiersEqual(&className, &parser.previous)) {
      error("A class can't inherit from itself.");
    }

    // "super" requires independent scope for adjacent
    // class declarations in order not to clash.
    beginScope();
    addLocal(syntheticToken("super"));
    defineVariable(0);

    namedVariable(className, false);
    emitByte(OP_INHERIT);
    classCompiler.hasSuperclass = true;
  }

  namedVariable(className, false);

  consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    method();
  }
  consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");

  // pop the classname.
  emitByte(OP_POP);

  if (classCompiler.hasSuperclass) {
    endScope();
  }

  currentClass = currentClass->enclosing;
}

static void letDeclaration() {
  uint8_t var = parseVariable("Expect variable name.");

  if (match(TOKEN_EQUAL)) {
    boundExpression();
  } else {
    emitByte(OP_NIL);
  }
  consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

  defineVariable(var);
}

static void expressionStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  emitByte(OP_POP);
}

static int loopCondition() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

  // Jump out of the loop if the condition is false.
  int exitJump = emitJump(OP_JUMP_IF_FALSE);
  // Condition.
  emitByte(OP_POP);

  return exitJump;
}

static int loopIncrement(int loopStart) {
  int bodyJump = emitJump(OP_JUMP);
  int incrementStart = currentChunk()->count;
  expression();
  emitByte(OP_POP);
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

  emitLoop(loopStart);
  patchJump(bodyJump);

  return incrementStart;
}

// Invokes the iteration protocol.
static void forInStatement() {
  Iterator iter = iterator();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
  int exitJump = iterationNext(iter);
  statement();
  iterationEnd(iter, exitJump);
}

static void forConditionStatement() {
  if (match(TOKEN_SEMICOLON)) {
    // No initializer.
  } else if (match(TOKEN_LET)) {
    letDeclaration();
  } else {
    expressionStatement();
  }

  int loopStart = currentChunk()->count;
  int exitJump = match(TOKEN_SEMICOLON) ? -1 : loopCondition();

  if (!match(TOKEN_RIGHT_PAREN)) loopStart = loopIncrement(loopStart);

  statement();
  emitLoop(loopStart);

  if (exitJump != -1) {
    patchJump(exitJump);
    // Condition.
    emitByte(OP_POP);
  }
}

static void forStatement() {
  beginScope();
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

  if (check(TOKEN_IDENTIFIER)) {
    forInStatement();
  } else {
    forConditionStatement();
  }

  endScope();
}

static void ifStatement() {
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int thenJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);

  statement();
  int elseJump = emitJump(OP_JUMP);

  patchJump(thenJump);
  emitByte(OP_POP);

  if (match(TOKEN_ELSE)) statement();
  patchJump(elseJump);
}

static void importStatement() {
  advanceDottedIdentifier();
  consume(TOKEN_IDENTIFIER, "Expect path to import.");
  advance();
  bareString();
  emitByte(OP_IMPORT);
}

static void printStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after value.");
  emitByte(OP_PRINT);
}

static void returnStatement() {
  if (current->type == TYPE_SCRIPT) {
    error("Can't return from top-level code.");
  }

  if (match(TOKEN_SEMICOLON)) {
    emitReturn();
  } else {
    if (current->type == TYPE_INITIALIZER) {
      error("Can't return a value from an initializer.");
    }

    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
    emitByte(OP_RETURN);
  }
}

static void throwStatement() {
  expression();
  emitByte(OP_THROW);
  consume(TOKEN_SEMICOLON, "Expect ';' after statement.");
}

static void whileStatement() {
  int loopStart = currentChunk()->count;

  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int exitJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();
  emitLoop(loopStart);

  patchJump(exitJump);
  emitByte(OP_POP);
}

static void synchronize() {
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

    advance();
  }
}

static void declaration() {
  if (match(TOKEN_CLASS)) {
    classDeclaration();
  } else if (match(TOKEN_LET)) {
    letDeclaration();
  } else {
    statement();
  }

  if (parser.panicMode) synchronize();
}

static void statement() {
  if (match(TOKEN_FOR)) {
    forStatement();
  } else if (match(TOKEN_IF)) {
    ifStatement();
  } else if (check(TOKEN_IMPORT)) {
    importStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } else if (match(TOKEN_PRINT)) {
    printStatement();
  } else if (match(TOKEN_RETURN)) {
    returnStatement();
  } else if (match(TOKEN_THROW)) {
    throwStatement();
  } else if (match(TOKEN_WHILE)) {
    whileStatement();
  } else {
    expressionStatement();
  }
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {braces, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACKET] = {brackets, subscript, PREC_CALL},
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, dot, PREC_CALL},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, and_, PREC_AND},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_IN] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, or_, PREC_OR},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {super_, NULL, PREC_NONE},
    [TOKEN_THIS] = {this_, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_LET] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

static ParseRule* getRule(TokenType type) { return &rules[type]; }

static void parsePrecedence(Precedence precedence, bool whiteSensitive) {
  advance();

  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == NULL) {
    error("Expect expression.");
    return;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(canAssign);

  if (whiteSensitive && prevWhite()) return;

  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    error("Invalid assignment target.");
  }
}

ObjFunction* compile(const char* source, char* path) {
  Scanner sc = initScanner(source);
  initParser(sc);

  Compiler compiler;
  initCompiler(&compiler, TYPE_SCRIPT, path);

  while (!match(TOKEN_EOF)) {
    declaration();
  }

  ObjFunction* function = endCompiler();
  return parser.hadError ? NULL : function;
}

void markCompilerRoots() {
  Compiler* compiler = current;
  while (compiler != NULL) {
    markObject((Obj*)compiler->function);
    compiler = compiler->enclosing;
  }
}