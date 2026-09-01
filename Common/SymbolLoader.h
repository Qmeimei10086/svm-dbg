#pragma once

#include "Symbols.h"

// Downloads the kernel PDB (symsrv), resolves all function addresses and struct
// offsets into out. Returns TRUE on success. Every step is reported through
// log (may be NULL).
BOOL SymbolLoadAndResolve(PSYMBOLS_DATA out, PDB_LOG_CB log);

// Cleans up the dbghelp handler after SymbolLoadAndResolve.
void SymbolCleanup();
