
#include "nodeCompiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/_types/_null.h>

#include "common.h"
#include "node.h"
#include "scanner.h"
#include "vm.h"

typedef enum { SIG_NAKED, SIG_PAREN, SIG_NOT } SignatureType;

typedef struct {
  Scanner scanner;

  Token next;
  Token current;
  Token previous;
  Token penult;
  Token ppenult;

  bool hadError;
  bool panicMode;
} Parser;

Parser parser;

void initParser(Scanner scanner) {
  parser.scanner = scanner;

  parser.hadError = false;
  parser.panicMode = false;
  parser.current = scanToken();
  parser.next = scanToken();
}

Parser saveParser() {
  Parser checkpoint = parser;

  checkpoint.scanner = saveScanner();

  return checkpoint;
}

void gotoParser(Parser checkpoint) {
  gotoScanner(checkpoint.scanner);
  parser = checkpoint;
}

bool prev(TokenType type) { return parser.previous.type == type; }
bool check(TokenType type) { return parser.current.type == type; }
bool peek(TokenType type) { return parser.next.type == type; }

bool checkVariable() {
  return check(TOKEN_IDENTIFIER) || check(TOKEN_TYPE_VARIABLE);
}

void errorAt(AstNode* fn, Token* token, const char* message) {
  if (parser.panicMode)
    return;
  else
    parser.panicMode = true;

  fprintf(stderr, "Error in %s:%d", fn->fn->as.function.name->chars,
          token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end.");
  } else if (token->type == TOKEN_ERROR) {
    // Nothing.
  } else {
    fprintf(stderr, " at '%.*s'.", token->length, token->start);
  }
  fprintf(stderr, " %s\n", message);

  parser.hadError = true;
}

void errorAtCurrent(AstNode* fn, const char* message) {
  errorAt(fn, &parser.current, message);
}

void error(AstNode* fn, const char* message) {
  errorAt(fn, &parser.previous, message);
  fn->hadError = true;
}

void checkError(AstNode* fn) {
  if (parser.current.type == TOKEN_ERROR)
    errorAtCurrent(fn, parser.current.start);
}

void shiftParser() {
  parser.ppenult = parser.penult;
  parser.penult = parser.previous;
  parser.previous = parser.current;
  parser.current = parser.next;
}

void advance(AstNode* fn) {
  shiftParser();
  parser.next = scanToken();
  checkError(fn);
}

bool match(AstNode* fn, TokenType type) {
  if (!check(type)) return false;
  advance(fn);
  return true;
}

void consume(AstNode* fn, TokenType type, const char* message) {
  if (parser.current.type == type)
    advance(fn);
  else
    errorAtCurrent(fn, message);
}

void consumeIdentifier(AstNode* fn, const char* message) {
  if (parser.current.type == TOKEN_IDENTIFIER ||
      parser.current.type == TOKEN_TYPE_VARIABLE)
    advance(fn);
  else
    errorAtCurrent(fn, message);
}

bool matchParamOrPattern(AstNode* fn) {
  return match(fn, TOKEN_IDENTIFIER) || match(fn, TOKEN_TYPE_VARIABLE) ||
         match(fn, TOKEN_NUMBER) || match(fn, TOKEN_TRUE) ||
         match(fn, TOKEN_FALSE) || match(fn, TOKEN_NIL) ||
         match(fn, TOKEN_UNDEFINED) || match(fn, TOKEN_STRING);
}

typedef AstNode* (*ParseFn)(AstNode* fn, bool canAssign);
typedef AstNode* (*InfixFn)(AstNode* fn, bool canAssign, AstNode* lhs,
                            Precedence prec);

typedef struct {
  ParseFn prefix;
  InfixFn infix;
  Precedence leftPrec;
  Precedence rightPrec;
} ParseRule;

static AstNode* statement(AstNode* fn);
static AstNode* expression(AstNode* fn);
static AstNode* parsePrecedence(AstNode* fn, Precedence precedence);
static AstNode* nakedFunction(AstNode* fn, Token name);

static bool identifiersEqual(Token* a, Token* b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static uint8_t addLocal(AstNode* fn, Token name) {
  if (fn->fn->as.function.localCount == UINT8_COUNT) {
    error(fn, "Too many local variables in function.");
    return 0;
  }

  Local* local = &fn->fn->as.function.locals[fn->fn->as.function.localCount++];

  local->name = name;
  local->depth = -1;
  local->isCaptured = false;

  return fn->fn->as.function.localCount - 1;
}

static int resolveLocal(AstNode* fn, Token* name) {
  for (int i = fn->fn->as.function.localCount - 1; i >= 0; i--) {
    Local* local = &fn->fn->as.function.locals[i];

    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error(fn, "Can't read local variable in its own initializer.");
      }
      return i;
    }
  }

  return -1;
}

static void markInitialized(AstNode* fn) {
  fn->fn->as.function.locals[fn->fn->as.function.localCount - 1].depth =
      fn->as.function.depth;
}

static int addUpvalue(AstNode* fn, uint8_t index, bool isLocal) {
  int upvalueCount = fn->fn->as.function.upvalueCount;

  for (int i = 0; i < upvalueCount; i++) {
    Upvalue* upvalue = &fn->fn->as.function.upvalues[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;
    }
  }

  if (upvalueCount == UINT8_COUNT) {
    error(fn, "Too many closure variables in function.");
    return 0;
  }

  fn->fn->as.function.upvalues[upvalueCount].isLocal = isLocal;
  fn->fn->as.function.upvalues[upvalueCount].index = index;
  return fn->fn->as.function.upvalueCount++;
}

static int resolveUpvalue(AstNode* fn, Token* name) {
  if (fn->fn == NULL) return -1;

  int local = resolveLocal(fn->fn, name);
  if (local != -1) {
    fn->fn->as.function.locals[local].isCaptured = true;
    return addUpvalue(fn, (uint8_t)local, true);
  }

  int upvalue = resolveUpvalue(fn->fn, name);
  if (upvalue != -1) {
    return addUpvalue(fn, (uint8_t)upvalue, false);
  }

  return -1;
}

static void beginScope(AstNode* fn) { fn->fn->as.function.depth++; }

static void endScope(AstNode* fn) {
  fn->fn->as.function.depth--;

  while (fn->fn->as.function.localCount > 0 &&
         fn->fn->as.function.locals[fn->fn->as.function.localCount - 1].depth >
             fn->fn->as.function.depth) {
    fn->fn->as.function.localCount--;
  }
}

static AstNode* identifier(AstNode* fn, bool canAssign) {
  if (check(TOKEN_FAT_ARROW)) return nakedFunction(fn, parser.ppenult);

  Token name = parser.previous;

  AstNode* node = newUnknownNode();
  int address = -1;
  if ((address = resolveLocal(fn, &name)) >= 0) {
    node = newVarLocalNode((uint8_t)address, NULL);
    node->as.local.name = tokenString(name);
  } else if ((address = resolveUpvalue(fn, &name)) >= 0) {
    node = newVarUpvalueNode((uint8_t)address, NULL);
    node->as.upvalue.name = tokenString(name);
  } else {
    node = newVarGlobalNode(NULL);
    node->as.global.name = tokenString(name);
  }

  if (match(fn, TOKEN_EQUAL)) {
    if (!canAssign) error(fn, "Invalid assignment target.");
    AstNode* rhs = expression(fn);
    node = newAssignmentNode(node, rhs);
  }

  return node;
}
static AstNode* parameter(AstNode* fn) {
  addLocal(fn, parser.previous);
  markInitialized(fn);
  return newParamNode(tokenString(parser.previous), NULL);
}

static AstNode* signature(AstNode* fn) {
  AstNode* node = newSignatureNode();

  if (!check(TOKEN_PAREN_RIGHT)) {
    do {
      if (!checkVariable()) {
        errorAtCurrent(fn, "Expecting parameter name.");
        return newUnknownNode();
      }
      AstNode* paramNode = parameter(fn);
      pushAstVec(&node->as.signature.params, paramNode);
    } while (match(fn, TOKEN_COMMA));
  }

  return node;
}

static AstNode* block(AstNode* fn) {
  AstNode* node = newBlockNode();

  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    AstNode* stmtNode = statement(fn);
    pushAstVec(&node->as.block.stmts, stmtNode);
  }

  consume(fn, TOKEN_RIGHT_BRACE, "Expect '}' after block.");

  return node;
}

static AstNode* functionBody(AstNode* fn) {
  AstNode* node = NULL;

  if (check(TOKEN_LEFT_BRACE)) {
    advance(fn);
    node = block(fn);
    pushAstVec(&node->as.block.stmts, newReturnNode(newLiteralNode(NIL_VAL)));
  } else {
    node = expression(fn);
    node = newReturnNode(node);
  }

  return node;
}

static AstNode* nakedFunction(AstNode* fn, Token name) {
  AstNode* node =
      newFunctionNode(tokenString(name), enclosing->fn->as.function.module);
  NodeCompiler fn;
  initNodeCompiler(&fn, enclosing, node);
  beginScope(&fn);
  AstNode* sigNode = newSignatureNode();
  AstNode* paramNode = parameter(&fn);
  pushAstVec(&sigNode->as.signature.params, paramNode);
  node->as.function.signature = sigNode;

  consume(&fn, TOKEN_FAT_ARROW, "Expect '=>' after signature.");
  node->as.function.body = functionBody(&fn);
  endScope(&fn);
  return node;
}

AstNode* function(NodeCompiler* enclosing, Token name) {
  AstNode* node =
      newFunctionNode(tokenString(name), enclosing->fn->as.function.module);
  NodeCompiler fn;
  initNodeCompiler(&fn, enclosing, node);
  beginScope(&fn);

  node->as.function.signature = signature(&fn);
  consume(&fn, TOKEN_PAREN_RIGHT, "Expect ')' after parameters.");
  consume(&fn, TOKEN_FAT_ARROW, "Expect '=>' after signature.");
  node->as.function.body = functionBody(&fn);
  endScope(&fn);
  return node;
}

static AstNode* literal(AstNode* fn, bool canAssign) {
  Value value;
  switch (parser.previous.type) {
    case TOKEN_TRUE:
      value = BOOL_VAL(true);
      break;
    case TOKEN_FALSE:
      value = BOOL_VAL(false);
      break;
    default:
      return newUnknownNode();
  }
  AstNode* node = newLiteralNode(value);
  node->line = parser.previous.line;
  return node;
}

static AstNode* number(AstNode* fn, bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  AstNode* node = newLiteralNode(NUMBER_VAL(value));
  node->line = parser.previous.line;
  return node;
}

static AstNode* string(AstNode* fn, bool canAssign) {
  // Extract string content (excluding quotes)
  // parser.previous.start points to the opening quote
  // parser.previous.length includes both quotes
  ObjString* str =
      copyString(parser.previous.start + 1, parser.previous.length - 2);
  AstNode* node = newLiteralNode(OBJ_VAL(str));
  node->line = parser.previous.line;
  return node;
}

static void argumentList(AstNode* fn, AstVec* vec) {
  uint8_t argCount = 0;
  if (!check(TOKEN_PAREN_RIGHT)) {
    do {
      AstNode* node = expression(fn);
      pushAstVec(vec, node);

      if (argCount == 255) error(fn, "Can't have more than 255 arguments.");

      argCount++;
    } while (match(fn, TOKEN_COMMA));
  }
  consume(fn, TOKEN_PAREN_RIGHT, "Expect ')' after arguments.");
}

static AstNode* userInfix(AstNode* fn, bool canAssign, AstNode* lhs,
                          Precedence prec) {
  AstNode* fn = identifier(fn, false);
  AstNode* rhs = parsePrecedence(fn, prec);
  return newCallInfixNode(fn, lhs, rhs);
}

static AstNode* call(AstNode* fn, bool canAssign, AstNode* lhs,
                     Precedence prec) {
  AstNode* node = newCallNode(lhs);
  argumentList(fn, &node->as.call.args);
  return node;
}

// Find a [token] that isn't nested within braces, brackets,
// or parentheses, for some initial [depth],
static bool advanceTo(AstNode* fn, TokenType token, TokenType closing,
                      int initialDepth) {
  int depth = initialDepth;

  for (;;) {
    if (check(TOKEN_LEFT_BRACE) || check(TOKEN_LEFT_BRACKET) ||
        check(TOKEN_PAREN_LEFT))
      depth++;
    if (check(TOKEN_RIGHT_BRACE) || check(TOKEN_RIGHT_BRACKET) ||
        check(TOKEN_PAREN_RIGHT))
      depth--;
    // found one.
    if (check(token) && depth == initialDepth) return true;
    // found none.
    if (check(closing) && depth == 0) return false;
    advance(fn);
  }
}

static bool peekFunction(AstNode* fn) {
  // 0 arg function.
  if (check(TOKEN_PAREN_RIGHT) && peek(TOKEN_FAT_ARROW)) return true;

  do {
    if (!matchParamOrPattern(fn)) return false;
    if (check(TOKEN_COLON)) {
      advanceTo(fn, TOKEN_COMMA, TOKEN_PAREN_RIGHT, 1);
    }

  } while (match(fn, TOKEN_COMMA));

  if (!match(fn, TOKEN_PAREN_RIGHT)) return false;
  if (!match(fn, TOKEN_FAT_ARROW)) return false;

  // n arg function.
  return true;
}

static AstNode* objectLiteral(AstNode* fn, bool canAssign) {
  AstNode* obj = newObjectNode();

  if (check(TOKEN_RIGHT_BRACE)) {
    advance(fn);
    return obj;
  }

  do {
    AstNode* key;
    if (check(TOKEN_IDENTIFIER) || check(TOKEN_TYPE_VARIABLE)) {
      advance(fn);
      ObjString* keyName = tokenString(parser.previous);
      key = newLiteralNode(OBJ_VAL(keyName));
    } else if (match(fn, TOKEN_STRING)) {
      key = string(fn, false);
    } else {
      errorAtCurrent(fn, "Expect identifier or string literal for object key.");
      return obj;
    }

    consume(fn, TOKEN_COLON, "Expect ':' after object key.");

    AstNode* value = expression(fn);
    AstNode* entry = newObjectEntryNode(key, value);
    pushAstVec(&obj->as.object.entries, entry);

    if (check(TOKEN_RIGHT_BRACE)) break;
    if (!match(fn, TOKEN_COMMA)) {
      errorAtCurrent(fn, "Expect ',' or '}' after object value.");
      break;
    }
  } while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF));

  consume(fn, TOKEN_RIGHT_BRACE, "Expect '}' after object literal.");
  return obj;
}

static AstNode* parentheses(AstNode* fn, bool canAssign) {
  // empty sequence.
  if (match(fn, TOKEN_COMMA)) {
    consume(fn, TOKEN_PAREN_RIGHT, "Expect ')'.");
    return newSequenceNode();
  }

  // function?
  Parser checkpoint = saveParser();
  bool isFunction = peekFunction(fn);
  gotoParser(checkpoint);
  if (isFunction) return function(fn, parser.ppenult);

  // sequence.
  AstNode* node = expression(fn);

  if (check(TOKEN_COMMA)) {
    AstNode* seq = newSequenceNode();
    pushAstVec(&seq->as.sequence.values, node);
    do {
      advance(fn);
      // allow a trailing comma.
      if (check(TOKEN_PAREN_RIGHT)) break;
      pushAstVec(&seq->as.sequence.values, expression(fn));
    } while (check(TOKEN_COMMA));

    node = seq;
  }
  consume(fn, TOKEN_PAREN_RIGHT, "Expect ')' after expression.");
  return node;
}

static ParseRule rules[] = {
    [TOKEN_IDENTIFIER] = {identifier, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_TYPE_VARIABLE] = {identifier, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {objectLiteral, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_PAREN_LEFT] = {parentheses, call, PREC_CALL, PREC_NONE},
    [TOKEN_PAREN_RIGHT] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_USER_INFIX] = {NULL, userInfix, PREC_NONE, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE, PREC_NONE},
};

#define PREC_STEP 1

static int sign(int x) { return (x > 0) - (x < 0); }

static void setPrecedence(AstNode* fn, ParseRule* rule, int prec) {
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
      error(fn, "Unexpected precedence");
  }
}

// Look up the rule for the [token]'s type, unless the
// [token] is an identifier, in which case check the vm's
// infix tables for a user-defined infixation precedence.
static ParseRule* getInfixRule(AstNode* fn, Token token) {
  if (token.type == TOKEN_IDENTIFIER) {
    Value name = tokenValue(token);
    Value prec;

    if (mapGet(&vm.infixes, name, &prec) ||
        mapGet(&vm.methodInfixes, name, &prec)) {
      setPrecedence(fn, &rules[TOKEN_USER_INFIX], AS_NUMBER(prec));
      return &rules[TOKEN_USER_INFIX];
    }
  }

  return &rules[token.type];
}

static AstNode* parsePrecedence(AstNode* fn, Precedence precedence) {
  AstNode* node = newUnknownNode();

  advance(fn);

  ParseFn prefixRule = rules[parser.previous.type].prefix;
  if (prefixRule == NULL) {
    error(fn, "Expect expression.");
    return node;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  node = prefixRule(fn, canAssign);

  ParseRule* infixRule;
  while (precedence <=
         (infixRule = getInfixRule(fn, parser.current))->leftPrec) {
    advance(fn);
    node = infixRule->infix(fn, canAssign, node, infixRule->rightPrec);
  }

  if (canAssign && match(fn, TOKEN_EQUAL))
    error(fn, "Invalid assignment target.");

  return node;
}

static AstNode* expression(AstNode* fn) {
  return parsePrecedence(fn, PREC_ASSIGNMENT);
}

static AstNode* letDeclaration(AstNode* fn) {
  consumeIdentifier(fn, "Expect variable name.");
  Token nameToken = parser.previous;

  addLocal(fn, nameToken);

  AstNode* node = newUnknownNode();
  if (match(fn, TOKEN_EQUAL)) {
    node = expression(fn);
  } else {
    node = newLiteralNode(UNDEF_VAL);
    node->line = parser.previous.line;
  }

  markInitialized(fn);

  ObjString* name = tokenString(nameToken);
  return newLetNode(name, node);
}

static AstNode* ifStatement(AstNode* fn) {
  consume(fn, TOKEN_PAREN_LEFT, "Expect '(' after 'if'.");
  AstNode* cond = expression(fn);
  consume(fn, TOKEN_PAREN_RIGHT, "Expect ')' after condition.");

  AstNode* then = statement(fn);

  AstNode* elseBranch = NULL;
  if (match(fn, TOKEN_ELSE)) {
    elseBranch = statement(fn);
  }

  return newIfNode(cond, then, elseBranch);
}

static AstNode* whileStatement(AstNode* fn) {
  consume(fn, TOKEN_PAREN_LEFT, "Expect '(' after 'while'.");
  AstNode* cond = expression(fn);
  consume(fn, TOKEN_PAREN_RIGHT, "Expect ')' after condition.");
  AstNode* body = statement(fn);
  return newWhileNode(cond, body);
}

static void rescanCurrentAsPathIdentifier(AstNode* fn) {
  rewindScanner(parser.current);
  parser.current = scanPathIdentifier();
  parser.next = scanToken();
}

static AstNode* importStatement(AstNode* fn) {
  rescanCurrentAsPathIdentifier(fn);
  consume(fn, TOKEN_IDENTIFIER, "Expect identifier for module path.");

  Parser checkpoint = saveParser();
  if (fn->fn->as.function.module == NULL) {
    error(fn, "Can't import from fn that has no module.");
    return newUnknownNode();
  }

  AstNode* module = vmCompileModuleImportBody(
      fn, fn->fn->as.function.module->as.module.dirName->chars,
      parser.previous);
  gotoParser(checkpoint);

  ObjString* alias = NULL;
  if (match(fn, TOKEN_AS)) {
    consume(fn, TOKEN_IDENTIFIER, "Expect identifier for alias.");
    Token aliasToken = parser.previous;
    alias = tokenString(aliasToken);
  }

  return newUseNode(module, alias);
}

static AstNode* throwStatement(AstNode* fn) {
  AstNode* expr = expression(fn);
  return newThrowNode(expr);
}

static AstNode* statement(AstNode* fn) {
  if (match(fn, TOKEN_IF)) {
    ifStatement(fn);
  } else if (match(fn, TOKEN_WHILE)) {
    whileStatement(fn);
  } else if (match(fn, TOKEN_USE)) {
    importStatement(fn);
  } else if (match(fn, TOKEN_THROW)) {
    throwStatement(fn);
  } else if (match(fn, TOKEN_LET)) {
    letDeclaration(fn);
  } else if (match(fn, TOKEN_LEFT_BRACE)) {
    block(fn);
  } else {
    expression(fn);
    newExprStmtNode(fn);
  }
}

static void statements(AstNode* fn) {
  while (!match(fn, TOKEN_EOF)) {
    statement(fn);
  }
}

void compileModuleImportBody(AstNode* fn, AstNode* module) {
  Scanner sc = initScanner(module->as.module.source->chars);
  initParser(sc);
  statements(fn, &module->as.module.stmts);
}

AstNode* compileFunctionNode(AstNode* module) {
  AstFunction* fn = (AstFunction*)module->as.module.fn;
  module->as.module.fn = newFunctionNode(module);
  module->as.module.fn->as.function.name = module->as.module.baseName;
  module->as.module.fn->as.function.signature = newSignatureNode();
  module->as.module.fn->as.function.body = newBlockNode();

  statements(module->as.module.fn);
  pushAstVec(&module->as.module.fn->as.function.body->as.block.stmts,
             newReturnNode(NULL));
  module->as.module.fn->as.function.body->as.block.stmts.items[0]
      ->as.xReturn.value = newLiteralNode(NIL_VAL);
  return node;
}

void compileModuleNode(AstNode* module) {
  Scanner sc = initScanner(module->as.module.source->chars);
  initParser(sc);
  compileFunctionNode(module);
}