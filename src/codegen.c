#include "lipi.h"

#include <string.h>

int lipi_emit_assembly(LipiCompiler *c, const char *path, const char *target) {
    const LipiBackend *backend = NULL;
    if (!target || strcmp(target, "x86_64-linux") == 0) {
        backend = lipi_backend_x86_64();
    }
    if (!backend) {
        LipiSpan sp = { c->forms.len ? ((LipiAst *)c->forms.items[0])->span.file : "<command line>", 1, 1, 1 };
        lipi_diag_error(c, sp, "unsupported target", target ? target : "<none>");
        return 0;
    }
    c->backend = backend;
    c->target = backend->name;
    c->asm_path = path;
    return backend->emit_program(c);
}
