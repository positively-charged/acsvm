`acsvm` is a virtual machine that executes ACS bytecode.

`acsvm` is not really useful for practical applications. For one, the virtual
machine does not implement every instruction of the ACS bytecode at this time.
Secondly, ACS doesn't have many useful functions that can be used outside of a
Doom-like engine. The virtual machine can be made to support useful
functions—but at that point, you might as well just use a production-grade
programming language instead.

`acsvm` might be useful for testing of tools that deal with ACS bytecode, such
as ACS compilers and decompilers.