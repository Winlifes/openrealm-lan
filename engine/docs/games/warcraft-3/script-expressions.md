# JASS expression evaluation

Logical operands and argument lists have separate AST ownership. A comma after
`a and b` belongs to the surrounding call, rather than to the operator's right
operand. Otherwise a following string argument can become a third operand and
leave a native reading a boolean in the string's position. ROC/TFT regressions
exercise both the real coroutine call path and synchronous calls.

The original JASS order is unusual: OR binds more tightly than AND. The primary
[pjass grammar](https://github.com/lep/pjass/blob/master/grammar.y) declares AND
before OR in its yacc precedence table. Its
[precedence investigation](https://github.com/lep/pjass/issues/10) also records
the original game's false result for `false and true or true`. These sources
guide the JASS parser; this change does not claim a new runtime comparison
against the supplied Windows executable. Parentheses retain explicit grouping.
Galaxy's `&&`/`||` and bitwise operators keep their C ordering separately.

Parsed logical operators carry a private `TF_OPERATOR` flag. The evaluator
evaluates the left operand once and skips the right operand when the result is
already known. Ordinary functions named `__and`/`__or` are not mistaken for
parser-authored short-circuit expressions. The existing value-native wrappers
remain available for their ordinary call path.

Integer/real equality promotes the integer to the VM's single-precision real
type when either operand is real. Real/real equality compares numeric values,
so positive and negative zero compare equally. Integer/integer comparison
keeps its exact integer representation. Existing null, enum, handle, string
and code identity rules are retained.

AST ownership, ordering and flags change saved token identities and execution
meaning. JASS snapshots therefore require version 13, with native/browser
envelopes 158/19. Older versions reject without migration. This does not widen
wire/player/entity layouts or add edict state. Existing fresh-VM, suspended
callback, timer, Lua heap and whole-world save regressions exercise the new
snapshot format; rendered save/load has a separate acceptance receipt.

`wc3_jass_expression.*` covers mixed numeric equality, signed zero, logical
argument tails in both call paths, original JASS grouping, and short-circuit
side effects. This is not an exhaustive language-equivalence claim for every
JASS/Galaxy operator or arbitrary third-party embedding extension.

A JASS bare `return` ends at its line boundary, as the same primary grammar's
`RETURN newline` production specifies. Only horizontal whitespace is skipped
when deciding whether a return value follows. Original Bomber Command and
Azeroth Grand Prix retain unreachable `if`/`call` statements after an early
return; these remain parsed statements rather than accidental return values.
ROC/TFT tests also retain nested returns, valued returns, comments and CRLF.
A fresh-VM native/MEMORY64 regression saves a sleeping child before its bare
return and verifies that restoration resumes the caller exactly once. This
fix changes no AST fields or return-node execution meaning and needs no
additional snapshot-envelope version change.
