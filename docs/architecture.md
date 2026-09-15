# Architecture

English architecture notes are intentionally minimal in 0.6. The actively maintained documentation is currently Chinese.

See:

- `docs/zh-CN/index.md`
- `docs/zh-CN/backend-cs2.md`
- `docs/zh-CN/project.md`

Pipeline:

```text
.scmd -> lexer -> parser/AST -> semantic analysis -> CFG lowering -> eager core + demand-loaded function modules -> paged CS2 package
```

A future release may insert a stable SCMD IR between semantic analysis and the CS2 backend.


The CS2 backend always emits non-`main` function bodies as demand-loaded CFG modules. The eager package contains runtime/global state plus stable function-entry stubs; the first call loads the function module, which replaces its own stub with the real entry. This behavior is mandatory and independent of `--no-opt`.
