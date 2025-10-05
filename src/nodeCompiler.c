
#include <stdio.h>
#include <stdlib.h>

#include "node.h"
#include "parser.h"
#include "scanner.h"
#include "vm.h"

typedef struct NodeCompiler {
  struct NodeCompiler* enclosing;
  AstNode* node;
  int scopeDepth;
} NodeCompiler;

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

void initNodeCompiler(NodeCompiler* cmp, NodeCompiler* enclosing,
                      AstNode* node) {
  cmp->enclosing = NULL;
  cmp->enclosing = enclosing;
  cmp->scopeDepth = 0;
  cmp->node = NULL;
  cmp->node = node;
}

static AstNode* variable(NodeCompiler* cmp, bool canAssign) {
  Token name = parser.previous;
  ObjString* objName = tokenString(name);
  return newVariableNode(objName);
}

static AstNode* variableParameter(NodeCompiler* cmp) {
  advance();
  return variable(cmp, false);
}

static AstNode* parameter(NodeCompiler* cmp) {
  if (checkVariable()) return variableParameter(cmp);

  return NULL;
}

static AstNode* signature(NodeCompiler* cmp) {
  AstNode* node = newSignatureNode();

  if (!check(TOKEN_PAREN_RIGHT)) {
    do {
      AstNode* paramNode = parameter(cmp);
      pushAstVec(&node->as.signature.params, paramNode);
    } while (match(TOKEN_COMMA));
  }

  return node;
}

static AstNode* block(NodeCompiler* cmp) {
  AstNode* node = newBlockNode();

  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    AstNode* stmtNode = statement(cmp);
    pushAstVec(&node->as.block.stmts, stmtNode);
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");

  return node;
}

static AstNode* blockOrExpression(NodeCompiler* cmp) {
  AstNode* node = NULL;

  if (check(TOKEN_LEFT_BRACE)) {
    advance();
    node = block(cmp);
  } else {
    node = expression(cmp);
    node = newReturnNode(node);
  }

  return node;
}

static AstNode* function(NodeCompiler* enclosing) {
  AstNode* node = newClosureNode();
  NodeCompiler cmp;
  initNodeCompiler(&cmp, enclosing, node);

  consume(TOKEN_PAREN_LEFT, "Expect '(' after function name.");
  node->as.closure.signature = signature(&cmp);
  consume(TOKEN_PAREN_RIGHT, "Expect ')' after parameters.");
  consume(TOKEN_FAT_ARROW, "Expect '=>' after signature.");
  node->as.closure.body = blockOrExpression(&cmp);

  return node;
}

static AstNode* tryFunction(NodeCompiler* cmp) {
  Parser checkpoint = saveParser();
  SignatureType signatureType = peekSignatureType();
  gotoParser(checkpoint);

  switch (signatureType) {
    case SIG_NAKED:
      return NULL;
    case SIG_PAREN:
      return function(cmp);
    case SIG_NOT:
      return NULL;
  }
  return NULL;
}

static AstNode* number(NodeCompiler* cmp, bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  return newLiteralNode(NUMBER_VAL(value));
}

static void argumentList(NodeCompiler* cmp, AstVec* vec) {
  uint8_t argCount = 0;
  if (!check(TOKEN_PAREN_RIGHT)) {
    do {
      AstNode* node = NULL;

      if (match(TOKEN_DOUBLE_DOT))
        node = newSpreadNode(expression(cmp));
      else
        node = expression(cmp);

      pushAstVec(vec, node);

      if (argCount == 255) error("Can't have more than 255 arguments.");

      argCount++;
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_PAREN_RIGHT, "Expect ')' after arguments.");
}

static AstNode* userInfix(NodeCompiler* cmp, bool canAssign, AstNode* lhs,
                          Precedence prec) {
  AstNode* fn = variable(cmp, false);
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
      advance();
      pushAstVec(&node->as.sequence.values, expression(cmp));
    } while (check(TOKEN_COMMA));

    node = seq;
  }
  consume(TOKEN_PAREN_RIGHT, "Expect ')' after expression.");
  return node;
}

static ParseRule rules[] = {
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE, PREC_NONE},
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
      error("Unexpected precedence");
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

  advance();

  ParseFn prefixRule = rules[parser.previous.type].prefix;

  if (prefixRule == NULL) {
    error("Expect expression.");
    return node;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  node = prefixRule(cmp, canAssign);

  ParseRule* infixRule;
  while (precedence <=
         (infixRule = getInfixRule(cmp, parser.current))->leftPrec) {
    advance();
    node = infixRule->infix(cmp, canAssign, node, infixRule->rightPrec);
  }

  if (canAssign && match(TOKEN_EQUAL)) error("Invalid assignment target.");

  return node;
}

static AstNode* expression(NodeCompiler* cmp) {
  AstNode* node = tryFunction(cmp);

  if (node != NULL) return node;

  return parsePrecedence(cmp, PREC_ASSIGNMENT);
}

static AstNode* letDeclaration(NodeCompiler* cmp) {
  ObjString* name = parseVariable("Expect variable name.");
  AstNode* value = NULL;

  if (match(TOKEN_EQUAL)) {
    value = expression(cmp);
  } else {
    value = newLiteralNode(UNDEF_VAL);
  }

  return newLetNode(name, value);
}

static AstNode* statement(NodeCompiler* cmp) {
  AstNode* node;
  if (match(TOKEN_LET)) {
    node = letDeclaration(cmp);
  } else {
    node = expression(cmp);
  }

  consume(TOKEN_SEMICOLON, "Expect ';' after statement.");
  return node;
}

static void statements(NodeCompiler* cmp) {
  while (!match(TOKEN_EOF)) {
    AstNode* node = statement(cmp);
    pushAstVec(&cmp->node->as.module.stmts, node);
  }
}

AstNode* compileModuleNode(Token path, const char* source) {
  Scanner sc = initScanner(source);
  initParser(sc);

  ObjString* objName = tokenString(path);
  AstNode* node = newModuleNode(objName);

  NodeCompiler cmp;
  initNodeCompiler(&cmp, NULL, node);

  statements(&cmp);

  return cmp.node;
}