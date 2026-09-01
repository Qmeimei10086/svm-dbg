#include "NptHook.h"
#include "Hook.h"

namespace
{
    // Owns the SVM hypervisor, the NPT hook manager and the trampoline
    // manager. Kept in a POD storage buffer so that we control construction
    // / destruction explicitly (avoids relying on global C++ object
    // initialization order inside a kernel driver).
    class NptHookContext
    {
    public:
        SVMManager svmManager;
        NptHookManager nptHookManager;
        FunctionCallerManager functionCallerManager;

        NTSTATUS Init()
        {
            NTSTATUS status = nptHookManager.Init();
            if (!NT_SUCCESS(status))
                return status;

            status = functionCallerManager.Init();
            if (!NT_SUCCESS(status))
            {
                nptHookManager.Deinit();
                return status;
            }

            nptHookManager.SetupSVMManager(svmManager);

            status = svmManager.Init();
            if (!NT_SUCCESS(status))
            {
                functionCallerManager.Deinit();
                nptHookManager.Deinit();
                return status;
            }

            return STATUS_SUCCESS;
        }

        void Deinit()
        {
            // Leave virtualization first, then free page tables / hooks.
            svmManager.Deinit();
            nptHookManager.Deinit();
            functionCallerManager.Deinit();
        }
    };

    alignas(64) UINT8 g_contextStorage[sizeof(NptHookContext)] = {};
    NptHookContext* g_context = nullptr;
}

NTSTATUS NptHookInitialize()
{
    if (g_context != nullptr)
        return STATUS_SUCCESS;

    NptHookContext* context = new (g_contextStorage) NptHookContext();

    NTSTATUS status = context->Init();
    if (!NT_SUCCESS(status))
    {
        context->~NptHookContext();
        return status;
    }

    g_context = context;
    return STATUS_SUCCESS;
}

void NptHookUninitialize()
{
    if (g_context == nullptr)
        return;

    g_context->Deinit();
    g_context->~NptHookContext();
    g_context = nullptr;
}

NTSTATUS NptHookAdd(PVOID pOrigin, PVOID pHook)
{
    if (g_context == nullptr)
		return STATUS_DEVICE_NOT_READY;

    NptHookRecord record = {};
    record.pOriginVirtAddr = pOrigin;
    record.pGotoVirtAddr = pHook;

    return g_context->nptHookManager.AddHook(record);
}

NTSTATUS NptHookRemove(PVOID pOrigin)
{
    if (g_context == nullptr)
		return STATUS_DEVICE_NOT_READY;

    return g_context->nptHookManager.RemoveHook(pOrigin);
}

PVOID NptHookGetFunctionCaller(PVOID pOrigin)
{
    if (g_context == nullptr)
        return nullptr;

    return g_context->functionCallerManager.GetFunctionCaller(pOrigin);
}

void NptHookRemoveFunctionCaller(PVOID pOrigin)
{
    if (g_context == nullptr)
        return;

    g_context->functionCallerManager.RemoveFunctionCaller(pOrigin);
}
