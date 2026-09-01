#pragma once
#include <ntifs.h>
#include "HookFunc.h"
#include "Symbols.h"

BOOLEAN DbgInit(ULONG Flags);
BOOLEAN UnHookFuncs();
