#!/usr/bin/env python3
"""List the Windows API imports that a program and all of its bundled DLLs need but Willows does not provide.

  tools/dev/missing.py path/to/app.exe [more.exe ...]       (DLLs are looked up next to each exe, like Willows does)

Follows the whole dependency tree: imports satisfied by a DLL file in the program's folder are checked against that DLL's exports;
everything else must be built into Willows (src/*.c export tables). Delay-load imports are included (Willows binds them eagerly).
Prints the missing functions grouped by DLL, plus DLLs that are neither files nor built in."""
import glob, os, re, struct, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

def builtin_names():
    src = "".join(open(f, errors="replace").read() for f in glob.glob(os.path.join(ROOT, "src/*.c")) + glob.glob(os.path.join(ROOT, "src/*.inc")))
    have = set(re.findall(r'\b[A-Z][A-Z0-9]{0,2}\((?:"[\w-]+",)?(\w+)\)', src))
    have |= set(re.findall(r'\{"[\w.-]+","(\w+)"', src))
    have |= set(re.findall(r'CX\("(\w+)"', src))
    dlls = set(d.lower() for d in re.findall(r'\{"([\w.-]+)","\w+"', src)) | set(d.lower() for d in re.findall(r'#define \w+\(n\) \{"([\w.-]+)"', src))
    return have, dlls

class PE:
    def __init__(self, path):
        self.path = path; d = self.d = open(path, "rb").read()
        pe = struct.unpack_from("<I", d, 0x3C)[0]; nsec, optsz = struct.unpack_from("<H", d, pe + 6)[0], struct.unpack_from("<H", d, pe + 20)[0]
        opt = pe + 24; self.p64 = struct.unpack_from("<H", d, opt)[0] == 0x20B
        dd = opt + (112 if self.p64 else 96); self.dirs = [struct.unpack_from("<II", d, dd + 8 * i) for i in range(16)]
        self.secs = []; so = opt + optsz
        for i in range(nsec):
            vs, va, rs, rp = struct.unpack_from("<IIII", d, so + 40 * i + 8); self.secs.append((va, max(vs, rs), rp))
    def off(self, rva):
        for va, sz, rp in self.secs:
            if va <= rva < va + sz: return rva - va + rp
        raise ValueError(hex(rva))
    def cstr(self, o): e = self.d.index(b"\0", o); return self.d[o:e].decode("latin1")
    def _thunks(self, dll, t):
        step = 8 if self.p64 else 4; out = []
        while True:
            v = struct.unpack_from("<Q" if self.p64 else "<I", self.d, t)[0]
            if not v: break
            out.append((dll, f"#{v & 0xFFFF}" if v >> (63 if self.p64 else 31) else self.cstr(self.off(v & 0x7FFFFFFF) + 2))); t += step
        return out
    def imports(self):
        out = []; rva = self.dirs[1][0]
        if rva:
            o = self.off(rva)
            while True:
                oft, _, _, name, ft = struct.unpack_from("<5I", self.d, o)
                if not name: break
                out += self._thunks(self.cstr(self.off(name)), self.off(oft or ft)); o += 20
        rva = self.dirs[13][0]
        if rva:
            o = self.off(rva)
            while True:
                attrs, name, hmod, iat, int_ = struct.unpack_from("<5I", self.d, o)
                if not name: break
                out += self._thunks(self.cstr(self.off(name)), self.off(int_)); o += 32
        return out
    def exports(self):
        rva = self.dirs[0][0]
        if not rva: return set()
        o = self.off(rva); nnames, aon = struct.unpack_from("<I", self.d, o + 24)[0], struct.unpack_from("<I", self.d, o + 32)[0]
        return {self.cstr(self.off(struct.unpack_from("<I", self.d, self.off(aon) + 4 * i)[0])) for i in range(nnames)}

ALIAS = ("api-ms-win-", "ucrtbase", "vcruntime", "msvcp", "kernelbase", "ntdll", "msvcr", "wsock32")
def main():
    have, bdlls = builtin_names(); missing = {}; nofile = set(); seen = set()
    todo = [os.path.abspath(p) for p in sys.argv[1:]]
    while todo:
        p = todo.pop()
        if p in seen: continue
        seen.add(p); pe = PE(p); folder = os.path.dirname(p)
        files = {f.lower(): os.path.join(folder, f) for f in os.listdir(folder)}
        for dll, fn in pe.imports():
            k = dll.lower(); k = k if k.endswith(".dll") else k + ".dll"
            builtin = k[:-4] in bdlls or any(k.startswith(a) for a in ALIAS) and not (k in files and k.startswith(("msvcp", "vcruntime")))
            if not builtin and k in files:
                todo.append(files[k])
                if fn not in PE(files[k]).exports() and not fn.startswith("#"): missing.setdefault(dll + " (file)", set()).add(fn)
                continue
            if not builtin: nofile.add(dll)
            if fn not in have: missing.setdefault(dll.lower(), set()).add(fn)
    total = 0
    for dll in sorted(missing):
        fns = sorted(missing[dll]); total += len(fns)
        print(f"{dll}: {len(fns)}"); print("    " + " ".join(fns))
    if nofile: print("\nDLLs that are neither files nor built in:", " ".join(sorted(nofile)))
    print(f"\n{total} missing functions in {len(seen)} modules")
main()
