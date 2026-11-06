#!/usr/bin/env python3
"""Lector mínimo de política SELinux binaria (policydb v27-v33, kernel/monolítica).

Pensado para responder preguntas concretas sobre un volcado de la política viva
de Android (p.ej. /sys/fs/selinux/policy copiado como sepolicy.bin) sin depender
de setools/libsepol. Se autovalida: tras leer todo el archivo, el cursor debe
quedar EXACTAMENTE en el último byte; si no, aborta (no se emiten resultados
de una lectura desalineada).

Uso:
    python3 -I sepolicy_query.py POLICY.bin allow SRC TGT [CLASS]
    python3 -I sepolicy_query.py POLICY.bin info TYPE
    python3 -I sepolicy_query.py POLICY.bin rules-from SRC [CLASS]
"""
import struct
import sys

MAGIC = 0xF97CFF8C
AV_ALLOWED, AV_XPERMS_MASK = 0x0001, 0x0700
AV_ENABLED = 0x8000


class R:
    def __init__(self, b):
        self.b, self.o = b, 0

    def u32(self):
        v = struct.unpack_from("<I", self.b, self.o)[0]
        self.o += 4
        return v

    def u16(self):
        v = struct.unpack_from("<H", self.b, self.o)[0]
        self.o += 2
        return v

    def u8(self):
        v = self.b[self.o]
        self.o += 1
        return v

    def raw(self, n):
        v = self.b[self.o:self.o + n]
        if len(v) != n:
            raise ValueError("EOF inesperado")
        self.o += n
        return v

    def s(self, n):
        return self.raw(n).decode("ascii", "replace")

    def ebitmap(self):
        _mapsize = self.u32()
        _high = self.u32()
        count = self.u32()
        bits = set()
        for _ in range(count):
            start = self.u32()
            m = struct.unpack("<Q", self.raw(8))[0]
            for i in range(64):
                if m >> i & 1:
                    bits.add(start + i)
        return bits


class Policy:
    def __init__(self, data):
        r = self.r = R(data)
        if r.u32() != MAGIC:
            raise ValueError("no es una política SELinux binaria")
        r.s(r.u32())  # "SE Linux"
        self.ver = r.u32()
        self.config = r.u32()
        self.mls = bool(self.config & 1)
        nsym, nocon = r.u32(), r.u32()
        if not 27 <= self.ver <= 33:
            raise ValueError(f"versión de política no soportada: {self.ver}")
        if self.ver >= 22:
            self.polcaps = r.ebitmap()
        if self.ver >= 23:
            self.permissive = r.ebitmap()
        self.commons, self.classes = {}, {}
        self.types, self.type_by_val, self.type_flags = {}, {}, {}
        self.read_commons()
        self.read_classes()
        self.read_roles()
        self.read_types()
        self.read_users()
        self.read_bools()
        if self.mls:
            self.read_levels()
            self.read_cats()
        self.read_avtab()
        self.read_conds()
        self.read_role_trans_allow()
        self.read_ocontexts(nocon)
        self.read_genfs()
        if self.mls:
            self.read_range_trans()
        self.attrs = [self.r.ebitmap() for _ in range(self.ntypes)]
        self.consumed = r.o
        self.total = len(data)
        if r.o != len(data):
            raise ValueError(f"lectura desalineada: {r.o} != {len(data)}")

    # ---- símbolos ----
    def hdr(self):
        return self.r.u32(), self.r.u32()

    def read_commons(self):
        r = self.r
        _nprim, nel = self.hdr()
        for _ in range(nel):
            ln, _val, _np, pn = r.u32(), r.u32(), r.u32(), r.u32()
            name = r.s(ln)
            perms = {}
            for _ in range(pn):
                pl, pv = r.u32(), r.u32()
                perms[pv] = r.s(pl)
            self.commons[name] = perms

    def expr_list(self, with_perms):
        r = self.r
        n = r.u32()
        for _ in range(n):
            if with_perms:
                r.u32()
            nexpr = r.u32()
            for _ in range(nexpr):
                et = r.u32(); r.u32(); r.u32()
                if et == 5:  # CEXPR_NAMES
                    r.ebitmap()
                    if self.ver >= 29:
                        r.ebitmap(); r.ebitmap(); r.u32()

    def read_classes(self):
        r = self.r
        _nprim, nel = self.hdr()
        for _ in range(nel):
            ln, ln2, val, _np, pn, _ncons = (r.u32() for _ in range(6))
            name = r.s(ln)
            com = r.s(ln2) if ln2 else None
            perms = {}
            for _ in range(pn):
                pl, pv = r.u32(), r.u32()
                perms[pv] = r.s(pl)
            # constraints: ya consumimos ncons como contador de la 6ª palabra
            # retroceder no es necesario: volvemos a leer según formato
            # (ncons está antes de la clave; los nodos siguen a los permisos)
            n = _ncons
            for _ in range(n):
                r.u32()
                nexpr = r.u32()
                for _ in range(nexpr):
                    et = r.u32(); r.u32(); r.u32()
                    if et == 5:
                        r.ebitmap()
                        if self.ver >= 29:
                            r.ebitmap(); r.ebitmap(); r.u32()
            if self.ver >= 19:
                nv = r.u32()
                for _ in range(nv):
                    nexpr = r.u32()
                    for _ in range(nexpr):
                        et = r.u32(); r.u32(); r.u32()
                        if et == 5:
                            r.ebitmap()
                            if self.ver >= 29:
                                r.ebitmap(); r.ebitmap(); r.u32()
            if self.ver >= 27:
                r.u32(); r.u32(); r.u32()
            if self.ver >= 28:
                r.u32()
            self.classes[val] = (name, com, perms)

    def read_roles(self):
        r = self.r
        _np, nel = self.hdr()
        for _ in range(nel):
            ln, _val = r.u32(), r.u32()
            if self.ver >= 24:
                r.u32()
            r.s(ln)
            r.ebitmap()
            r.ebitmap()

    def read_types(self):
        r = self.r
        self.ntypes, nel = self.hdr()
        for _ in range(nel):
            ln, val, props = r.u32(), r.u32(), r.u32()
            if self.ver >= 24:
                r.u32()  # bounds
            name = r.s(ln)
            self.types[name] = val
            self.type_by_val[val] = name
            self.type_flags[name] = props

    def level(self):
        self.r.u32(); self.r.ebitmap()

    def range_(self):
        r = self.r
        items = r.u32()
        sens = [r.u32() for _ in range(items)]
        cats = [r.ebitmap() for _ in range(items)]
        return sens, cats

    def read_users(self):
        r = self.r
        _np, nel = self.hdr()
        for _ in range(nel):
            ln, _val = r.u32(), r.u32()
            if self.ver >= 24:
                r.u32()
            r.s(ln)
            r.ebitmap()
            if self.mls:
                self.range_()
                self.level()

    def read_bools(self):
        r = self.r
        _np, nel = self.hdr()
        self.bools = {}
        for _ in range(nel):
            val, state, ln = r.u32(), r.u32(), r.u32()
            self.bools[r.s(ln)] = state

    def read_levels(self):
        r = self.r
        _np, nel = self.hdr()
        for _ in range(nel):
            ln, _alias = r.u32(), r.u32()
            r.s(ln)
            self.level()

    def read_cats(self):
        r = self.r
        _np, nel = self.hdr()
        for _ in range(nel):
            ln, _val, _alias = r.u32(), r.u32(), r.u32()
            r.s(ln)

    # ---- reglas ----
    def avitem(self):
        r = self.r
        src, tgt, cls, spec = r.u16(), r.u16(), r.u16(), r.u16()
        if self.ver >= 30 and spec & AV_XPERMS_MASK:
            r.u8(); r.u8(); r.raw(32)
            data = None
        else:
            data = r.u32()
        return src, tgt, cls, spec, data

    def read_avtab(self):
        n = self.r.u32()
        self.av = []
        for _ in range(n):
            self.av.append(self.avitem())

    def read_conds(self):
        r = self.r
        self.cond_av = []
        for _ in range(r.u32()):
            r.u32()
            for _ in range(r.u32()):
                r.u32(); r.u32()
            for _ in range(2):
                for _ in range(r.u32()):
                    self.cond_av.append(self.avitem())

    def read_role_trans_allow(self):
        r = self.r
        for _ in range(r.u32()):
            for _ in range(4 if self.ver >= 26 else 3):
                r.u32()
        for _ in range(r.u32()):
            r.u32(); r.u32()
        if 25 <= self.ver < 33 and self.ver != 33:
            n = r.u32()
            if self.ver >= 31:
                raise ValueError("filename_trans comprimido no soportado")
            for _ in range(n):
                r.s(r.u32())
                r.u32(); r.u32(); r.u32(); r.u32()

    def context(self):
        r = self.r
        r.u32(); r.u32(); r.u32()
        if self.mls:
            self.range_()

    def read_ocontexts(self, nocon):
        r = self.r
        for i in range(nocon):
            for _ in range(r.u32()):
                if i == 0:
                    r.u32(); self.context()
                elif i == 1:
                    r.s(r.u32()); self.context()
                elif i == 2:
                    r.u32(); r.u32(); r.u32(); self.context()
                elif i == 3:
                    r.s(r.u32()); self.context(); self.context()
                elif i == 4:
                    r.u32(); r.u32(); self.context()
                elif i == 5:
                    r.u32(); r.s(r.u32()); self.context()
                elif i == 6:
                    r.raw(32); self.context()
                else:
                    raise ValueError("ocontext desconocido")

    def read_genfs(self):
        r = self.r
        for _ in range(r.u32()):
            r.s(r.u32())
            for _ in range(r.u32()):
                r.s(r.u32()); r.u32(); self.context()

    def read_range_trans(self):
        r = self.r
        for _ in range(r.u32()):
            r.u32(); r.u32()
            if self.ver >= 21:
                r.u32()
            self.range_()

    # ---- consultas ----
    def tid(self, name):
        if name not in self.types:
            raise KeyError(f"tipo inexistente en la política: {name}")
        return self.types[name]

    def class_perm_names(self, cid):
        name, com, perms = self.classes[cid]
        allp = dict(perms)
        if com:
            for v, p in self.commons[com].items():
                allp[v] = p
        return name, allp

    def sets_for(self, tname):
        v = self.tid(tname)
        return {v} | {a + 1 for a in self.attrs[v - 1]}

    def allowed(self, src, tgt, cls=None):
        S, T = self.sets_for(src), self.sets_for(tgt)
        out = {}
        for s, t, c, spec, data in self.av + self.cond_av:
            if not (spec & AV_ALLOWED) or data is None:
                continue
            if s in S and t in T:
                cname, perms = self.class_perm_names(c)
                if cls and cname != cls:
                    continue
                for bit in range(32):
                    if data >> bit & 1:
                        out.setdefault(cname, set()).add(perms.get(bit + 1, f"perm{bit+1}"))
        return out


def main(a):
    if len(a) < 3:
        print(__doc__); return 2
    p = Policy(open(a[1], "rb").read())
    print(f"# policy v{p.ver} mls={p.mls} types={len(p.types)} allow-rules={len(p.av)} "
          f"(parse OK: {p.consumed}/{p.total} bytes)")
    cmd = a[2]
    if cmd == "allow":
        res = p.allowed(a[3], a[4], a[5] if len(a) > 5 else None)
        for c in sorted(res):
            print(f"allow {a[3]} {a[4]}:{c} {{ {' '.join(sorted(res[c]))} }}")
        if not res:
            print("(sin reglas allow)")
    elif cmd == "info":
        t = a[3]
        v = p.tid(t)
        print(t, "value", v, "permissive" if (v in p.permissive) else "enforcing",
              "attributes:", sorted(p.type_by_val[x + 1] for x in p.attrs[v - 1] if x + 1 != v)[:80])
    else:
        print(__doc__); return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
