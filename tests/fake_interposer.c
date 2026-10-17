/* Stand-in for Streamline's sl.interposer.dll: exports vkQueuePresentKHR and
   counts how many calls reach it. Built as "sl.interposer.dll" for the tests. */
#include <windows.h>
#include <stdint.h>

__declspec(dllexport) volatile LONG g_calls;

__declspec(dllexport) int32_t __stdcall vkQueuePresentKHR(void* queue, const void* presentInfo) {
    (void)queue;
    (void)presentInfo;
    InterlockedIncrement(&g_calls);
    return 0;
}
