#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "object.h"
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
  TYPE_FUNCTION, // Any other function
  TYPE_SCRIPT    // The outer implicit function wrapping the global
} FunctionType;
typedef struct Compiler {
  struct Compiler* enclosing;

  ObjFunction* function;
  FunctionType type;

  int localCount;
  Upvalue upvalues[UINT8_COUNT];
  int scopeDepth;
  Local locals[UINT8_COUNT]; // fixed array of 255 locals
} Compiler;

Parser parser;
Compiler* current = NULL;

static Chunk* currentChunk() { return &current->function->chunk; }

// ============================================================================
// PARSER_UTILITIES
// ============================================================================
static void PARSER_UTILITIES() {}

static void errorAt(Token* token, const char* message) {
  if (parser.panicMode) return;
  parser.panicMode = true;

  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
    // Nothing
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  fflush(stderr); // Force output
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

static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

static void emitLoop(int loopStart) {
  emitByte(OP_LOOP);

  int offset = currentChunk()->count - loopStart + 2;
  if (offset > UINT16_MAX) error("Loop body too large");

  emitByte((offset >> 8) & 0xff);
  emitByte(offset & 0xff);
}

/* Emit an instruction and two bytes, return the index of the first offset byte
 * (to be used to find and overwrite it after the 'then' is parsed) */
static int emitJump(uint8_t instruction) {
  // 2 byte/16 bit offset will allow us to jump 65,535 bytes of code
  emitByte(instruction);
  emitByte(0xff);
  emitByte(0xff);

  return currentChunk()->count - 2; // Index of the empty bytes
}

static void emitReturn() {
  emitByte(OP_NIL);
  emitByte(OP_RETURN);
}

/* Adds the value into the chunks pool of constants
@returns index of the constant
*/
static uint16_t makeConstant(Value value) {
  uint16_t constant = addConstant(currentChunk(), value);
  if (constant > UINT16_MAX) {
    error("Too many constants in one chunk");
    return 0;
  }
  return constant;
}

static void emitConstant(Value value) {
  emitBytes(OP_CONSTANT, makeConstant(value));
}

/* Patch a bytecode at a give offset by computing the jump distance */
static void patchJump(int offset) {
  // -2 to adjust for the bytecode for the jump offset itself
  int jump = currentChunk()->count - offset - 2;

  if (jump > UINT16_MAX) { error("Too much code to jump over."); }

  currentChunk()->code[offset] = (jump >> 8) & 0xff;
  currentChunk()->code[offset + 1] = jump & 0xff;
}

static void initCompiler(Compiler* compiler, FunctionType type) {
  compiler->enclosing = current;
  compiler->function = NULL;
  compiler->type = type;
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  compiler->function = newFunction();
  current = compiler;

  // Set the name of the function to the previous token
  if (type != TYPE_SCRIPT) {
    current->function->name =
        copyString(parser.previous.start, parser.previous.length);
  }

  Local* local = &current->locals[current->localCount++];
  local->depth = 0;
  local->isCaptured = false;
  local->name.start = "";
  local->name.length = 0;
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

// ============================================================================
// EXPRESSION_PARSING
// ============================================================================
static void EXPRESSION_PARSING() {}

/*
Forward declaration allows for the functions to be declared even tho the full
bodies are further down, this is okay because the function that make these calls
are defined before these are declared
*/
static void expression();
static void and_(bool canAssign);
static void or_(bool canAssign);
static uint8_t argumentList();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static void binary(bool canAssign) {
  TokenType operatorType = parser.previous.type;
  ParseRule* rule = getRule(operatorType);
  // Keep winding the recursion
  parsePrecedence((Precedence)(rule->precedence + 1));

  switch (operatorType) {
    // clang-format off
  case TOKEN_BANG_EQUAL: emitBytes(OP_EQUAL, OP_NOT); break;
  case TOKEN_EQUAL_EQUAL: emitByte(OP_EQUAL); break;
  case TOKEN_GREATER: emitByte(OP_GREATER); break;
  case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
  case TOKEN_LESS: emitByte(OP_LESS); break;
  case TOKEN_LESS_EQUAL: emitBytes(OP_GREATER, OP_NOT); break;
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

static void call(bool canAssign) {
  uint8_t argCount = argumentList();
  emitBytes(OP_CALL, argCount);
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

static void or_(bool canAssign) {
  int elseJump = emitJump(OP_JUMP_IF_FALSE);
  int endJump = emitJump(OP_JUMP);

  patchJump(elseJump);
  emitByte(OP_POP);

  parsePrecedence(PREC_OR);
  patchJump(endJump);
}

/* Emits the string as constant */
static void string(bool canAssign) {
  // Takes the strings characters directly from the source code/lexeme, the + 1 and -2 trims the quotes
  emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

/* Creates a new copy the string from the source, wraps it in a value, loads it into the chunks constants and passes back the index */
static uint16_t identifierConstant(Token* name) {
  // Copy the identifier string from the token
  ObjString* identifier = copyString(name->start, name->length);
  // Wrap it as a value
  Value value = OBJ_VAL(identifier);
  // Add it to the constant table and get its index
  uint16_t constant = makeConstant(value);
  return constant;
}

static bool identifiersEqual(Token* a, Token* b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

/* Walks backwards in the compilers local list and checks for one with the same name and returns its index */
static int resolveLocal(Compiler* compiler, Token* name) {
  for(int i = compiler->localCount -1; i >= 0; i--) {
    Local* local = &compiler->locals[i];
    if(identifiersEqual(name, &local->name)) {
      if(local->depth == -1) {
        error("Can't read local variable in its own initializer");
      }
      return i;
    }
  }
  
  return -1;
}

/* Returns the index of the upvalue in the functions list of upvalues */
static int addUpValue(Compiler* compiler, uint8_t index, bool isLocal) {
  int upvalueCount = compiler->function->upvalueCount;

  for(int i = 0; i < upvalueCount; i++) {
    Upvalue* upvalue = &compiler->upvalues[i];
    if(upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;
    }
  }

  if(upvalueCount == UINT8_COUNT) {
    error("Too many closure variables in function.");
    return 0;
  }

  compiler->upvalues[upvalueCount].isLocal = isLocal;
  compiler->upvalues[upvalueCount].index = index;
  return compiler->function->upvalueCount++;
}

/* Try to find the value anywhere in the locals, if not found return -1 */
static int resolveUpvalue(Compiler* compiler, Token* name) {
  if(compiler->enclosing == NULL) return -1;

  int local = resolveLocal(compiler->enclosing, name);
  if(local != -1) {
    compiler->enclosing->locals[local].isCaptured = true;
    return addUpValue(compiler, (uint8_t)local, true);
  }

  // Recursively traverse up the scope tree to see if any of the wrapping compilers have the value
  int upvalue = resolveUpvalue(compiler->enclosing, name);
  if(upvalue != -1) {
    return addUpValue(compiler, (uint8_t)upvalue, false);
  }
  
  return -1;
}

static void addLocal(Token name) {
  if(current->localCount == UINT8_COUNT) {
    error("Too many local variables in function.");
    return;
  }

  Local* local = &current->locals[current->localCount++];
  local->name = name;
  // local->depth = current->scopeDepth;
  local->depth = -1;
  local->isCaptured = false;
}

/*
  Checks for duplicate variable names in the current scope and adds a new local variable.
  Only runs for local (non-global) variables.
*/
static void declareVariable() {
  // If we're in the global scope, no need to declare a local variable.
  // if (current->scopeDepth == 0) return;

  // The name of the variable being declared.
  Token* name = &parser.previous;
  int localCount = current->localCount;
  int scopeDepth = current->scopeDepth;

  // Walk backwards through the list of locals to check for duplicates in the same scope.
  for (int i = localCount - 1; i >= 0; i--) {
    Local* local = &current->locals[i];
    int localDepth = local->depth;

    // Stop if we've reached a variable from an outer scope.
    if (localDepth != -1 && localDepth < scopeDepth) {
      break;
    }

    // If a variable with the same name exists in this scope, throw an error.
    if (identifiersEqual(name, &local->name)) {
      error("Already a variable with same name in this scope.");
    }
  }

  // Add the new local variable to the list.
  addLocal(*name);
}

static void namedVariable(Token name, bool canAssign) {
  uint8_t getOp, setOp;
  int arg = resolveLocal(current, &name);
  
  if(arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else if((arg = resolveUpvalue(current, &name)) != -1) {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  } else {
    arg = identifierConstant(&name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  if(canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitBytes(setOp, (uint8_t)arg);
  } else {
    emitBytes(getOp, (uint8_t)arg);
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
  [TOKEN_LEFT_PAREN]    = {grouping,  call,     PREC_CALL},
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
  [TOKEN_BANG_EQUAL]    = {NULL,      binary,   PREC_EQUALITY},
  [TOKEN_EQUAL]         = {NULL,      NULL,     PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL,      binary,   PREC_EQUALITY},
  [TOKEN_GREATER]       = {NULL,      binary,   PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] = {NULL,      binary,   PREC_COMPARISON},
  [TOKEN_LESS]          = {NULL,      binary,   PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]    = {NULL,      binary,   PREC_COMPARISON},
  [TOKEN_IDENTIFIER]    = {variable,  NULL,     PREC_NONE},
  [TOKEN_STRING]        = {string,    NULL,     PREC_NONE},
  [TOKEN_NUMBER]        = {number,    NULL,     PREC_NONE},
  [TOKEN_AND]           = {NULL,      and_,     PREC_NONE},
  [TOKEN_CLASS]         = {NULL,      NULL,     PREC_NONE},
  [TOKEN_ELSE]          = {NULL,      NULL,     PREC_NONE},
  [TOKEN_FALSE]         = {literal,   NULL,     PREC_NONE},
  [TOKEN_FOR]           = {NULL,      NULL,     PREC_NONE},
  [TOKEN_FUN]           = {NULL,      NULL,     PREC_NONE},
  [TOKEN_IF]            = {NULL,      NULL,     PREC_NONE},
  [TOKEN_NIL]           = {literal,   NULL,     PREC_NONE},
  [TOKEN_OR]            = {NULL,      or_,      PREC_NONE},
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

  if (canAssign && match(TOKEN_EQUAL)) { error("Invalid assignment target."); }
}

/* Parse out the variable identifier
  *`GLOBAL` - add the variable identifier to the constants table and get its
  * index
  *`LOCAL`  - declare the variable inside the locals, the index handle will be
  grabbed from the locals stack mirror
  * when lookup is required

  @returns
  *`GLOBAL` - the index of the variable inside the constants
  *`LOCAL`  - 0
*/
static uint16_t parseVariable(const char* errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);

  bool isGlobal = current->scopeDepth == 0;
  if (isGlobal) return identifierConstant(&parser.previous);

  declareVariable();
  return 0;
}

/* Flips the depth of the variable from -1 to the current scope */
static void markInitialized() {
  if (current->scopeDepth == 0) return;
  current->locals[current->localCount - 1].depth = current->scopeDepth;
}

/*
`local` - flip the depth from -1 to the current depth
`global` - emit as bytecode and add to the chunks constants
 */
static void defineVariable(uint8_t global) {
  if (current->scopeDepth > 0) {
    markInitialized();
    return;
  }

  emitBytes(OP_DEFINE_GLOBAL, global);
}

static uint8_t argumentList() {
  uint8_t argCount = 0;

  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      expression();
      if (argCount == 255) { error("Can't have more than 255 arguments."); }
      argCount++;
    } while (match(TOKEN_COMMA));
  }

  consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
  return argCount;
}

static void and_(bool canAssign) {
  int endJump = emitJump(OP_JUMP_IF_FALSE);

  emitByte(OP_POP);
  parsePrecedence(PREC_AND);

  patchJump(endJump);
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

// ============================================================================
// STATEMENT_PARSING
// ============================================================================
static void STATEMENT_PARSING() {}

static void declaration();
static void statement();

static void block() {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(FunctionType type) {
  Compiler compiler;
  initCompiler(&compiler, type);
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
  consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
  block();

  ObjFunction* function = endCompiler();
  emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

  for (int i = 0; i < function->upvalueCount; i++) {
    emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
    emitByte(compiler.upvalues[i].index);
  }
}

static void funDeclaration() {
  uint8_t global = parseVariable("Expect function name");
  markInitialized();
  function(TYPE_FUNCTION);
  defineVariable(global);
}

/* Parse out the variable identifier, parse out the expression and finally
 * define the value. `GLOBAL` - emits an OP_DEFINE_GLOBAL and the index of the
 * Chunk constants `LOCAL`  - flips the depth from -1 to the current to mark as
 * initialized
 */
static void varDeclaration() {
  uint16_t global = parseVariable("Expect variable name.");

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

static void ifStatement() {
  // Consume the parens and expression inside - if(expr)
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  // Save the location of the bytecode

  // thenJump is conditional - it will check the value on top of stack and jump
  // if false
  int thenJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();

  // elseJump is unconditional - the program will always jump to this
  int elseJump = emitJump(OP_JUMP);

  patchJump(thenJump);
  emitByte(OP_POP);

  if (match(TOKEN_ELSE)) statement();

  patchJump(elseJump);
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

static void printStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after statement value.");
  emitByte(OP_PRINT);
}

static void returnStatement() {
  if (current->type == TYPE_SCRIPT) {
    error("Can't return from top-level code.");
  }

  if (match(TOKEN_SEMICOLON)) {
    emitReturn();
  } else {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
    emitByte(OP_RETURN);
  }
}

static void forStatement() {
  beginScope();
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
  if (match(TOKEN_SEMICOLON)) {
    // No initializer.
  } else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    expressionStatement();
  }

  int loopStart = currentChunk()->count;
  int exitJump = -1;
  if (!match(TOKEN_SEMICOLON)) {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

    // Jump out of the loop if the condition is false.
    exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP); // Condition.
  }

  if (!match(TOKEN_RIGHT_PAREN)) {
    int bodyJump = emitJump(OP_JUMP);
    int incrementStart = currentChunk()->count;
    expression();
    emitByte(OP_POP);
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

    emitLoop(loopStart);
    loopStart = incrementStart;
    patchJump(bodyJump);
  }

  statement();
  emitLoop(loopStart);

  if (exitJump != -1) {
    patchJump(exitJump);
    emitByte(OP_POP); // Condition.
  }
  endScope();
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

static void declaration() {
  if (match(TOKEN_FUN)) {
    funDeclaration();
  } else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    statement();
  }
  if (parser.panicMode) synchronize();
};

static void statement() {
  if (match(TOKEN_PRINT)) {
    printStatement();
  } else if (match(TOKEN_FOR)) {
    forStatement();
  } else if (match(TOKEN_IF)) {
    ifStatement();
  } else if (match(TOKEN_RETURN)) {
    returnStatement();
  } else if (match(TOKEN_WHILE)) {
    whileStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } else {
    expressionStatement();
  }
}

// ============================================================================
// COMPILER_MAIN
// ============================================================================
static void COMPILER_MAIN() {}

ObjFunction* compile(const char* source) {
  initScanner(source);
  Compiler compiler;
  initCompiler(&compiler, TYPE_SCRIPT);
  // compilingChunk = chunk;

  parser.hadError = false;
  parser.panicMode = false;

  // The call to advance() “primes the pump” on the scanner.
  advance();
  while (!match(TOKEN_EOF)) {
    declaration();
  }
  // expression();
  // consume(TOKEN_EOF, "Expect end of expression");

  ObjFunction* function = endCompiler();
  return parser.hadError ? NULL : function;
}