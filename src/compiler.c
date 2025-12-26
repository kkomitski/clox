#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"
#include "value.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
  Token current;
  Token previous;
  bool hadError;
  bool panicMode;
} Parser;

/*
The enum ordinal specifies the binding power of the precedence
*/
typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT, // =
  PREC_OR,         // or
  PREC_AND,        // and
  PREC_EQUALITY,   // == !=
  PREC_COMPARISON, // < > <= >=
  PREC_TERM,       // + -
  PREC_FACTOR,     // * /
  PREC_UNARY,      // ! -
  PREC_CALL,       // . ()
  PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
  /*
  Prefix rule is *NOT* the prefix operator, its the prefix or the expression
  An expression cannot start with a * or a +, it needs to start with a (, - or
  number. These can be terminals and non-terminals, because a unary prefix expr
  rule would have a single branch. (-5 for example will be - --- 5). They can
  also be terminals because all literals are handled by prefix rules and can
  start and complete an expression on their own.
  */
  ParseFn prefix;
  /*
  Infix is the same... its relates to the expression.
  Infix rules *CAN NEVER BE TERMINALS*
  */
  ParseFn infix;
  Precedence precedence;
} ParseRule;

Parser parser;
Chunk* compilingChunk;

static Chunk* currentChunk() { return compilingChunk; }

static void errorAt(Token* token, const char* message) {
  // supress any further errors
  if (parser.panicMode) return;
  parser.panicMode = true;

  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, "at end");
  } else if (token->type == TOKEN_ERROR) {
    // Nothing
  } else {
    // e.g., if token->start = "foobar", token->length = 3, prints: at 'foo'
    fprintf(stderr, "at '%.*s'", token->length, token->start);
  }
}

// Error at previous
static void error(const char* message) { errorAt(&parser.previous, message); }

// Error at current
static void errorAtCurrent(const char* message) {
  errorAt(&parser.current, message);
}

static void advance() {
  parser.previous = parser.current;

  for (;;) {
    parser.current = scanToken();

    if (parser.current.type != TOKEN_ERROR) break;

    errorAtCurrent(parser.current.start);
  }
}

static void consume(TokenType type, const char* message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}

static bool check(TokenType type) { return parser.current.type == type; }

/* Check against type and advance */
static bool match(TokenType type) {
  if (!check(type)) return false;

  advance();
  return true;
}

static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
}

/* Takes:

@count - how many bytes to emit and;
@args - variadic args of 1 byte each
*/
static void emitBytes(uint8_t count, const uint8_t* bytes) {
  for (int i = 0; i < count; i++) {
    emitByte(bytes[i]);
  }
}

static void emitReturn() { emitByte(OP_RETURN); }

static uint16_t makeConstant(Value value) {
  uint16_t constant = addConstant(currentChunk(), value);
  if (constant > UINT16_MAX) {
    error("Too many constants in one chunk");
    return 0;
  }
  return constant;
}

static void emitConstant(Value value) {
  // uint16_t constant = makeConstant(value);
  uint16_t constant = addConstant(currentChunk(), value);

  if (constant <= UINT8_MAX) {
    emitBytes(2, (uint8_t[]){OP_CONSTANT, (uint8_t)constant});
  } else if (constant <= UINT16_MAX) {
    // Emit OP_CONSTANT_LONG and split the index into two bytes (big-endian)
    emitBytes(3, (uint8_t[]){OP_CONSTANT_LONG, (constant >> 8) & 0xff,
                             constant & 0xff});
  } else {
    error("Too many constants in one chunk");
  }
}

static void endCompiler() {
#ifdef DEBUG_PRINT_CODE
  if (!parser.hadError) { disassembleChunk(currentChunk(), "code"); }
#endif
  emitReturn();
}

/*
Forward declaration allows for the functions to be declared even tho the full
bodies are further down, this is okay because the function that make these calls
are defined before these are declared
*/
static void expression();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static void binary(bool canAssign) {
  TokenType operatorType = parser.previous.type;
  ParseRule* rule = getRule(operatorType);
  // Keep winding the recursion
  parsePrecedence((Precedence)(rule->precedence + 1));

  switch (operatorType) {
    // clang-format off
  case TOKEN_BANG_EQUAL: emitBytes(2, (uint8_t[]){OP_EQUAL, OP_NOT}); break;
  case TOKEN_EQUAL_EQUAL: emitByte(OP_EQUAL); break;
  case TOKEN_GREATER: emitByte(OP_GREATER); break;
  case TOKEN_GREATER_EQUAL: emitBytes(2, (uint8_t[]){OP_LESS, OP_NOT}); break;
  case TOKEN_LESS: emitByte(OP_LESS); break;
  case TOKEN_LESS_EQUAL: emitBytes(2, (uint8_t[]){OP_GREATER, OP_NOT}); break;
  case TOKEN_BANG: emitByte(OP_NOT); break;
  case TOKEN_PLUS: emitByte(OP_ADD); break;
  case TOKEN_MINUS: emitByte(OP_SUBTRACT); break;
  case TOKEN_STAR: emitByte(OP_MULTIPLY); break;
  case TOKEN_SLASH: emitByte(OP_DIVIDE); break;
  // clang-format on
  // Unreachable
  default:
    errorAt(&parser.previous, "Token not yet processable.");
    break;
  }
}

/* This function just emits bytes as is */
static void literal(bool canAssign) {
  switch (parser.previous.type) {
    // clang-format off
  case TOKEN_FALSE: emitByte(OP_FALSE); break;
  case TOKEN_NIL: emitByte(OP_NIL); break;
  case TOKEN_TRUE: emitByte(OP_TRUE); break;
  default: return;
  // clang-format off
  }
}

/* Calls `expression()` and consumes the closing ')' paren */
static void grouping(bool canAssign) {
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

/* Emits a constant of the string as float and continues */
static void number(bool canAssign) {
  // String to double (parseFloat)
  double value = strtod(parser.previous.start, NULL);
  emitConstant(NUMBER_VAL(value));
}

/* Emits the string as constant */
static void string(bool canAssign) {
  // Takes the strings characters directly from the source code/lexeme, the + 1 and -2 trims the quotes
  emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

static uint8_t identifierConstant(Token* name) {
  return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static void namedVariable(Token name, bool canAssign) {
  uint8_t arg = identifierConstant(&name);

  if(canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitBytes(2, (uint8_t[]){OP_SET_GLOBAL, arg});
  } else {
    emitBytes(2, (uint8_t[]){OP_GET_GLOBAL, arg});
  }
}

static void variable(bool canAssign) {
  namedVariable(parser.previous, canAssign);
}

static void unary(bool canAssign) {
  TokenType operatorType = parser.previous.type;

  // Compile the operand
  parsePrecedence(PREC_UNARY);

  switch (operatorType) {
  case TOKEN_MINUS:
    emitByte(OP_NEGATE);
    break;
  default:
    return; // Unreachable
  }
}

// clang-format off
ParseRule rules[] = {
  /*   Token Type       |  prefix  |  infix  |  precedence */
  [TOKEN_LEFT_PAREN]    = {grouping,  NULL,     PREC_NONE},
  [TOKEN_RIGHT_PAREN]   = {NULL,      NULL,     PREC_NONE},
  [TOKEN_LEFT_BRACE]    = {NULL,      NULL,     PREC_NONE}, 
  [TOKEN_RIGHT_BRACE]   = {NULL,      NULL,     PREC_NONE},
  [TOKEN_COMMA]         = {NULL,      NULL,     PREC_NONE},
  [TOKEN_DOT]           = {NULL,      NULL,     PREC_NONE},
  [TOKEN_MINUS]         = {unary,     binary,   PREC_TERM},
  [TOKEN_PLUS]          = {NULL,      binary,   PREC_TERM},
  [TOKEN_SEMICOLON]     = {NULL,      NULL,     PREC_NONE},
  [TOKEN_SLASH]         = {NULL,      binary,   PREC_FACTOR},
  [TOKEN_STAR]          = {NULL,      binary,   PREC_FACTOR},
  [TOKEN_BANG]          = {unary,     NULL,     PREC_NONE},
  [TOKEN_BANG_EQUAL]    = {NULL,      binary,   PREC_NONE},
  [TOKEN_EQUAL]         = {NULL,      NULL,     PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL,      binary,   PREC_NONE},
  [TOKEN_GREATER]       = {NULL,      binary,   PREC_NONE},
  [TOKEN_GREATER_EQUAL] = {NULL,      binary,   PREC_NONE},
  [TOKEN_LESS]          = {NULL,      binary,   PREC_NONE},
  [TOKEN_LESS_EQUAL]    = {variable,  binary,   PREC_NONE},
  [TOKEN_IDENTIFIER]    = {NULL,      NULL,     PREC_NONE},
  [TOKEN_STRING]        = {string,    NULL,     PREC_NONE},
  [TOKEN_NUMBER]        = {number,    NULL,     PREC_NONE},
  [TOKEN_AND]           = {NULL,      NULL,     PREC_NONE},
  [TOKEN_CLASS]         = {NULL,      NULL,     PREC_NONE},
  [TOKEN_ELSE]          = {NULL,      NULL,     PREC_NONE},
  [TOKEN_FALSE]         = {literal,   NULL,     PREC_NONE},
  [TOKEN_FOR]           = {NULL,      NULL,     PREC_NONE},
  [TOKEN_FUN]           = {NULL,      NULL,     PREC_NONE},
  [TOKEN_IF]            = {NULL,      NULL,     PREC_NONE},
  [TOKEN_NIL]           = {literal,   NULL,     PREC_NONE},
  [TOKEN_OR]            = {NULL,      NULL,     PREC_NONE},
  [TOKEN_PRINT]         = {NULL,      NULL,     PREC_NONE},
  [TOKEN_RETURN]        = {NULL,      NULL,     PREC_NONE},
  [TOKEN_SUPER]         = {NULL,      NULL,     PREC_NONE},
  [TOKEN_THIS]          = {NULL,      NULL,     PREC_NONE},
  [TOKEN_TRUE]          = {literal,   NULL,     PREC_NONE},
  [TOKEN_VAR]           = {NULL,      NULL,     PREC_NONE},
  [TOKEN_WHILE]         = {NULL,      NULL,     PREC_NONE},
  [TOKEN_ERROR]         = {NULL,      NULL,     PREC_NONE},
  [TOKEN_EOF]           = {NULL,      NULL,     PREC_NONE},
};
// clang-format on

static void parsePrecedence(Precedence precedence) {
  advance();

  ParseFn prefixRule = getRule(parser.previous.type)->prefix;

  /*
  Each expr needs to start withe either a TOKEN_NUMBER, a TOKEN_MINUS, or a
  TOKEN_LEFT_PAREN (3, - or '(').
  Expressions cannot start with *, + or &&
  */
  if (prefixRule == NULL) {
    error("Expect expression.");
    return;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(canAssign);

  // * If the precedence of the previous frame is higher then the prec of
  // * the current frame - we start unwinding the recursion,
  // * otherwise - keep opening stack frames
  while (precedence <= getRule(parser.current.type)->precedence) {
    // Move to the next token (the operator)
    advance();
    // Get the infix rule for the operator we just advanced past
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    // Call the infix rule to parse the right-hand side of the operator
    infixRule(canAssign);
  }

  if(canAssign && match(TOKEN_EQUAL)) {
    error("Invalid assignment target.");
  }
}

static uint8_t parseVariable(const char* errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);
  return identifierConstant(&parser.previous);
}

static void defineVariable(uint8_t global) {
  emitBytes(2, (uint8_t[]){OP_DEFINE_GLOBAL, global});
}

static ParseRule* getRule(TokenType type) { return &rules[type]; }

/*
We map each token type to a different kind of expression.
We define a function for each expression that outputs the appropriate bytecode.
Then we build an array of function pointers.
The indexes in the array correspond to the TokenType enum values, and the
function at each index is the code to compile an expression of that token type.
*/

static void expression() {
  // Each expr call starts at the lowest level of prec
  parsePrecedence(PREC_ASSIGNMENT);
}

static void varDeclaration() {
  uint8_t global = parseVariable("Expect variable name.");

  if (match(TOKEN_EQUAL)) {
    expression();
  } else {
    emitByte(OP_NIL);
  }

  consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration");
  defineVariable(global);
}

static void expressionStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
}

static void printStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after statement value.");
  emitByte(OP_PRINT);
}

/* Keep advancing until we hit a new statement or until the current statement
 * completes (ie - semicolon) */
static void synchronize() {
  parser.panicMode = false;

  while (parser.current.type != TOKEN_EOF) {
    if (parser.previous.type == TOKEN_SEMICOLON) return;

    switch (parser.current.type) {
    case TOKEN_CLASS:
    case TOKEN_FUN:
    case TOKEN_VAR:
    case TOKEN_FOR:
    case TOKEN_IF:
    case TOKEN_WHILE:
    case TOKEN_PRINT:
    case TOKEN_RETURN:
      return;
    default:;
    }

    advance();
  }
}

/* Statements leave the stack where it was after completion */
static void statement();
static void declaration();

static void declaration() {
  if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    statement();
  }
  if (parser.panicMode) synchronize();
};

static void statement() {
  if (match(TOKEN_PRINT)) {
    printStatement();
  } else {
    expressionStatement();
  }
}

bool compile(const char* source, Chunk* chunk) {
  initScanner(source);
  compilingChunk = chunk;

  parser.hadError = false;
  parser.panicMode = false;

  // The call to advance() “primes the pump” on the scanner.
  advance();
  while (!match(TOKEN_EOF)) {
    declaration();
  }
  // expression();
  // consume(TOKEN_EOF, "Expect end of expression");

  endCompiler();

  return !parser.hadError;
}