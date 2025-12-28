// The main root
#ifndef clox_common_h
#define clox_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Prints each instruction as it is executed, along with the current stack state. 
// It’s a dynamic trace—shows the VM’s state and control flow during runtime.
#define DEBUG_TRACE_EXECUTION

// Before execution begins. Static disassembly—shows the code as data, not as it runs.
// #define DEBUG_PRINT_CODE

#define UINT8_COUNT (UINT8_MAX + 1)

#endif