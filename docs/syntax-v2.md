# Syntax

Current language syntax is documented in:

- `docs/zh-CN/language.md`
- `docs/zh-CN/expressions.md`
- `docs/zh-CN/block.md`
- `docs/zh-CN/grammar.md`

SCMD 0.11 adds compile-time execution without introducing a second control-flow syntax:

```scmd
const N = 16;
u8 table[N] = 0;

compile
{
    for(var i = 0; i < N; i += 1)
    {
        table[i] = i;
    }
}

volatile u8 host_state = 0;

@noopt
function exact_bridge()
{
    host_state = 1;
}
```

The same `if(...)`, `while(...)`, and `for(init; cond; step)` grammar is used both at runtime and inside `compile {}`.
