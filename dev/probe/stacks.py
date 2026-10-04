# stacks.py <pid> — for each thread of a 32-bit process: EIP + return addresses into AI.exe / AlienModUI copies (VA-mapped)
import ctypes, struct, sys
from ctypes import wintypes as W
k32 = ctypes.WinDLL('kernel32', use_last_error=True); psapi = ctypes.WinDLL('psapi')
pid = int(sys.argv[1])
class THREADENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', W.DWORD), ('cntUsage', W.DWORD), ('th32ThreadID', W.DWORD), ('th32OwnerProcessID', W.DWORD),
                ('tpBasePri', W.LONG), ('tpDeltaPri', W.LONG), ('dwFlags', W.DWORD)]
class FLOATING_SAVE_AREA(ctypes.Structure):
    _fields_ = [('ControlWord', W.DWORD), ('StatusWord', W.DWORD), ('TagWord', W.DWORD), ('ErrorOffset', W.DWORD),
                ('ErrorSelector', W.DWORD), ('DataOffset', W.DWORD), ('DataSelector', W.DWORD), ('RegisterArea', ctypes.c_byte * 80),
                ('Cr0NpxState', W.DWORD)]
class WOW64_CONTEXT(ctypes.Structure):
    _fields_ = [('ContextFlags', W.DWORD), ('Dr0', W.DWORD), ('Dr1', W.DWORD), ('Dr2', W.DWORD), ('Dr3', W.DWORD), ('Dr6', W.DWORD),
                ('Dr7', W.DWORD), ('FloatSave', FLOATING_SAVE_AREA), ('SegGs', W.DWORD), ('SegFs', W.DWORD), ('SegEs', W.DWORD),
                ('SegDs', W.DWORD), ('Edi', W.DWORD), ('Esi', W.DWORD), ('Ebx', W.DWORD), ('Edx', W.DWORD), ('Ecx', W.DWORD),
                ('Eax', W.DWORD), ('Ebp', W.DWORD), ('Eip', W.DWORD), ('SegCs', W.DWORD), ('EFlags', W.DWORD), ('Esp', W.DWORD),
                ('SegSs', W.DWORD), ('ExtendedRegisters', ctypes.c_byte * 512)]
k32.ReadProcessMemory.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
h = k32.OpenProcess(0x0410, False, pid)
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, n, ctypes.byref(got)); return buf.raw[:got.value]
# modules (32-bit list)
mods = (ctypes.c_void_p * 1024)(); need = W.DWORD()
psapi.EnumProcessModulesEx(h, mods, ctypes.sizeof(mods), ctypes.byref(need), 3)
class MI(ctypes.Structure): _fields_ = [('base', ctypes.c_void_p), ('size', W.DWORD), ('entry', ctypes.c_void_p)]
names = []
for i in range(need.value // ctypes.sizeof(ctypes.c_void_p)):
    if not mods[i]: continue
    mi = MI(); psapi.GetModuleInformation(h, ctypes.c_void_p(mods[i]), ctypes.byref(mi), ctypes.sizeof(mi))
    nm = ctypes.create_unicode_buffer(260); psapi.GetModuleBaseNameW(h, ctypes.c_void_p(mods[i]), nm, 260)
    names.append((mi.base, mi.base + mi.size, nm.value))
exe = names[0]
def where(a):
    for b, e, n in names:
        if b <= a < e:
            if n.lower() == 'ai.exe': return 'AI %08X' % (a - b + 0x400000)
            return '%s+%X' % (n, a - b)
    return None
snap = k32.CreateToolhelp32Snapshot(4, 0)
te = THREADENTRY32(); te.dwSize = ctypes.sizeof(te)
ok = k32.Thread32First(snap, ctypes.byref(te))
k32.Wow64GetThreadContext.argtypes = [W.HANDLE, ctypes.POINTER(WOW64_CONTEXT)]
while ok:
    if te.th32OwnerProcessID == pid:
        th = k32.OpenThread(0x0002 | 0x0008 | 0x0040, False, te.th32ThreadID)  # suspend, get ctx, query
        if th:
            k32.Wow64SuspendThread(th)
            ctx = WOW64_CONTEXT(); ctx.ContextFlags = 0x10007
            if k32.Wow64GetThreadContext(th, ctypes.byref(ctx)):
                stack = rd(ctx.Esp, 4096)
                chain = []
                for i in range(0, len(stack) - 3, 4):
                    v = struct.unpack_from('<I', stack, i)[0]
                    w = where(v)
                    if w and (w.startswith('AI ') or 'Alien' in w or w.lower().startswith('steam')):
                        chain.append(w)
                    if len(chain) >= 60: break
                print('tid %5d eip %-28s %s' % (te.th32ThreadID, where(ctx.Eip) or '%08X' % ctx.Eip, ' | '.join(chain[:60])))
            k32.ResumeThread(th)
            k32.CloseHandle(th)
    ok = k32.Thread32Next(snap, ctypes.byref(te))
