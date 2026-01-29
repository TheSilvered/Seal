# Goals

- Simple to embed in other applications
- Dynamic typing
- Small feature set but powerful

## Built-in types

- `Null`: represents the absence of a value
- `Bool`: `true` or `false`
- `Int`: 64-bit signed integer
- `Float`: double-precision floating point number
- `Str`: array of bytes, not actual characters
- `List`: ordered sequence of values, it can be heterogenous
- `Map`: hash map with key-value pairs
- `Func`: closure

Immutability is linked to "instances" rather than types (i.e. objects can be
frozen).

Each symbol can either be a variable or a constant. The constant, once
initialized cannot be re-assigned but it can be assigned to a mutable object.

Any operation that would modify an object that is frozen results in a deep copy
of the object in question.

Similarly when a frozen object is required (as the key of a map or as a member
of another frozen object) any mutable object is deeply copied into a new
frozen object.

Note that the following types always behave as if they were frozen:

- `Null`
- `Bool`
- `Int`
- `Float`

Closures are a special kind of object since the value itself is immutable but
the captured values may change. Because of this functions cannot be used as keys
of a hash map but they can appear in frozen objects, although the object itself
cannot be hashed.

## Syntax

```
# This is a comment
#[ this is a
multiline comment ]#

# Variable declaration
var a = 3;
# Constant declaration
let PI = 3.14;

# Function
func say_hi(name) {
    # Function call
    print("Hello, {name}!");
}

# Range-based for loop
for i in ..10 {
    print(i);
}

# C-style for loop
for var i = 0; i < 10; i += 1 {
    print(i);
}

# While loop
var j = 0;
while j < 10 {
    print(j);
    j += 1;
}

# Value literals:

# Integer
123;
0b101101;
0xcafe;

# Float
1.2;
1.;
.3;

# String (always frozen, when mutated a copy is created)
"Hello, world";
"interpolated: {1 + 3}";

# Null
null;

# Bool
true;
false;

# List (frozen)
(1, 2, 3);
("hello", 3.12, a);

# List (mutable)
[1, 2, 3];
["hello", 3.12, a];

# Map (frozen)
("a": 1, null: 2, .field: "hello");
# Note: (.field: value) and ("field": value) are equivalent

# Map (mutable)
["a": 1, null: 2, .field: "hello"];

# Anonymus function
|a, b| { print(a + b); }

# List indexing
a[0] = "First item";
print(a[1]);

# Map access
print(a["weird key"]);
print(a.field); # Note: equivalent to a["field"]
a["weird key"] = 3;
a.field = "?";

# Pseudo-methods
## The syntax
map:method(arg);
## Is exactly equivalent to
map.method(map, arg);
map["method"](map, arg);

# More calling syntax
## Writing
value::function();
## Is equivalent to
function(value);

## This means that this is a valid hello world
"Hello, world!"::print();
```
