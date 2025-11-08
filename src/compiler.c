
#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void errorAt(NodeCompiler* cmp, Token* token, const char* message) {
  if (parser.panicMode)
    return;
  else
    parser.panicMode = true;

  fprintf(stderr, "Error in %s:%d", cmp->fn->as.function.name->chars,
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

void errorAtCurrent(NodeCompiler* cmp, const char* message) {
  errorAt(cmp, &parser.current, message);
}

void error(NodeCompiler* cmp, const char* message) {
  errorAt(cmp, &parser.previous, message);
  cmp->hadError = true;
}

void checkError(NodeCompiler* cmp) {
  if (parser.current.type == TOKEN_ERROR)
    errorAtCurrent(cmp, parser.current.start);
}

void shiftParser() {
  parser.ppenult = parser.penult;
  parser.penult = parser.previous;
  parser.previous = parser.current;
  parser.current = parser.next;
}

void advance(NodeCompiler* cmp) {
  shiftParser();
  parser.next = scanToken();
  checkError(cmp);
}

bool match(NodeCompiler* cmp, TokenType type) {
  if (!check(type)) return false;
  advance(cmp);
  return true;
}

void consume(NodeCompiler* cmp, TokenType type, const char* message) {
  if (parser.current.type == type)
    advance(cmp);
  else
    errorAtCurrent(cmp, message);
}

void consumeIdentifier(NodeCompiler* cmp, const char* message) {
  if (parser.current.type == TOKEN_IDENTIFIER ||
      parser.current.type == TOKEN_TYPE_VARIABLE)
    advance(cmp);
  else
    errorAtCurrent(cmp, message);
}

bool matchParamOrPattern(NodeCompiler* cmp) {
  return match(cmp, TOKEN_IDENTIFIER) || match(cmp, TOKEN_TYPE_VARIABLE) ||
         match(cmp, TOKEN_NUMBER) || match(cmp, TOKEN_TRUE) ||
         match(cmp, TOKEN_FALSE) || match(cmp, TOKEN_NIL) ||
         match(cmp, TOKEN_UNDEFINED) || match(cmp, TOKEN_STRING);
}

typedef AstNode* (*ParseFn)(NodeCompiler* cmp, bool canAssign);
typedef AstNode* (*InfixFn)(NodeCompiler* cmp, bool canAssign, AstNode* lhs,
                            Precedence prec);

typedef struct {
  ParseFn prefix;
  InfixFn infix;
  Precedence leftPrec;
  Precedence rightPrec;
} ParseRule;

static AstNode* statement(NodeCompiler* cmp);
static AstNode* expression(NodeCompiler* cmp);
static AstNode* parsePrecedence(NodeCompiler* cmp, Precedence precedence);
static AstNode* nakedFunction(NodeCompiler* enclosing, Token name);

void initNodeCompiler(NodeCompiler* cmp, NodeCompiler* enclosing,
                      AstNode* node) {
  cmp->enclosing = NULL;
  cmp->enclosing = enclosing;
  cmp->fn = NULL;
  cmp->fn = node;
  cmp->scopeDepth = 0;
  cmp->hadError = false;
}

static bool identifiersEqual(Token* a, Token* b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static uint8_t addLocal(NodeCompiler* cmp, Token name) {
  if (cmp->fn->as.function.localCount == UINT8_COUNT) {
    error(cmp, "Too many local variables in function.");
    return 0;
  }

  Local* local =
      &cmp->fn->as.function.locals[cmp->fn->as.function.localCount++];

  local->name = name;
  local->depth = -1;
  local->isCaptured = false;

  return cmp->fn->as.function.localCount - 1;
}

static int resolveLocal(NodeCompiler* cmp, Token* name) {
  for (int i = cmp->fn->as.function.localCount - 1; i >= 0; i--) {
    Local* local = &cmp->fn->as.function.locals[i];

    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error(cmp, "Can't read local variable in its own initializer.");
      }
      return i;
    }
  }

  return -1;
}

static void markInitialized(NodeCompiler* cmp) {
  cmp->fn->as.function.locals[cmp->fn->as.function.localCount - 1].depth =
      cmp->scopeDepth;
}

static int addUpvalue(NodeCompiler* cmp, uint8_t index, bool isLocal) {
  int upvalueCount = cmp->fn->as.function.upvalueCount;

  for (int i = 0; i < upvalueCount; i++) {
    Upvalue* upvalue = &cmp->fn->as.function.upvalues[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;
    }
  }

  if (upvalueCount == UINT8_COUNT) {
    error(cmp, "Too many closure variables in function.");
    return 0;
  }

  cmp->fn->as.function.upvalues[upvalueCount].isLocal = isLocal;
  cmp->fn->as.function.upvalues[upvalueCount].index = index;
  return cmp->fn->as.function.upvalueCount++;
}

static int resolveUpvalue(NodeCompiler* cmp, Token* name) {
  if (cmp->enclosing == NULL) return -1;

  int local = resolveLocal(cmp->enclosing, name);
  if (local != -1) {
    cmp->enclosing->fn->as.function.locals[local].isCaptured = true;
    return addUpvalue(cmp, (uint8_t)local, true);
  }

  int upvalue = resolveUpvalue(cmp->enclosing, name);
  if (upvalue != -1) {
    return addUpvalue(cmp, (uint8_t)upvalue, false);
  }

  return -1;
}

static void beginScope(NodeCompiler* cmp) { cmp->scopeDepth++; }

static void endScope(NodeCompiler* cmp) {
  cmp->scopeDepth--;

  while (
      cmp->fn->as.function.localCount > 0 &&
      cmp->fn->as.function.locals[cmp->fn->as.function.localCount - 1].depth >
          cmp->scopeDepth) {
    cmp->fn->as.function.localCount--;
  }
}

static AstNode* identifier(NodeCompiler* cmp, bool canAssign) {
  if (check(TOKEN_FAT_ARROW)) return nakedFunction(cmp, parser.ppenult);

  Token name = parser.previous;

  AstNode* node = newUnknownNode();
  int address = -1;
  if ((address = resolveLocal(cmp, &name)) >= 0) {
    node = newVarLocalNode((uint8_t)address);
    node->as.local.name = tokenString(name);
  } else if ((address = resolveUpvalue(cmp, &name)) >= 0) {
    node = newVarUpvalueNode((uint8_t)address);
    node->as.upvalue.name = tokenString(name);
  } else {
    node = newVarGlobalNode();
    node->as.global.name = tokenString(name);
  }

  if (match(cmp, TOKEN_EQUAL)) {
    if (!canAssign) error(cmp, "Invalid assignment target.");
    AstNode* rhs = expression(cmp);
    node = newAssignmentNode(node, rhs);
  }

  return node;
}
static AstNode* parameter(NodeCompiler* cmp) {
  addLocal(cmp, parser.previous);
  markInitialized(cmp);
  AstNode* node = newParamNode(NULL);
  node->as.param.name = tokenString(parser.previous);
  return node;
}

static AstNode* signature(NodeCompiler* cmp) {
  AstNode* node = newSignatureNode();

  if (!check(TOKEN_PAREN_RIGHT)) {
    do {
      if (!checkVariable()) {
        errorAtCurrent(cmp, "Expecting parameter name.");
        return newUnknownNode();
      }
      AstNode* paramNode = parameter(cmp);
      pushAstVec(&node->as.signature.params, paramNode);
    } while (match(cmp, TOKEN_COMMA));
  }

  return node;
}

static AstNode* block(NodeCompiler* cmp) {
  AstNode* block = newBlockNode();

  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    AstNode* stmtNode = statement(cmp);
    pushAstVec(&block->as.block.stmts, stmtNode);
  }

  consume(cmp, TOKEN_RIGHT_BRACE, "Expect '}' after block.");

  return block;
}

static AstNode* functionBody(NodeCompiler* cmp) {
  AstNode* node = NULL;

  if (check(TOKEN_LEFT_BRACE)) {
    advance(cmp);
    node = block(cmp);
    AstNode* defaultReturn = newReturnNode(newLiteralValueNode(NIL_VAL));
    pushAstVec(&node->as.block.stmts, defaultReturn);
  } else {
    node = expression(cmp);
    node = newReturnNode(node);
  }

  return node;
}

static AstNode* nakedFunction(NodeCompiler* enclosing, Token name) {
  AstNode* node = newFunctionNode(enclosing->fn->as.function.module);
  node->as.function.name = tokenString(name);
  NodeCompiler cmp;
  initNodeCompiler(&cmp, enclosing, node);
  beginScope(&cmp);
  AstNode* sigNode = newSignatureNode();
  AstNode* paramNode = parameter(&cmp);
  pushAstVec(&sigNode->as.signature.params, paramNode);
  node->as.function.signature = sigNode;

  consume(&cmp, TOKEN_FAT_ARROW, "Expect '=>' after signature.");
  node->as.function.body = functionBody(&cmp);
  endScope(&cmp);
  return node;
}

AstNode* function(NodeCompiler* enclosing, Token name) {
  AstNode* node = newFunctionNode(enclosing->fn->as.function.module);
  node->as.function.name = tokenString(name);
  NodeCompiler cmp;
  initNodeCompiler(&cmp, enclosing, node);
  beginScope(&cmp);

  node->as.function.signature = signature(&cmp);
  consume(&cmp, TOKEN_PAREN_RIGHT, "Expect ')' after parameters.");
  consume(&cmp, TOKEN_FAT_ARROW, "Expect '=>' after signature.");
  node->as.function.body = functionBody(&cmp);
  endScope(&cmp);
  return node;
}

static AstNode* boolean(NodeCompiler* cmp, bool canAssign) {
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
  AstNode* node = newLiteralValueNode(value);
  node->line = parser.previous.line;
  return node;
}

static AstNode* number(NodeCompiler* cmp, bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  AstNode* node = newLiteralValueNode(NUMBER_VAL(value));
  node->line = parser.previous.line;
  return node;
}

static AstNode* string(NodeCompiler* cmp, bool canAssign) {
  AstNode* node = newLiteralNode();
  node->as.literal.value = OBJ_VAL(
      copyString(parser.previous.start + 1, parser.previous.length - 2));
  node->line = parser.previous.line;
  return node;
}

static void argumentList(NodeCompiler* cmp, AstVec* vec) {
  uint8_t argCount = 0;
  if (!check(TOKEN_PAREN_RIGHT)) {
    do {
      AstNode* node = expression(cmp);
      pushAstVec(vec, node);

      if (argCount == 255) error(cmp, "Can't have more than 255 arguments.");

      argCount++;
    } while (match(cmp, TOKEN_COMMA));
  }
  consume(cmp, TOKEN_PAREN_RIGHT, "Expect ')' after arguments.");
}

static AstNode* userInfix(NodeCompiler* cmp, bool canAssign, AstNode* lhs,
                          Precedence prec) {
  AstNode* fn = identifier(cmp, false);
  AstNode* rhs = parsePrecedence(cmp, prec);
  return newCallInfixNode(fn, lhs, rhs);
}

static AstNode* call(NodeCompiler* cmp, bool canAssign, AstNode* lhs,
                     Precedence prec) {
  AstNode* node = newCallNode(lhs);
  argumentList(cmp, &node->as.call.args);
  return node;
}

// Find a [token] that isn't nested within braces, brackets,
// or parentheses, for some initial [depth],
static bool advanceTo(NodeCompiler* cmp, TokenType token, TokenType closing,
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
    advance(cmp);
  }
}

static bool peekFunction(NodeCompiler* cmp) {
  // 0 arg function.
  if (check(TOKEN_PAREN_RIGHT) && peek(TOKEN_FAT_ARROW)) return true;

  do {
    if (!matchParamOrPattern(cmp)) return false;
    if (check(TOKEN_COLON)) {
      advanceTo(cmp, TOKEN_COMMA, TOKEN_PAREN_RIGHT, 1);
    }

  } while (match(cmp, TOKEN_COMMA));

  if (!check(TOKEN_PAREN_RIGHT)) return false;
  if (!peek(TOKEN_FAT_ARROW)) return false;

  // n arg function.
  return true;
}

static AstNode* parseComprehension(NodeCompiler* cmp, Parser bodyCheckpoint,
                                   ComprehensionType type,
                                   TokenType closingToken) {
  AstNode* comprehension = newComprehensionNode(NULL, type);
  int scopesOpened = 0;
  bool sawClause = false;

  while (!check(closingToken) && !check(TOKEN_EOF)) {
    if (checkVariable() && peek(TOKEN_IN)) {
      beginScope(cmp);
      scopesOpened++;

      consumeIdentifier(cmp, "Expect variable name.");
      Token varName = parser.previous;
      uint8_t localIndex = addLocal(cmp, varName);
      markInitialized(cmp);

      AstNode* var = newVarLocalNode(localIndex);
      var->as.local.name = tokenString(varName);

      consume(cmp, TOKEN_IN, "Expect 'in' after variable name.");
      AstNode* iterable = expression(cmp);

      AstNode* iterCond = newComprehensionIterNode(var, iterable);
      pushAstVec(&comprehension->as.comprehension.conditions, iterCond);
    } else {
      AstNode* predicate = expression(cmp);
      AstNode* predCond = newComprehensionPredNode(predicate);
      pushAstVec(&comprehension->as.comprehension.conditions, predCond);
    }

    sawClause = true;

    if (check(closingToken)) {
      break;
    }

    if (!match(cmp, TOKEN_COMMA)) {
      errorAtCurrent(cmp,
                     "Expect ',' or closing token after comprehension "
                     "condition.");
      break;
    }
  }

  if (!sawClause) {
    error(cmp, "Comprehension requires at least one clause.");
  }

  Parser afterConditions = saveParser();

  gotoParser(bodyCheckpoint);
  AstNode* body = expression(cmp);
  comprehension->as.comprehension.body = body;

  gotoParser(afterConditions);

  for (int i = 0; i < scopesOpened; i++) {
    endScope(cmp);
  }

  return comprehension;
}

static bool isValidObjectKey(AstNode* key) {
  return key->type == AST_LITERAL ||
         (key->type == AST_VAR_GLOBAL && key->as.global.name != NULL);
}

static void ensureObjectKey(NodeCompiler* cmp, AstNode* key) {
  if (!isValidObjectKey(key)) {
    error(cmp, "Expect identifier or literal for object key.");
  }
}

static AstNode* leftBrace(NodeCompiler* cmp, bool canAssign) {
  if (check(TOKEN_RIGHT_BRACE)) {
    advance(cmp);
    return newObjectNode();
  }

  Parser bodyCheckpoint = saveParser();
  bool isComprehension = advanceTo(cmp, TOKEN_PIPE, TOKEN_RIGHT_BRACE, 1);
  gotoParser(bodyCheckpoint);

  if (isComprehension) {
    advanceTo(cmp, TOKEN_PIPE, TOKEN_RIGHT_BRACE, 1);
    consume(cmp, TOKEN_PIPE, "Expect '|' in comprehension.");
    AstNode* comprehension = parseComprehension(
        cmp, bodyCheckpoint, COMPREHENSION_SET, TOKEN_RIGHT_BRACE);
    consume(cmp, TOKEN_RIGHT_BRACE, "Expect '}' after comprehension.");
    return comprehension;
  }

  AstNode* first = expression(cmp);

  if (check(TOKEN_COLON)) {
    AstNode* obj = newObjectNode();
    ensureObjectKey(cmp, first);

    consume(cmp, TOKEN_COLON, "Expect ':' after object key.");
    AstNode* value = expression(cmp);
    pushAstVec(&obj->as.object.entries, newObjectEntryNode(first, value));

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
      if (!match(cmp, TOKEN_COMMA)) {
        errorAtCurrent(cmp, "Expect ',' or '}' after object value.");
        break;
      }
      if (check(TOKEN_RIGHT_BRACE)) break;

      AstNode* key = expression(cmp);
      ensureObjectKey(cmp, key);
      consume(cmp, TOKEN_COLON, "Expect ':' after object key.");
      AstNode* val = expression(cmp);
      pushAstVec(&obj->as.object.entries, newObjectEntryNode(key, val));
    }

    consume(cmp, TOKEN_RIGHT_BRACE, "Expect '}' after object literal.");
    return obj;
  }

  AstNode* set = newSetNode();
  pushAstVec(&set->as.set.values, first);

  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    if (!match(cmp, TOKEN_COMMA)) {
      errorAtCurrent(cmp, "Expect ',' or '}' after set element.");
      break;
    }
    if (check(TOKEN_RIGHT_BRACE)) break;
    pushAstVec(&set->as.set.values, expression(cmp));
  }

  consume(cmp, TOKEN_RIGHT_BRACE, "Expect '}' after set literal.");
  return set;
}

static AstNode* parenLeft(NodeCompiler* cmp, bool canAssign) {
  // empty sequence.
  if (match(cmp, TOKEN_COMMA)) {
    consume(cmp, TOKEN_PAREN_RIGHT, "Expect ')'.");
    return newSequenceNode();
  }

  // function?
  Parser checkpoint = saveParser();
  bool isFunction = peekFunction(cmp);
  gotoParser(checkpoint);
  if (isFunction) return function(cmp, parser.ppenult);

  Parser bodyCheckpoint = saveParser();
  bool isComprehension = advanceTo(cmp, TOKEN_PIPE, TOKEN_PAREN_RIGHT, 1);
  gotoParser(bodyCheckpoint);

  if (isComprehension) {
    advanceTo(cmp, TOKEN_PIPE, TOKEN_PAREN_RIGHT, 1);
    consume(cmp, TOKEN_PIPE, "Expect '|' in comprehension.");
    AstNode* comprehension = parseComprehension(
        cmp, bodyCheckpoint, COMPREHENSION_SEQUENCE, TOKEN_PAREN_RIGHT);
    consume(cmp, TOKEN_PAREN_RIGHT, "Expect ')' after comprehension.");
    return comprehension;
  }

  // sequence or parenthesized expression.
  AstNode* node = expression(cmp);

  if (check(TOKEN_COMMA)) {
    AstNode* seq = newSequenceNode();
    pushAstVec(&seq->as.sequence.values, node);
    do {
      advance(cmp);
      // allow a trailing comma.
      if (check(TOKEN_PAREN_RIGHT)) break;
      pushAstVec(&seq->as.sequence.values, expression(cmp));
    } while (check(TOKEN_COMMA));

    node = seq;
  }
  consume(cmp, TOKEN_PAREN_RIGHT, "Expect ')' after expression.");
  return node;
}

static ParseRule rules[] = {
    [TOKEN_IDENTIFIER] = {identifier, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_TYPE_VARIABLE] = {identifier, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {leftBrace, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_PAREN_LEFT] = {parenLeft, call, PREC_CALL, PREC_NONE},
    [TOKEN_PAREN_RIGHT] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_USER_INFIX] = {NULL, userInfix, PREC_NONE, PREC_NONE},
    [TOKEN_TRUE] = {boolean, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_FALSE] = {boolean, NULL, PREC_NONE, PREC_NONE},
};

#define PREC_STEP 1

static int sign(int x) { return (x > 0) - (x < 0); }

static void setPrecedence(NodeCompiler* cmp, ParseRule* rule, int prec) {
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
static ParseRule* getInfixRule(NodeCompiler* cmp, Token token) {
  if (token.type == TOKEN_IDENTIFIER) {
    Value name = tokenValue(token);
    Value prec;

    if (mapGet(&vm.infixes, name, &prec)) {
      setPrecedence(cmp, &rules[TOKEN_USER_INFIX], AS_NUMBER(prec));
      return &rules[TOKEN_USER_INFIX];
    }
  }

  return &rules[token.type];
}

static AstNode* parsePrecedence(NodeCompiler* cmp, Precedence precedence) {
  AstNode* node = newUnknownNode();

  advance(cmp);

  ParseFn prefixRule = rules[parser.previous.type].prefix;
  if (prefixRule == NULL) {
    error(cmp, "Expect expression.");
    return node;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  node = prefixRule(cmp, canAssign);

  ParseRule* infixRule;
  while (parser.current.type != TOKEN_PIPE &&
         precedence <=
             (infixRule = getInfixRule(cmp, parser.current))->leftPrec) {
    advance(cmp);
    node = infixRule->infix(cmp, canAssign, node, infixRule->rightPrec);
  }

  if (canAssign && match(cmp, TOKEN_EQUAL))
    error(cmp, "Invalid assignment target.");

  return node;
}

static AstNode* expression(NodeCompiler* cmp) {
  return parsePrecedence(cmp, PREC_ASSIGNMENT);
}

static AstNode* letDeclaration(NodeCompiler* cmp) {
  consumeIdentifier(cmp, "Expect variable name.");
  Token nameToken = parser.previous;

  addLocal(cmp, nameToken);

  AstNode* node = newUnknownNode();
  if (match(cmp, TOKEN_EQUAL)) {
    node = expression(cmp);
  } else {
    node = newLiteralValueNode(UNDEF_VAL);
    node->line = parser.previous.line;
  }

  markInitialized(cmp);

  node = newLetNode(node);
  node->as.let.name = tokenString(nameToken);
  return node;
}

static AstNode* ifStatement(NodeCompiler* cmp) {
  consume(cmp, TOKEN_PAREN_LEFT, "Expect '(' after 'if'.");
  AstNode* cond = expression(cmp);
  consume(cmp, TOKEN_PAREN_RIGHT, "Expect ')' after condition.");

  AstNode* then = statement(cmp);

  AstNode* elseBranch = NULL;
  if (match(cmp, TOKEN_ELSE)) {
    elseBranch = statement(cmp);
  }

  return newIfNode(cond, then, elseBranch);
}

static AstNode* whileStatement(NodeCompiler* cmp) {
  consume(cmp, TOKEN_PAREN_LEFT, "Expect '(' after 'while'.");
  AstNode* cond = expression(cmp);
  consume(cmp, TOKEN_PAREN_RIGHT, "Expect ')' after condition.");
  AstNode* body = statement(cmp);
  return newWhileNode(cond, body);
}

static void rescanCurrentAsPathIdentifier(NodeCompiler* cmp) {
  rewindScanner(parser.current);
  parser.current = scanPathIdentifier();
  parser.next = scanToken();
}

static AstNode* importStatement(NodeCompiler* cmp) {
  rescanCurrentAsPathIdentifier(cmp);
  consume(cmp, TOKEN_IDENTIFIER, "Expect identifier for module path.");

  Parser checkpoint = saveParser();
  if (cmp->fn->as.function.module == NULL) {
    error(cmp, "Can't import from fn that has no module.");
    return newUnknownNode();
  }

  AstNode* module = vmCompileModuleImportBody(
      cmp, cmp->fn->as.function.module->as.module.dirName->chars,
      parser.previous);
  gotoParser(checkpoint);

  AstNode* node = newUseNode(module);

  if (match(cmp, TOKEN_AS)) {
    consume(cmp, TOKEN_IDENTIFIER, "Expect identifier for alias.");
    node->as.use.alias = tokenString(parser.previous);
  }

  return node;
}

static AstNode* throwStatement(NodeCompiler* cmp) {
  AstNode* expr = expression(cmp);
  return newThrowNode(expr);
}

static AstNode* statement(NodeCompiler* cmp) {
  AstNode* node;
  if (match(cmp, TOKEN_IF)) {
    node = ifStatement(cmp);
  } else if (match(cmp, TOKEN_WHILE)) {
    node = whileStatement(cmp);
  } else if (match(cmp, TOKEN_USE)) {
    node = importStatement(cmp);
  } else if (match(cmp, TOKEN_THROW)) {
    node = throwStatement(cmp);
  } else if (match(cmp, TOKEN_LET)) {
    node = letDeclaration(cmp);
  } else if (match(cmp, TOKEN_LEFT_BRACE)) {
    node = block(cmp);
  } else {
    node = expression(cmp);
    node = newExprStmtNode(node);
  }

  return node;
}

static void statements(NodeCompiler* cmp, AstVec* target) {
  while (!match(cmp, TOKEN_EOF)) {
    AstNode* node = statement(cmp);
    pushAstVec(target, node);
  }
}

void compileModuleImportBody(NodeCompiler* cmp, AstNode* module) {
  Scanner sc = initScanner(module->as.module.source->chars);
  initParser(sc);
  statements(cmp, &module->as.module.stmts);
}

AstNode* compileFunctionNode(ObjString* name, char* source, AstNode* module) {
  Scanner sc = initScanner(source);
  initParser(sc);
  NodeCompiler cmp;
  AstNode* node = newFunctionNode(module);
  node->as.function.name = name;
  node->as.function.signature = newSignatureNode();
  node->as.function.body = newBlockNode();
  initNodeCompiler(&cmp, NULL, node);

  statements(&cmp, &node->as.function.body->as.block.stmts);
  AstNode* defaultReturn = newReturnNode(newLiteralValueNode(NIL_VAL));
  pushAstVec(&node->as.function.body->as.block.stmts, defaultReturn);
  return node;
}

void markNodeCompilerRoots(NodeCompiler* cmp) {
  while (cmp != NULL) {
    markAstNode(cmp->fn);
    cmp = cmp->enclosing;
  }
}