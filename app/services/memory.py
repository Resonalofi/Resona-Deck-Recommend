import ctypes
import ctypes.util
import gc
import sys
from collections.abc import Callable
from functools import cache


class NativeMemoryReleaser:
    def __init__(self, platform: str = sys.platform):
        self._release = self._release_for(platform)

    def release(self) -> bool:
        gc.collect()
        if self._release is None:
            return False
        try:
            return self._release()
        except OSError:
            self._release = None
            return False

    def _release_for(self, platform: str) -> Callable[[], bool] | None:
        try:
            if platform.startswith("linux"):
                return self._linux_release()
            if platform == "darwin":
                return self._macos_release()
            if platform == "win32":
                return self._windows_release()
        except (AttributeError, OSError):
            return None
        return None

    def _linux_release(self) -> Callable[[], bool]:
        libc = ctypes.CDLL(ctypes.util.find_library("c") or "libc.so.6")
        malloc_trim = libc.malloc_trim
        malloc_trim.argtypes = [ctypes.c_size_t]
        malloc_trim.restype = ctypes.c_int
        return lambda: bool(malloc_trim(0))

    def _macos_release(self) -> Callable[[], bool]:
        libsystem = ctypes.CDLL(ctypes.util.find_library("System") or "/usr/lib/libSystem.dylib")
        malloc_default_zone = libsystem.malloc_default_zone
        pressure_relief = libsystem.malloc_zone_pressure_relief

        malloc_default_zone.argtypes = []
        malloc_default_zone.restype = ctypes.c_void_p
        pressure_relief.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        pressure_relief.restype = ctypes.c_size_t

        def release() -> bool:
            zone = malloc_default_zone()
            return bool(zone and pressure_relief(zone, 0))

        return release

    def _windows_release(self) -> Callable[[], bool]:
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        get_current_process = kernel32.GetCurrentProcess
        set_working_set_size = kernel32.SetProcessWorkingSetSize

        get_current_process.argtypes = []
        get_current_process.restype = wintypes.HANDLE
        set_working_set_size.argtypes = [wintypes.HANDLE, ctypes.c_size_t, ctypes.c_size_t]
        set_working_set_size.restype = wintypes.BOOL

        trim_size = ctypes.c_size_t(-1).value
        return lambda: bool(set_working_set_size(get_current_process(), trim_size, trim_size))


@cache
def _native_memory_releaser() -> NativeMemoryReleaser:
    return NativeMemoryReleaser()


def release_unused_native_memory() -> bool:
    return _native_memory_releaser().release()
