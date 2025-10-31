
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

  fprintf(stderr, "Error in %s:%d", cmp->node->as.function.name->chars,
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
}

void checkError(NodeCompiler* cmp) {
  if (parser.current.type == TOKEN_ERROR)
    errorAtCurrent(cmp, parser.current.start);
}

void shiftParser() {
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

SignatureType peekSignatureType(NodeCompiler* cmp) {
  if (match(cmp, TOKEN_PAREN_LEFT)) {
    if (!check(TOKEN_PAREN_RIGHT)) {
      do {
        if (!matchParamOrPattern(cmp)) return SIG_NOT;
        if (check(TOKEN_COLON)) {
          advanceTo(cmp, TOKEN_COMMA, TOKEN_PAREN_RIGHT, 1);
        }

      } while (match(cmp, TOKEN_COMMA));
    }

    if (!match(cmp, TOKEN_PAREN_RIGHT)) return SIG_NOT;
    if (!match(cmp, TOKEN_FAT_ARROW)) return SIG_NOT;

    return SIG_PAREN;
  } else {
    // can only be naked.
    if (matchParamOrPattern(cmp)) return peekSignatureType(cmp);
    if (match(cmp, TOKEN_FAT_ARROW)) return SIG_NAKED;
  }

  return SIG_NOT;
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
  cmp->node = NULL;
  cmp->node = node;
  cmp->scopeDepth = 0;
}

static bool identifiersEqual(Token* a, Token* b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(NodeCompiler* cmp, Token* name) {
  for (int i = cmp->node->as.function.localCount - 1; i >= 0; i--) {
    Local* local = &cmp->node->as.function.locals[i];

    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error(cmp, "Can't read local variable in its own initializer.");
      }
      return i;
    }
  }

  return -1;
}

static int addUpvalue(NodeCompiler* cmp, uint8_t index, bool isLocal) {
  int upvalueCount = cmp->node->as.function.upvalueCount;

  for (int i = 0; i < upvalueCount; i++) {
    Upvalue* upvalue = &cmp->node->as.function.upvalues[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;
    }
  }

  if (upvalueCount == UINT8_COUNT) {
    error(cmp, "Too many closure variables in function.");
    return 0;
  }

  cmp->node->as.function.upvalues[upvalueCount].isLocal = isLocal;
  cmp->node->as.function.upvalues[upvalueCount].index = index;
  return cmp->node->as.function.upvalueCount++;
}

static int resolveUpvalue(NodeCompiler* cmp, Token* name) {
  if (cmp->enclosing == NULL) return -1;

  int local = resolveLocal(cmp->enclosing, name);
  if (local != -1) {
    cmp->enclosing->node->as.function.locals[local].isCaptured = true;
    return addUpvalue(cmp, (uint8_t)local, true);
  }

  int upvalue = resolveUpvalue(cmp->enclosing, name);
  if (upvalue != -1) {
    return addUpvalue(cmp, (uint8_t)upvalue, false);
  }

  return -1;
}

static uint8_t addLocal(NodeCompiler* cmp, Token name) {
  if (cmp->node->as.function.localCount == UINT8_COUNT) {
    error(cmp, "Too many local variables in function.");
    return 0;
  }

  Local* local =
      &cmp->node->as.function.locals[cmp->node->as.function.localCount++];

  local->name = name;
  local->depth = -1;
  local->isCaptured = false;

  return cmp->node->as.function.localCount - 1;
}

static uint8_t declareLocal(NodeCompiler* cmp, Token* name) {
  for (int i = cmp->node->as.function.localCount - 1; i >= 0; i--) {
    Local* local = &cmp->node->as.function.locals[i];
    if (local->depth != -1 && local->depth < cmp->scopeDepth) {
      break;
    }

    if (identifiersEqual(name, &local->name)) {
      error(cmp, "Already a variable with this name in this scope.");
    }
  }

  return addLocal(cmp, *name);
}

static void markInitialized(NodeCompiler* cmp) {
  if (cmp->scopeDepth == 0) return;

  cmp->node->as.function.locals[cmp->node->as.function.localCount - 1].depth =
      cmp->scopeDepth;
}

static AstNode* identifier(NodeCompiler* cmp, bool canAssign) {
  if (check(TOKEN_FAT_ARROW)) return nakedFunction(cmp, parser.previous);

  Token name = parser.previous;
  ObjString* objName = tokenString(name);

  int local = resolveLocal(cmp, &name);
  if (local >= 0) {
    return newVarLocalNode((uint8_t)local, objName);
  }

  int upvalue = resolveUpvalue(cmp, &name);
  if (upvalue >= 0) {
    return newVarUpvalueNode((uint8_t)upvalue, objName);
  }

  return newVarGlobalNode(objName);
}

static AstNode* parameter(NodeCompiler* cmp) {
  if (!checkVariable()) {
    errorAtCurrent(cmp, "Expecting parameter name.");
    return NULL;
  }

  return newParamNode(tokenString(parser.previous), NULL);
}

static AstNode* signature(NodeCompiler* cmp) {
  AstNode* node = newSignatureNode();

  if (!check(TOKEN_PAREN_RIGHT)) {
    do {
      AstNode* paramNode = parameter(cmp);
      pushAstVec(&node->as.signature.params, paramNode);
    } while (match(cmp, TOKEN_COMMA));
  }

  return node;
}

static AstNode* block(NodeCompiler* cmp) {
  AstNode* node = newBlockNode();

  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    AstNode* stmtNode = statement(cmp);
    pushAstVec(&node->as.block.stmts, stmtNode);
  }

  consume(cmp, TOKEN_RIGHT_BRACE, "Expect '}' after block.");

  return node;
}

static AstNode* blockOrExpression(NodeCompiler* cmp) {
  AstNode* node = NULL;

  if (check(TOKEN_LEFT_BRACE)) {
    advance(cmp);
    node = block(cmp);
  } else {
    node = expression(cmp);
    node = newReturnNode(node);
  }

  return node;
}

static AstNode* nakedFunction(NodeCompiler* enclosing, Token name) {
  AstNode* node = newFunctionNode(tokenString(name));
  NodeCompiler cmp;
  initNodeCompiler(&cmp, enclosing, node);

  AstNode* sigNode = newSignatureNode();
  AstNode* paramNode = newParamNode(tokenString(parser.previous), NULL);
  pushAstVec(&sigNode->as.signature.params, paramNode);
  node->as.function.signature = sigNode;

  consume(&cmp, TOKEN_FAT_ARROW, "Expect '=>' after signature.");
  node->as.function.body = blockOrExpression(&cmp);

  return node;
}

static AstNode* function(NodeCompiler* enclosing, Token name) {
  AstNode* node = newFunctionNode(tokenString(name));
  NodeCompiler cmp;
  initNodeCompiler(&cmp, enclosing, node);

  consume(&cmp, TOKEN_PAREN_LEFT, "Expect '(' after function name.");
  node->as.function.signature = signature(&cmp);
  consume(&cmp, TOKEN_PAREN_RIGHT, "Expect ')' after parameters.");
  consume(&cmp, TOKEN_FAT_ARROW, "Expect '=>' after signature.");
  node->as.function.body = blockOrExpression(&cmp);

  return node;
}

AstNode* tryFunction(NodeCompiler* cmp, Token name) {
  Parser checkpoint = saveParser();
  SignatureType signatureType = peekSignatureType(cmp);
  gotoParser(checkpoint);

  switch (signatureType) {
    case SIG_NAKED:
      return NULL;
    case SIG_PAREN:
      return function(cmp, name);
    case SIG_NOT:
      return NULL;
  }
  return NULL;
}

static AstNode* number(NodeCompiler* cmp, bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  AstNode* node = newLiteralNode(NUMBER_VAL(value));
  node->line = parser.previous.line;
  return node;
}

static void argumentList(NodeCompiler* cmp, AstVec* vec) {
  uint8_t argCount = 0;
  if (!check(TOKEN_PAREN_RIGHT)) {
    do {
      AstNode* node = NULL;

      if (match(cmp, TOKEN_DOUBLE_DOT))
        node = newSpreadNode(expression(cmp));
      else
        node = expression(cmp);

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

static AstNode* parentheses(NodeCompiler* cmp, bool canAssign) {
  AstNode* node = expression(cmp);

  if (check(TOKEN_COMMA)) {
    AstNode* seq = newSequenceNode();
    pushAstVec(&seq->as.sequence.values, node);
    do {
      advance(cmp);
      pushAstVec(&node->as.sequence.values, expression(cmp));
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
    [TOKEN_PAREN_LEFT] = {parentheses, call, PREC_CALL, PREC_NONE},
    [TOKEN_PAREN_RIGHT] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE, PREC_NONE},
    [TOKEN_USER_INFIX] = {NULL, userInfix, PREC_NONE, PREC_NONE},
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

    if (mapGet(&vm.infixes, name, &prec) ||
        mapGet(&vm.methodInfixes, name, &prec)) {
      setPrecedence(cmp, &rules[TOKEN_USER_INFIX], AS_NUMBER(prec));
      return &rules[TOKEN_USER_INFIX];
    }
  }

  return &rules[token.type];
}

static AstNode* parsePrecedence(NodeCompiler* cmp, Precedence precedence) {
  AstNode* node = NULL;

  advance(cmp);

  ParseFn prefixRule = rules[parser.previous.type].prefix;
  if (prefixRule == NULL) {
    error(cmp, "Expect expression.");
    return node;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  node = prefixRule(cmp, canAssign);

  ParseRule* infixRule;
  while (precedence <=
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

  declareLocal(cmp, &nameToken);

  AstNode* node = NULL;
  if (match(cmp, TOKEN_EQUAL)) {
    node = expression(cmp);
  } else {
    node = newLiteralNode(UNDEF_VAL);
    node->line = parser.previous.line;
  }

  markInitialized(cmp);

  ObjString* name = tokenString(nameToken);
  return newLetNode(name, node);
}

static AstNode* statement(NodeCompiler* cmp) {
  AstNode* node;
  if (match(cmp, TOKEN_LET)) {
    node = letDeclaration(cmp);
  } else {
    node = expression(cmp);
    node = newExprStmtNode(node);
  }

  consume(cmp, TOKEN_SEMICOLON, "Expect ';' after statement.");
  return node;
}

static void statements(NodeCompiler* cmp) {
  while (!match(cmp, TOKEN_EOF)) {
    AstNode* node = statement(cmp);
    pushAstVec(&cmp->node->as.function.body->as.block.stmts, node);
  }
}

AstNode* compileModuleNode(Token path, const char* source) {
  Scanner sc = initScanner(source);
  initParser(sc);

  ObjString* objName = tokenString(path);
  NodeCompiler cmp;
  AstNode* node = newFunctionNode(objName);
  node->as.function.body = newBlockNode();
  initNodeCompiler(&cmp, NULL, node);

  statements(&cmp);

  return newModuleNode(objName, node);
}

void markNodeCompilerRoots(NodeCompiler* cmp) {
  printf("\n");
  while (cmp != NULL) {
    markAstNode(cmp->node);
    cmp = cmp->enclosing;
  }
}