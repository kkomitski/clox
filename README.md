## Variables and values

The Chunk struct has an array of constants that gets populated during compilation. The values get pushed into the array and the index of the value gets added to the `OP_CONSTANTS` opcode:

2 bytes - [OP_CONSTANTS, 001]

The `VM` also has a pool of variables, but instead its in a hashmap form.

Depending on the scope depth the variables either get thrown up as `LOCAL` or `GLOBAL` where:
- `LOCAL`  get put onto the stack and popped on scope completion
- `GLOBAL` get lifted into the globals hashmap of the `VM`

### How globals flow

```
source:    var name = "Bob";

compile:
       constants[42] = "name"
		   constants[43] = "Bob"
		   bytecode      = [OP_CONSTANT 43][OP_DEFINE_GLOBAL 42]
	                  // OP_CONSTANT pushes the value onto the stack, OP_DEFINE_GLOBAL pops it into vm.globals

runtime:
       stack         = []
		   OP_CONSTANT   → push "Bob"
		   OP_DEFINE...  → globals["name"] = pop()
```

The compiler emits both the identifier and the value into the chunk’s constant table. At runtime the VM looks up the identifier string (stored as a constant) and uses it as the key inside `vm.globals`, a hash map that owns all global bindings. All name lookups for globals happen here, so the VM bears that cost at runtime.

### How locals flow

```
source:    { var age = 25; print age; }

compile:   locals[0]     = "age" (remembered only by the compiler)
		   constants[17] = 25
		   bytecode      = [OP_CONSTANT 17][OP_GET_LOCAL 0][OP_PRINT]

runtime:   stack         = [25]  ← slot 0 is the local
		   OP_GET_LOCAL  → read stack[0] without any name lookup
```

For locals the compiler resolves the identifier once and caches the stack slot number. The VM never sees the variable name again; it simply reads or writes the slot. Locals therefore cost an array access instead of a hash-table lookup.

### Why push work into the compiler

- Name resolution for locals is done at compile time, so runtime cost is a fast stack access.
- Globals still need dynamic lookup, but the compiler ensures the VM has the identifier ready inside the constants table.
- The VM remains simple: it only manipulates the stack or the globals table using indices the compiler already prepared.
