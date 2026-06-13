#include "lipi.h"

#include <string.h>

const char *lipi_type_name(LipiTypeKind type) {
    switch (type) {
    case TYPE_I8: return "i8";
    case TYPE_I16: return "i16";
    case TYPE_I32: return "i32";
    case TYPE_I64: return "i64";
    case TYPE_U8: return "u8";
    case TYPE_U16: return "u16";
    case TYPE_U32: return "u32";
    case TYPE_U64: return "u64";
    case TYPE_BOOL: return "bool";
    case TYPE_VOID: return "none";
    case TYPE_PTR: return "ptr";
    case TYPE_STR: return "str";
    case TYPE_ARRAY: return "array";
    case TYPE_MAP: return "map";
    case TYPE_UNKNOWN:
    default: return "<unknown>";
    }
}

LipiTypeKind lipi_type_from_name(const char *name) {
    if (strcmp(name, "i8") == 0) return TYPE_I8;
    if (strcmp(name, "i16") == 0) return TYPE_I16;
    if (strcmp(name, "i32") == 0) return TYPE_I32;
    if (strcmp(name, "i64") == 0) return TYPE_I64;
    if (strcmp(name, "u8") == 0) return TYPE_U8;
    if (strcmp(name, "u16") == 0) return TYPE_U16;
    if (strcmp(name, "u32") == 0) return TYPE_U32;
    if (strcmp(name, "u64") == 0) return TYPE_U64;
    if (strcmp(name, "bool") == 0) return TYPE_BOOL;
    if (strcmp(name, "none") == 0 || strcmp(name, "void") == 0) return TYPE_VOID;
    if (strcmp(name, "ptr") == 0) return TYPE_PTR;
    if (strcmp(name, "str") == 0) return TYPE_STR;
    if (strcmp(name, "array") == 0 || strcmp(name, "list") == 0) return TYPE_ARRAY;
    if (strcmp(name, "map") == 0) return TYPE_MAP;
    return TYPE_UNKNOWN;
}

int lipi_type_is_signed_integer(LipiTypeKind type) {
    return type == TYPE_I8 || type == TYPE_I16 || type == TYPE_I32 || type == TYPE_I64;
}

int lipi_type_is_unsigned_integer(LipiTypeKind type) {
    return type == TYPE_U8 || type == TYPE_U16 || type == TYPE_U32 || type == TYPE_U64;
}

int lipi_type_is_integer(LipiTypeKind type) {
    return lipi_type_is_signed_integer(type) || lipi_type_is_unsigned_integer(type);
}

int lipi_type_is_pointer_like(LipiTypeKind type) {
    return type == TYPE_PTR || type == TYPE_STR || type == TYPE_ARRAY || type == TYPE_MAP;
}

int lipi_type_is_runtime_scalar(LipiTypeKind type) {
    return lipi_type_is_integer(type) || type == TYPE_BOOL || lipi_type_is_pointer_like(type);
}

int lipi_type_int_bits(LipiTypeKind type) {
    switch (type) {
    case TYPE_I8:
    case TYPE_U8:
        return 8;
    case TYPE_I16:
    case TYPE_U16:
        return 16;
    case TYPE_I32:
    case TYPE_U32:
        return 32;
    case TYPE_I64:
    case TYPE_U64:
        return 64;
    default:
        return 0;
    }
}

int lipi_split_typed_name(LipiCompiler *c, const char *text, char **name, char **type_name) {
    const char *colon = strrchr(text, ':');
    if (!colon || colon == text || colon[1] == 0) {
        return 0;
    }
    *name = lipi_arena_strndup(&c->arena, text, (size_t)(colon - text));
    *type_name = lipi_arena_strdup(&c->arena, colon + 1);
    return 1;
}
