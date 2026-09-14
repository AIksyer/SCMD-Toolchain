# Architecture

English architecture notes are intentionally minimal in 0.6. The actively maintained documentation is currently Chinese.

See:

- `docs/zh-CN/index.md`
- `docs/zh-CN/backend-cs2.md`
- `docs/zh-CN/project.md`

Pipeline:

```text
.scmd -> lexer -> parser/AST -> semantic analysis -> CFG lowering -> paged CS2 package
```

A future release may insert a stable SCMD IR between semantic analysis and the CS2 backend.
