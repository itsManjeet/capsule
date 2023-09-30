#!/bin/sh

cat src/Compiler.hxx | grep "::=" | sed 's#        /// ##g'