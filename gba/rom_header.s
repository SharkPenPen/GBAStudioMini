@ The ROM header names, for a build with no project behind it.
@
@ A real project gets its own copy of this file, generated from project.json by
@ "gbs build" and found first on the include path. Kept as a file rather than as
@ a default inside crt0.s because the assembler needs the same .include either
@ way, so there is one shape to reason about instead of two.

    .ascii  "GBS M0 DEMO "              @ 0x0A0 title, 12 bytes
    .ascii  "M0D1"                      @ 0x0AC game code
    .ascii  "01"                        @ 0x0B0 maker code
