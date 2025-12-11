#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"

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

typedef void (*ParseFn)();

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
Chunk *compilingChunk;

static Chunk *currentChunk() { return compilingChunk; }

static void errorAt(Token *token, const char *message) {
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
static void error(const char *message) { errorAt(&parser.previous, message); }

// Error at current
static void errorAtCurrent(const char *message) {
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

static void consume(TokenType type, const char *message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}

static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
}

/* Takes:

@count - how many bytes to emit and;
@args - variadic args of 1 byte each
*/
static void emitBytes(uint8_t count, const uint8_t *bytes) {
  for (int i = 0; i < count; i++) {
    emitByte(bytes[i]);
  }
}

static void emitReturn() { emitByte(OP_RETURN); }

// static uint16_t makeConstant(Value value) {
//   uint16_t constant = addConstant(currentChunk(), value);
//   if (constant > UINT16_MAX) {
//     error("Too many constants in one chunk");
//     return 0;
//   }
//   return constant;
// }

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
static ParseRule *getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static void binary() {
  TokenType operatorType = parser.previous.type;
  ParseRule *rule = getRule(operatorType);
  // Keep winding the recursion
  parsePrecedence((Precedence)(rule->precedence + 1));

  switch (operatorType) {
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

  // Unreachable
  default:
    return;
  }
}

static void grouping() {
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number() {
  // String to double (parseFloat)
  double value = strtod(parser.previous.start, NULL);
  emitConstant(value);
}

static void unary() {
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
  [TOKEN_BANG]          = {NULL,      NULL,     PREC_NONE},
  [TOKEN_BANG_EQUAL]    = {NULL,      NULL,     PREC_NONE},
  [TOKEN_EQUAL]         = {NULL,      NULL,     PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL,      NULL,     PREC_NONE},
  [TOKEN_GREATER]       = {NULL,      NULL,     PREC_NONE},
  [TOKEN_GREATER_EQUAL] = {NULL,      NULL,     PREC_NONE},
  [TOKEN_LESS]          = {NULL,      NULL,     PREC_NONE},
  [TOKEN_LESS_EQUAL]    = {NULL,      NULL,     PREC_NONE},
  [TOKEN_IDENTIFIER]    = {NULL,      NULL,     PREC_NONE},
  [TOKEN_STRING]        = {NULL,      NULL,     PREC_NONE},
  [TOKEN_NUMBER]        = {number,    NULL,     PREC_NONE},
  [TOKEN_AND]           = {NULL,      NULL,     PREC_NONE},
  [TOKEN_CLASS]         = {NULL,      NULL,     PREC_NONE},
  [TOKEN_ELSE]          = {NULL,      NULL,     PREC_NONE},
  [TOKEN_FALSE]         = {NULL,      NULL,     PREC_NONE},
  [TOKEN_FOR]           = {NULL,      NULL,     PREC_NONE},
  [TOKEN_FUN]           = {NULL,      NULL,     PREC_NONE},
  [TOKEN_IF]            = {NULL,      NULL,     PREC_NONE},
  [TOKEN_NIL]           = {NULL,      NULL,     PREC_NONE},
  [TOKEN_OR]            = {NULL,      NULL,     PREC_NONE},
  [TOKEN_PRINT]         = {NULL,      NULL,     PREC_NONE},
  [TOKEN_RETURN]        = {NULL,      NULL,     PREC_NONE},
  [TOKEN_SUPER]         = {NULL,      NULL,     PREC_NONE},
  [TOKEN_THIS]          = {NULL,      NULL,     PREC_NONE},
  [TOKEN_TRUE]          = {NULL,      NULL,     PREC_NONE},
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

  prefixRule();

  // * If the precedence of the previous frame is higher then the prec of
  // * the current frame - we start unwinding the recursion,
  // * otherwise - keep opening stack frames
  while (precedence <= getRule(parser.current.type)->precedence) {
    // Move to the next token (the operator)
    advance();
    // Get the infix rule for the operator we just advanced past
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    // Call the infix rule to parse the right-hand side of the operator
    infixRule();
  }
}

static ParseRule *getRule(TokenType type) { return &rules[type]; }

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

bool compile(const char *source, Chunk *chunk) {
  initScanner(source);
  compilingChunk = chunk;

  parser.hadError = false;
  parser.panicMode = false;

  // The call to advance() “primes the pump” on the scanner.
  advance();
  expression();
  consume(TOKEN_EOF, "Expect end of expression");

  endCompiler();

  return !parser.hadError;
}