# \[WIP\] SrcLang - Source Programming Language

---

Srclang is a simple and easy-to-learn dynamically typed scripting programming language.

## Concept - "Data and its flow"

- **Data** Everything is data, the number, string, list, and functions.
- **Flow** Controlling and Manipulating that data.


## Data

> A memory block that holds some useful information.

Data in srclang can be stored in the following forms.

| Type     | ID       | About                                              | Example                             |
| -------- | -------- | -------------------------------------------------- | ----------------------------------- |
| Null     | null_t   | blank memory block                                 | `null`                              |
| Integer  | int      | Signed Integer −2,147,483,647 -> 2,147,483,647     | `1035`                              |
| Decimal  | float    | Floating point value in range 1.7E-308 to 1.7E+308 | `10.35`                             |
| Boolean  | bool     | true or false                                      | `true`                              |
| String   | string   | Immutable string buffer                            | `"Hello World"`                     |
| Array    | array    | Mutable list of data                               | `[10, "hey", true]`                 |
| Function | function | List of instructions                               | `fun (a, b) { ret a + b; };` |
| Type     | type     | Holds the information of srclang block type        | `<type int>`                        |
| Error    | error    | Holds error message                                | `Undefined variable`                |
| Native   | native   | symbol representing the c function                 | `fun (string, ...) int`   |


## Flow

> Controlling and Manipulating that data.

### Conditions.

Branching the control flow based on conditions.

``` 
                if condition
                      |
                      |
     _____ TRUE ______|___ FALSE _______
    |                                   |
    |                                   |
```

*Example:*
```srclang
let x = 10;
if x > 10 {
    println (x, " is greater than ", 10);
} else if x < 10 {
    println (x, " is smaller than ", 10);
} else {
    println (x, " is equal to ", 10);
}
```

### Looping

Repeating the specific flow until the condition is satisfied.

```
                CONDITION <----------|
                    |                |
                    |                |
                    |------TRUTH-----|
                    |
                  FALSE
```

*Example:*
```
let x = 10;
for x > 0 {
    println (x);
    x = x - 1;
}

range(10).foreach (fun (i) {
    println (i);
});

```


## Operations

Srclang supports the following operations on data

| Data Type | Operations                             |
| --------- | -------------------------------------- |
| Null      | == !=                                  |
| Integer   | + - * / == != < > >= <= %  && \|\| ()  |
| Float     | + - * / == != < > >= <= %  &&  \|\| () |
| String    | + == != > < [] ()                      |
| Boolean   | && \|\|  ==  != ()                     |
| Array     | + == != > < [] ()                      |
| Map       | + == != > < [] .                       |
| Error     | == != ()                               |
| Function  | ()                                     |
| Type      | == != ()                               |
| Native    | ()                                     |
| Any       |                                        |


# Usage

SrcLang can be used in the following modes.
- Interactive: Single command interpretation.
- Scripting: Execution of source code via script

# Installation

## Prerequisites

- GCC g++ 10.x (GNU C compiler, CLang also work but not tested)
- glibc (Needed dlopen, dlsym for native functions)
- libffi (For calling native functions) (static)

## Building from source

- `$ git clone https://github.com/itsManjeet/srclang`
- `$ cmake -B build -DCMAKE_INSTALL_PREFIX=/usr` 
- `$ cmake --build build`

### Testing the build

- `$ ./build/srclang` in interactive mode
- `$ ./build/srclang ./code.src` in scripting mode

