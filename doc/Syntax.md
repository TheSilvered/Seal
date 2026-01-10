# Aims

- Simple to embed in other applications
- Dynamic typing
- Small feature set but powerful

# Built-in types

These objects are immutable:

- `Null`
- `Int` (64 bit)
- `Float` (double precision)
- `Bool`
- `Tuple` (can be a non-owning view into a list)
- `Struct` (named tuple)
- `Strview` (can own the memory)
- `Char` (Unicode character)
- `Bytesview` (can own the memory)
- `Function`

These are mutable:

- `Str`
- `Bytes` (binary byte data)
- `List` (dynamic array)
- `Map` (hash-map)
- `Foreign` (user-defined object)
- `Iter`

# Syntax

```
# This is a comment

# Constant declaration
let PI = <value>;
# Variable declaration
var my_variable = <value>;

# Value literals

# Null
null

# Int
123
0xab12
0b01101

# Float
12.3
.5
123.

# Bool
true
false

# Strview
"hello, world!"
string(1:3)

# Tuple
(1, null, true)
() # empty typle
(1,) # tuple with one item

# Struct
(x: 1, y: 2)

# Bytesview
b"abc123"
bytes(1:3)

# Str
Str("Hello, world!")
string[1:3]

# Bytes
Bytes(1, 2, 3)

# List
[1, null, true]

# Map
{"a": 1, "b": 2}


# Functions
func my_function(a, b, c) <expr>
# Anonymus functions
|a, b, c| <expr>
# Function call
my_function()

# Expression
# Any values and mathematical expression
# A block is also an expression
# By default its value is null unless you return
# If the last expression does not end with a semicolon it is automatically
# returned.
{
    <code>;
    <value>
}
```
