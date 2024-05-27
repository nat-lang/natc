#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
typedef bool (*DelimitFn)();

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
} Compiler;

typedef struct ClassCompiler {
  struct ClassCompiler* enclosing;
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

static bool prev(TokenType type) { return parser.previous.type == type; }
static bool check(TokenType type) { return parser.current.type == type; }
static bool peek(TokenType type) { return parser.next.type == type; }

// Keyword symbols that are also valid identifiers
// aren't tokenized, so we need to check for the
// bare string.
static bool checkStr(char* str) {
  return strncmp(parser.current.start, str, strlen(str)) == 0;
}

static void consume(TokenType type, const char* message) {
  if (parser.current.type == type)
    advance();
  else
    errorAtCurrent(message);
}

static void consumeQuietly(TokenType type) {
  if (parser.current.type == type)
    advance();
  else
    parser.hadError = true;
}

static bool match(TokenType type) {
  if (!check(type)) return false;
  advance();
  return true;
}

// Is there whitespace preceding the previous token?
static bool penultWhite() { return isWhite(*(parser.previous.start - 1)); }

// Is there whitespace preceding the current token?
static bool prevWhite() { return isWhite(*(parser.current.start - 1)); }

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

static int getConstant(Value value) {
  Value existing;

  if (isHashable(value) &&
      mapGet(&current->function->constants, value, &existing) &&
      IS_NUMBER(existing)) {
    return (uint16_t)AS_NUMBER(existing);
  }

  return -1;
}

static uint16_t makeConstant(Value value) {
  int existing = getConstant(value);
  if (existing != -1) return existing;

  int constant = addConstant(currentChunk(), value);

  if (constant > UINT16_MAX) {
    error("Too many constants in one chunk.");
    return 0;
  }

  if (isHashable(value))
    mapSet(&current->function->constants, value, NUMBER_VAL(constant));

  return (uint16_t)constant;
}

static void loadConstant(Value value) {
  uint16_t constant = makeConstant(value);
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
  compiler->function = newFunction();
  compiler->type = type;
  compiler->localCount = 0;
  compiler->scopeDepth = 0;

  current = compiler;
  current->function->name = functionName(type, name);

  for (int i = 0; i < UINT8_COUNT; i++) {
    compiler->locals[i].depth = 0;
    compiler->locals[i].isCaptured = false;

    initToken(&compiler->locals[i].name);
  }

  Local* local = &current->locals[current->localCount++];
  local->depth = 0;
  local->isCaptured = false;
  local->name.type = TOKEN_IDENTIFIER;
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
    disassembleChunk(&function->chunk, function->name != NULL
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
static ParseRule* getRule(Token token);
static void parseDelimitedPrecedence(Precedence precedence, DelimitFn fn);
static void parsePrecedence(Precedence precedence);

static Value identifier(char* name) { return INTERN(name); }

static Value identifierToken(Token token) {
  return OBJ_VAL(copyString(token.start, token.length));
}

static uint16_t identifierConstant(Token* token) {
  return makeConstant(identifierToken(*token));
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

static uint8_t addLocal(Token name) {
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

static uint8_t declareLocal(Token* name) {
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

static uint8_t declareVariable() {
  if (current->scopeDepth == 0) return 0;

  return declareLocal(&parser.previous);
}

static uint16_t nativeVariable(char* name) {
  uint16_t var = makeConstant(identifier(name));
  emitConstInstr(OP_GET_GLOBAL, var);
  return var;
}

void getProperty(char* name) {
  uint16_t var = makeConstant(INTERN(name));
  emitConstInstr(OP_GET_PROPERTY, var);
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
  ParseRule* rule = getRule(parser.previous);
  parsePrecedence((Precedence)(rule->precedence + 1));

  switch (operatorType) {
    case TOKEN_BANG_EQUAL:
      emitBytes(OP_EQUAL, OP_NOT);
      break;
    case TOKEN_EQUAL_EQUAL:
      emitByte(OP_EQUAL);
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

static void property(bool canAssign) {
  consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
  uint16_t name = identifierConstant(&parser.previous);

  if (canAssign && match(TOKEN_EQUAL)) {
    // if we're here, then there's a value on the stack
    // that's not reflected in the local count. that won't
    // matter *unless* we're assigning a comprehension, which
    // allocates its own locals in the *same scope* as the
    // assignment.
    current->localCount++;
    expression();
    current->localCount--;
    emitConstInstr(OP_SET_PROPERTY, name);
  } else if (match(TOKEN_LEFT_PAREN)) {
    uint8_t argCount = argumentList();
    emitConstInstr(OP_INVOKE, name);
    emitByte(argCount);
  } else {
    emitConstInstr(OP_GET_PROPERTY, name);
  }
}

// A dot flush against an expression is property access;
// a dot with whitespace preceding and following it is
// function composition. We dont require the whitespace
// following composition to disambiguate the two, but we
// may as well enforce the symmetry.
static void dot(bool canAssign) {
  if (penultWhite() && prevWhite()) {
    nativeVariable("compose");
    expression();
    emitByte(OP_CALL_INFIX);
  } else {
    property(canAssign);
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

static void parentheses(bool canAssign) {
  expression();

  if (check(TOKEN_COMMA)) {
    int argCount = 1;
    do {
      advance();
      expression();
      argCount++;
    } while (check(TOKEN_COMMA));

    nativeVariable(S_TUPLE);
    emitBytes(OP_CALL_POSTFIX, argCount);
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  loadConstant(NUMBER_VAL(value));
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
  } else if (canAssign && match(TOKEN_ARROW_LEFT)) {
    expression();
    emitByte(OP_DESTRUCTURE);
    emitConstInstr(setOp, arg);
  } else {
    emitConstInstr(getOp, arg);
  }
}

static int nativeCall(char* name, int argCount) {
  int address = nativeVariable(name);
  emitBytes(OP_CALL, argCount);
  return address;
}

static void methodCall(char* name, int argCount) {
  Value method = identifier(name);
  uint16_t constant = makeConstant(method);
  emitConstInstr(OP_INVOKE, constant);
  emitByte(argCount);
}

static void variable(bool canAssign) {
  namedVariable(parser.previous, canAssign);
}

static void infix(bool canAssign) {
  ParseRule* rule = getRule(parser.previous);

  variable(canAssign);

  if (penultWhite() && prevWhite()) {
    parsePrecedence((Precedence)(rule->precedence + 1));
    emitByte(OP_CALL_INFIX);
  }
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
  }

  consume(TOKEN_DOT, "Expect '.' after 'super'.");
  consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
  uint16_t name = identifierConstant(&parser.previous);

  if (match(TOKEN_LEFT_PAREN)) {
    uint8_t argCount = argumentList();
    namedVariable(syntheticToken("this"), false);
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
  parsePrecedence(PREC_UNARY);

  // Emit the operator instruction.
  switch (operatorType) {
    case TOKEN_BANG:
      emitByte(OP_NOT);
      break;
    default:
      return;  // Unreachable.
  }
}

static uint16_t parseVariable(const char* errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);
  uint8_t local = declareVariable();

  if (current->scopeDepth > 0) return local;

  return identifierConstant(&parser.previous);
}

static void markInitialized() {
  if (current->scopeDepth == 0) return;

  current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void defineVariable(uint16_t variable) {
  if (current->scopeDepth > 0) {
    markInitialized();
    return;
  }

  emitConstInstr(OP_DEFINE_GLOBAL, variable);
}

static bool peekSignature() {
  bool found;

  Parser checkpoint = saveParser();

  consumeQuietly(TOKEN_LEFT_PAREN);

  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      consumeQuietly(TOKEN_IDENTIFIER);
    } while (match(TOKEN_COMMA));
  }
  consumeQuietly(TOKEN_RIGHT_PAREN);
  consumeQuietly(TOKEN_FAT_ARROW);

  found = !parser.hadError;

  gotoParser(checkpoint);

  return found;
}

static bool tryFunction(FunctionType fnType) {
  if (peekSignature()) {
    function(fnType);
    return true;
  }
  return false;
}

static void boundExpression() {
  if (tryFunction(TYPE_BOUND)) return;

  parsePrecedence(PREC_ASSIGNMENT);
}

static void whiteDelimitedExpression() {
  if (tryFunction(TYPE_ANONYMOUS)) return;

  parseDelimitedPrecedence(PREC_ASSIGNMENT, prevWhite);
}

static void expression() {
  if (tryFunction(TYPE_ANONYMOUS)) return;

  parsePrecedence(PREC_ASSIGNMENT);
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
      if (current->function->variadic)
        error("Can only apply * to the final parameter.");

      current->function->arity++;
      if (current->function->arity > 255) {
        errorAtCurrent("Can't have more than 255 parameters.");
      }

      if (checkStr("*")) {
        current->function->variadic = true;
        // shift the star off the parameter's token.
        parser.current.start++;
        parser.current.length--;
      }

      uint16_t constant = parseVariable("Expect parameter name.");
      defineVariable(constant);
      mapSet(&current->function->signature, NUMBER_VAL(constant),
             identifierToken(current->locals[constant].name));
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
  consume(TOKEN_FAT_ARROW, "Expect '=>' after signature.");

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
  uint16_t constant = identifierConstant(&parser.previous);
  FunctionType type = TYPE_METHOD;
  if (parser.previous.length == 4 &&
      memcmp(parser.previous.start, S_INIT, 4) == 0) {
    type = TYPE_INITIALIZER;
  }

  function(type);
  emitConstInstr(OP_METHOD, constant);

  if (!prev(TOKEN_RIGHT_BRACE)) {
    consume(TOKEN_SEMICOLON, "Expect ';' after method with expression body.");
  }
}

// Parse an iterator and store the details in an
// [Iterator] struct.
static Iterator iterator() {
  Iterator iter;
  initIterator(&iter);

  // store the bound variable.
  iter.var = parseVariable("Expect identifier.");
  emitByte(OP_NIL);
  defineVariable(iter.var);

  consume(TOKEN_IN, "Expect 'in' between identifier and iterable.");

  // the potentially iterable expression.
  expression();

  // turn the expression into an iterator instance and store it.
  nativeVariable(S_ITER);
  emitBytes(OP_CALL_POSTFIX, 1);
  iter.iter = addLocal(syntheticToken("#iter"));
  markInitialized();

  iter.loopStart = currentChunk()->count;

  return iter;
}

static int iterationNext(Iterator iter) {
  // more().
  emitConstInstr(OP_GET_LOCAL, iter.iter);
  methodCall("more", 0);

  // jump out of the loop if more() false.
  int exitJump = emitJump(OP_JUMP_IF_FALSE);
  // condition.
  emitByte(OP_POP);

  // next().
  emitConstInstr(OP_GET_LOCAL, iter.iter);
  methodCall("next", 0);
  emitConstInstr(OP_SET_LOCAL, iter.var);
  emitByte(OP_POP);

  return exitJump;
}

static void iterationEnd(Iterator iter, int exitJump) {
  emitLoop(iter.loopStart);
  patchJump(exitJump);
  // condition.
  emitByte(OP_POP);
}
static void forInStatement() {
  Iterator iter = iterator();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clause.");
  int exitJump = iterationNext(iter);
  statement();
  iterationEnd(iter, exitJump);
}

// Parse a comprehension, given the stack address [var] of the
// sequence under construction and the type of [closingToken].
Parser comprehension(Parser checkpointA, int var, TokenType closingToken) {
  Iterator iter;

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

    // load the comprehension instance and append the expression.
    expression();
    emitConstInstr(OP_GET_LOCAL, var);
    getProperty(S_PUSH);
    emitBytes(OP_CALL_POSTFIX, 1);
    emitByte(OP_POP);  // comprehension instance.
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
    advance();  // eat the pipe.

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
  emitByte(OP_TRUE);
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
    // TODO: now that we have a postfix operation, we should
    // use that here instead of backpatching.
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
  int klass = nativeCall(S_SEQUENCE, 0);

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
    methodCall(S_PUSH, 1);

    while (match(TOKEN_COMMA)) {
      expression();
      methodCall(S_PUSH, 1);
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
  uint16_t nameConstant = identifierConstant(&parser.previous);
  declareVariable();

  emitConstInstr(OP_CLASS, nameConstant);
  defineVariable(nameConstant);

  ClassCompiler classCompiler;
  classCompiler.enclosing = currentClass;
  currentClass = &classCompiler;

  if (match(TOKEN_EXTENDS)) {
    consume(TOKEN_IDENTIFIER, "Expect superclass name.");
    variable(false);

    if (identifiersEqual(&className, &parser.previous)) {
      error("A class can't inherit from itself.");
    }
  } else {
    // all classes inherit from [Object] unless they explicitly
    // inherit from another class.
    nativeVariable(S_OBJECT);
  }

  // "super" requires independent scope for adjacent
  // class declarations in order not to clash.
  beginScope();
  addLocal(syntheticToken("super"));
  defineVariable(0);

  namedVariable(className, false);
  emitByte(OP_INHERIT);

  namedVariable(className, false);

  consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    method();
  }
  consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");

  // pop the classname.
  emitByte(OP_POP);
  endScope();

  currentClass = currentClass->enclosing;
}

static void letDeclaration() {
  int infixPrecedence = -1;

  if (check(TOKEN_INFIX)) {
    if (current->scopeDepth > 0) error("Can only infix globals.");
    advance();

    if (check(TOKEN_LEFT_PAREN)) {
      advance();
      consume(TOKEN_NUMBER, "Expect numeral precedence.");

      infixPrecedence = strtod(parser.previous.start, NULL);

      consume(TOKEN_RIGHT_PAREN, "Expect closing ')'.");
    } else {
      infixPrecedence = PREC_CALL;
    }
  }

  uint16_t var = parseVariable("Expect variable name.");
  emitByte(OP_UNDEFINED);
  defineVariable(var);

  if (match(TOKEN_EQUAL)) {
    boundExpression();
  } else if (match(TOKEN_ARROW_LEFT)) {
    expression();
    emitByte(OP_DESTRUCTURE);
  } else {
    emitByte(OP_NIL);
  }

  if (current->scopeDepth > 0) {
    emitConstInstr(OP_SET_LOCAL, var);
  } else {
    emitConstInstr(OP_SET_GLOBAL, var);

    if (infixPrecedence != -1) {
      Value name = currentChunk()->constants.values[var];
      mapSet(&vm.infixes, name, NUMBER_VAL(infixPrecedence));
    }
  }

  emitByte(OP_POP);
  consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
}

static void expressionStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  emitByte(OP_EXPR_STATEMENT);
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
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clause.");

  emitLoop(loopStart);
  patchJump(bodyJump);

  return incrementStart;
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
    [TOKEN_LEFT_PAREN] = {parentheses, call, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {braces, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACKET] = {brackets, subscript, PREC_CALL},
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, dot, PREC_CALL},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_IDENTIFIER] = {variable, infix, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_IN] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
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

// Simply look up the rule for the [token]'s type, unless the
// [token] is an identifier, in which case check the parser's
// infix table for a user-defined infixation precedence.
static ParseRule* getRule(Token token) {
  if (token.type == TOKEN_IDENTIFIER) {
    Value name = identifierToken(token);
    Value prec;

    if (mapGet(&vm.infixes, name, &prec)) {
      rules[TOKEN_IDENTIFIER].infix = infix;
      rules[TOKEN_IDENTIFIER].precedence = AS_NUMBER(prec);
    } else {
      rules[TOKEN_IDENTIFIER].infix = NULL;
      rules[TOKEN_IDENTIFIER].precedence = PREC_NONE;
    }
  }

  return &rules[token.type];
}

static void parseDelimitedPrecedence(Precedence precedence, DelimitFn delimit) {
  advance();

  ParseFn prefixRule = getRule(parser.previous)->prefix;

  if (prefixRule == NULL) {
    error("Expect expression.");
    return;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(canAssign);

  while (!(delimit != NULL && delimit()) &&
         precedence <= getRule(parser.current)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous)->infix;

    if (infixRule == NULL) {
      error("Expect expression.");
      return;
    }

    infixRule(canAssign);
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    error("Invalid assignment target.");
  }
}

static void parsePrecedence(Precedence precedence) {
  parseDelimitedPrecedence(precedence, NULL);
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