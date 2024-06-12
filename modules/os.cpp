/*
 * Copyright (c) 2024 Manjeet Singh <itsmanjeet1998@gmail.com>.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "../src/Interpreter.h"

using namespace SrcLang;

SRCLANG_MODULE_FUNC(exit) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Number);
    exit(SRCLANG_VALUE_AS_NUMBER(args[0]));
}

SRCLANG_MODULE_INIT {
    SRCLANG_MODULE_DEFINE(EXIT_SUCCESS, SRCLANG_VALUE_NUMBER(EXIT_SUCCESS));
    SRCLANG_MODULE_DEFINE(EXIT_FAILURE, SRCLANG_VALUE_NUMBER(EXIT_FAILURE));
    SRCLANG_MODULE_DEFINE_FUNC(exit);
}