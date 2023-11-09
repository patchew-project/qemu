/*
 * Copyright (C) 2023, Greg Manning <gmanning@rapitasystems.com>
 *
 * This hook, __pfnDliFailureHook2, is documented in the microsoft documentation here:
 * https://learn.microsoft.com/en-us/cpp/build/reference/error-handling-and-notification
 * It gets called when a delay-loaded DLL encounters various errors.
 * QEMU will set it to a function which handles the specific case of a DLL looking for
 * a "qemu.exe", and give it the running executable (regardless of what it is named).
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include <windows.h>
#include <delayimp.h>

__declspec(dllexport) PfnDliHook __pfnDliNotifyHook2 = NULL;
