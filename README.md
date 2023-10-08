# Srclang: An Easy to learn Programming language (Work in Progress)

Welcome to the early development stage of Srclang! Srclang is an emerging, open-source programming language that's a work in progress. We're in the process of crafting a versatile and feature-rich language to empower developers, but please keep in mind that this README reflects our ongoing efforts.

## Table of Contents

- [Srclang: An Easy to learn Programming language (Work in Progress)](#srclang-an-easy-to-learn-programming-language-work-in-progress)
  - [Table of Contents](#table-of-contents)
  - [Introduction](#introduction)
  - [Features](#features)
    - [Dynamic Typing](#dynamic-typing)
    - [Garbage Collection](#garbage-collection)
    - [Simplified Data Types](#simplified-data-types)
    - [Error Handling](#error-handling)
    - [First-Class Functions](#first-class-functions)
    - [Control Structures](#control-structures)
    - [Built-In Functions](#built-in-functions)
    - [`println`](#println)
    - [`print`](#print)
    - [`gc`](#gc)
    - [`len`](#len)
    - [`append`](#append)
    - [`range`](#range)
    - [`clone`](#clone)
    - [`eval`](#eval)
    - [`pop`](#pop)
    - [`call`](#call)
    - [`alloc` and `free`](#alloc-and-free)
    - [`lower` and `upper`](#lower-and-upper)
    - [`search`](#search)
    - [`system`](#system)
  - [Examples](#examples)
  - [Getting Started](#getting-started)
  - [License](#license)

## Introduction

Srclang, also known as Source Language, is a programming language in the early stages of development. Our goal is to create a modern and accessible language that will eventually offer a rich set of features to facilitate efficient and expressive coding. This README provides an overview of our current progress.

## Features

### Dynamic Typing

Srclang is being designed as a dynamically-typed programming language, offering flexibility in variable declarations and assignments. The concept of generic variables that can store values of any type, as well as variables that can change their type during program execution, is part of our vision:

```srclang
let x = 10;
println("VALUE:", x, "TYPE:", type(x));

x = "String";
println("VALUE:", x, "TYPE:", type(x));

x = [10, x];
println("VALUE:", x, "TYPE:", type(x));
```

### Garbage Collection

We are actively working on implementing self-managed garbage collection, which will eliminate the need for manual memory management and reduce the risk of memory leaks. This feature aims to enhance code reliability and simplify resource management:

```srclang
for i in range(100000) {
    x := "Hello " + "World";
}
```

### Simplified Data Types

Our aspiration is to provide a concise set of data types, making the language easy to learn and use. Currently, we're focusing on the following data types:

- **Boolean**: A primitive data type for logical operations:

```srclang
println(true);
println(not false);
```

- **Numbers**: We are working on supporting double-precision floating-point values that can also represent a wide range of integral values. Underscores will be used for better visualization:

```srclang
let bigNumber = 1_234_567_890;
let floatingPoint = 123_456_789.99;
```

### Error Handling

We plan to include error handling to simplify runtime error management:

```srclang
let e = error("Runtime Error");
println(type(e), e);
```

### First-Class Functions

Our design philosophy involves treating functions as first-class citizens, allowing for the creation of higher-order functions. Here's a glimpse of our intended approach:

```srclang
Complex := fun (real, img) {
    self := {};

    self.real = real;
    self.img = img;

    self.__str__ = fun () {
        return str(real) + " i" + str(img);
    };

    return self;
};
```

### Control Structures

We're actively developing a variety of control structures, including conditions and loops, to facilitate program flow control:

```srclang
if x := 10; x > 10 {
    println("Greater than 10");
} else if x < 10 {
    println("Smaller");
} else {
    println("Equal");
}

count := 0;
for true {
    count = count + 1;
    if count >= 20 {
        break;
    }
    if count % 5 {
        println("skipping");
        continue;
    }
    println("i am running");
}
```

### Built-In Functions

Srclang offers a variety of built-in functions to simplify common tasks and enhance your coding experience. Here's a brief overview of some of the essential built-in functions:

### `println`

The `println` function is used to print argument values, followed by a newline character. It is a handy tool for displaying output in your programs:

```srclang
println ("Hello", 10, 20.03, Complex(10, 20), e);
```

### `print`

Similar to `println`, the `print` function is used to print argument values without adding a newline character. This allows you to display output on the same line:

```srclang
print ("Hello", 10, 20.03, Complex(10, 20), e);
```

### `gc`

The `gc` function manually triggers garbage collection, helping you manage memory efficiently and prevent memory leaks:

```srclang
gc();
```

### `len`

The `len` function calculates and prints the size of a value. It is especially useful for determining the length of strings or lists:

```srclang
len("Hello World");
len([10, 20, 30]);
```

### `append`

The `append` function adds a value to a string or list. It allows you to build and modify strings or lists dynamically:

```srclang
l := [];
append(l, 10);
append(l, 20);
append(l, 30);
println(l);
```

### `range`

The `range` function generates a range of values based on the specified parameters. It is often used in loops and iteration:

```srclang
println(range(4));           // [0, 1, 2, 3]
println(range(4, 10));       // [4, 5, 6, 7, 8, 9]
println(range(5, 20, 2));    // [5, 7, 9, 11, 13, 15, 17, 19]
```

### `clone`

The `clone` function performs a deep copy of a value and allocates space for the new value. This helps maintain data integrity:

```srclang
println(clone([10, 20, "hello"]));
```

### `eval`

`eval` allows you to evaluate Srclang code from a string. It can be a powerful tool for dynamic code execution:

```srclang
let v = 10;
println(eval("println(v);"));
```

### `pop`

The `pop` function removes and returns a value from a list. It's a useful function for managing lists:

```srclang
println(pop([10, 20, 30]));
```

### `call`

The `call` function enables the invocation of Srclang methods with arguments. It ensures safe evaluation and returns any runtime errors as an error:

```srclang
println(Complex, 10, 20);
```

### `alloc` and `free`

These functions allow you to allocate and manually free memory buffers. They are helpful for managing memory resources:

```srclang
let buffer = alloc(10);
free(buffer);
```

### `lower` and `upper`

The `lower` and `upper` functions convert string values to lowercase and uppercase, respectively:

```srclang
println(lower("HELLO"));
println(upper("hello"));
```

### `search`

The `search` function looks for a value within a string, list, or map and returns its index:

```srclang
println(search([10, 20, 40, "ley"], "ley")); // 3
```

### `system`

The `system` function provides a platform-independent way to execute system commands. It even allows you to specify a callback function for handling the command's output:

```srclang
exit_code := system("ls", fun (output) {
    print(output);
});

Our development journey continues, and we welcome your contributions and feedback as we work towards a more complete and robust Srclang.

## Examples

Stay tuned for more code examples and in-depth usage of Srclang features as we progress with our development efforts.

## Getting Started

We're actively working on providing detailed instructions for getting started with Srclang. Keep an eye on this space for updates.

## License

Srclang will be released under an open-source license. We encourage open collaboration and usage of Srclang for both personal and commercial projects. Thank you for your interest in our work-in-progress programming language!