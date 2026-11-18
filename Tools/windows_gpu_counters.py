"""Read local Windows per-process GPU allocation gauges through the PDH API."""
import ctypes
from ctypes import wintypes
import re


class Number(ctypes.Union):
    _fields_ = [("large", ctypes.c_int64), ("real", ctypes.c_double), ("integer", wintypes.LONG), ("pointer", ctypes.c_void_p)]


class Value(ctypes.Structure):
    _fields_ = [("status", wintypes.DWORD), ("number", Number)]


class Item(ctypes.Structure):
    _fields_ = [("name", wintypes.LPWSTR), ("value", Value)]


class GPUProcessCounters:
    def __init__(self):
        self.api = ctypes.WinDLL("pdh")
        self.api.PdhOpenQueryW.argtypes = [wintypes.LPCWSTR, ctypes.c_size_t, ctypes.POINTER(wintypes.HANDLE)]
        self.api.PdhAddEnglishCounterW.argtypes = [wintypes.HANDLE, wintypes.LPCWSTR, ctypes.c_size_t, ctypes.POINTER(wintypes.HANDLE)]
        self.api.PdhCollectQueryData.argtypes = [wintypes.HANDLE]
        self.api.PdhGetFormattedCounterArrayW.argtypes = [wintypes.HANDLE, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
        self.api.PdhCloseQuery.argtypes = [wintypes.HANDLE]
        for name in ("PdhOpenQueryW", "PdhAddEnglishCounterW", "PdhCollectQueryData", "PdhGetFormattedCounterArrayW", "PdhCloseQuery"):
            getattr(self.api, name).restype = wintypes.DWORD
        self.query = wintypes.HANDLE()
        self.check(self.api.PdhOpenQueryW(None, 0, ctypes.byref(self.query)))
        self.counters = {}
        try:
            for label, gauge in (("dedicated", "Dedicated Usage"), ("shared", "Shared Usage")):
                counter = wintypes.HANDLE()
                self.check(self.api.PdhAddEnglishCounterW(self.query, "\\GPU Process Memory(*)\\" + gauge, 0, ctypes.byref(counter)))
                self.counters[label] = counter
        except Exception:
            self.close()
            raise

    @staticmethod
    def check(status):
        if status: raise OSError(f"PDH status 0x{status:08x}")

    def sample(self):
        self.check(self.api.PdhCollectQueryData(self.query))
        records = {}
        for label, counter in self.counters.items():
            size, count = wintypes.DWORD(), wintypes.DWORD()
            status = self.api.PdhGetFormattedCounterArrayW(counter, 0x400, ctypes.byref(size), ctypes.byref(count), None)
            if not size.value: continue
            if status not in (0, 0x800007d2): self.check(status)
            buffer = ctypes.create_string_buffer(size.value)
            self.check(self.api.PdhGetFormattedCounterArrayW(counter, 0x400, ctypes.byref(size), ctypes.byref(count), buffer))
            items = ctypes.cast(buffer, ctypes.POINTER(Item))
            for index in range(count.value):
                item = items[index]; match = re.search(r"pid_(\d+)_", item.name or "")
                if match and item.value.status in (0, 1):
                    entry = records.setdefault(int(match.group(1)), {})
                    entry[label] = entry.get(label, 0) + item.value.number.large
        return records

    def close(self):
        if self.query:
            self.api.PdhCloseQuery(self.query); self.query = wintypes.HANDLE()


if __name__ == "__main__":
    import json
    counter = GPUProcessCounters()
    try: print(json.dumps(counter.sample()))
    finally: counter.close()
