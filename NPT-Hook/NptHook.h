#ifndef NPT_HOOK_H
#define NPT_HOOK_H

#include <ntddk.h>

//
// NPT (Nested Page Table) hook library — AMD-V (SVM) based.
//
// This is a thin C++ wrapper around the AMD-V hypervisor + NPT hook engine.
// It lets a driver intercept the execution of arbitrary kernel functions
// without modifying the original code (the hook is applied via a shadow page).
//
// Requirements:
//   - x64 only, AMD CPU with SVM + NPT enabled.
//   - The hook function must have the exact same signature / calling
//     convention as the origin function. When a hook fires, the hook
//     function is entered with the original register state (arguments are
//     still in rcx/rdx/r8/r9 and on the stack as if the origin was called).
//

//
// Initialize the hypervisor and the NPT hook engine.
// Must be called before any other NptHook* function.
// Returns STATUS_SUCCESS on success.
//
NTSTATUS NptHookInitialize();

//
// Tear everything down: remove all hooks, leave virtualization and free
// all resources. Safe to call even if initialization failed or already
// uninitialized.
//
void NptHookUninitialize();

//
// Install a hook: when execution reaches `pOrigin`, control is redirected
// to `pHook` (the hook function) instead.
//   pOrigin - address of the function to hook.
//   pHook   - address of the hook function (same signature as pOrigin).
// Returns STATUS_SUCCESS on success.
//
NTSTATUS NptHookAdd(PVOID pOrigin, PVOID pHook);

//
// Remove a previously installed hook for `pOrigin`.
//
NTSTATUS NptHookRemove(PVOID pOrigin);

//
// Return a trampoline used to call the original `pOrigin` function from
// inside a hook handler. The returned pointer has the same signature as
// `pOrigin`; calling it runs the original first instruction(s) and then
// jumps past them, so the hook is not re-triggered.
// Returns NULL if `pOrigin` has no trampoline yet (call NptHookAdd first).
//
PVOID NptHookGetFunctionCaller(PVOID pOrigin);

//
// Release the trampoline previously obtained via NptHookGetFunctionCaller.
//
void NptHookRemoveFunctionCaller(PVOID pOrigin);

#endif // NPT_HOOK_H
