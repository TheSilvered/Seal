# Virtual machine architecture

The virtual machine must keep track of:

- the call stack
- the value stack
- all allocated objects
- all loaded files

A function has:

- bytecode (shared)
- a name (shared)
- number of arguments (shared)
- captures (per-instance)

Bytecode has:

- constants
- instructions

## Bytecode

Op-codes are 1 byte and operands have different widths.

Most operands are register indices into the function's stack memory. The width
of the register index depends on the first byte:

```
0x00 - 0x7F -> one byte
0x80 - 0xFF -> two bytes
```

If the argument is one byte long the value is mapped directly, otherwise if
`b1` and `b2` are the two bytes the actual value is
`(((b1 & 0x7F) << 8) | b2) + 0x80` putting the maximum amount of stack slots
at 32895 (`2^15 + 127`) per function call.
