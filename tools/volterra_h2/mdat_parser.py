#!/usr/bin/env python3
"""
mdat_parser.py — Minimal, dependency-light reader for REW .mdat files
(Java Object Serialization Stream, version 5, containing roomeqwizard.* classes).

Fase II — IVANNA OMEGA SUPREME / Volterra H2 public dataset task.
Read-only: never mutates the source .mdat. Emits a full inventory JSON.

Usage: python3 mdat_parser.py <file.mdat> <out_inventory.json>
"""
import json
import struct
import sys

# --- Java serialization constants ---
TC_NULL = 0x70
TC_REFERENCE = 0x71
TC_CLASSDESC = 0x72
TC_OBJECT = 0x73
TC_STRING = 0x74
TC_ARRAY = 0x75
TC_CLASS = 0x76
TC_BLOCKDATA = 0x77
TC_ENDBLOCKDATA = 0x78
TC_RESET = 0x79
TC_BLOCKDATALONG = 0x7A
TC_EXCEPTION = 0x7B
TC_LONGSTRING = 0x7C
TC_PROXYCLASSDESC = 0x7D
TC_ENUM = 0x7E

PRIM_SIZES = {"B": 1, "C": 2, "D": 8, "F": 4, "I": 4, "J": 8, "S": 2, "Z": 1}

SC_WRITE_METHOD = 0x01
SC_BLOCK_DATA = 0x02
SC_SERIALIZABLE = 0x04
SC_EXTERNALIZABLE = 0x08
SC_ENUM = 0x10


class ClassDesc:
    def __init__(self, name, suid, fields, super_desc, flags=SC_SERIALIZABLE):
        self.name = name
        self.suid = suid
        self.fields = fields          # list of (typecode, name, classdesc|None)
        self.super_desc = super_desc
        self.flags = flags

    def all_fields(self):
        fl = []
        sd = self.super_desc
        while sd is not None:
            fl.extend(sd.fields)
            sd = sd.super_desc
        fl.extend(self.fields)
        return fl

    def has_write_method(self):
        return bool(self.flags & SC_WRITE_METHOD)

    def __repr__(self):
        return f"<ClassDesc {self.name}>"


class JavaObj:
    __slots__ = ("cls", "values", "extra")

    def __init__(self, cls, values):
        self.cls = cls
        self.values = values
        self.extra = []  # custom writeObject data

    def get(self, name):
        for (tc, fname, _), v in zip(self.cls.all_fields(), self.values):
            if fname == name:
                return v
        raise KeyError(f"{name} not in {self.cls.name}")

    def field_names(self):
        return [f[1] for f in self.cls.all_fields()]

    def __repr__(self):
        return f"<{self.cls.name} {id(self):x}>"


class JavaEnum:
    def __init__(self, cls, constant):
        self.cls = cls
        self.constant = constant

    def __repr__(self):
        return f"<{self.cls.name}.{self.constant}>"


class Reader:
    def __init__(self, data):
        self.d = data
        self.p = 0
        self.handles = {}
        self.next_handle = 0x7E0000

    def u1(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def u2(self):
        v = struct.unpack_from(">H", self.d, self.p)[0]
        self.p += 2
        return v

    def u4(self):
        v = struct.unpack_from(">I", self.d, self.p)[0]
        self.p += 4
        return v

    def s4(self):
        v = struct.unpack_from(">i", self.d, self.p)[0]
        self.p += 4
        return v

    def s8(self):
        v = struct.unpack_from(">q", self.d, self.p)[0]
        self.p += 8
        return v

    def f4(self):
        v = struct.unpack_from(">f", self.d, self.p)[0]
        self.p += 4
        return v

    def f8(self):
        v = struct.unpack_from(">d", self.d, self.p)[0]
        self.p += 8
        return v

    def bytes(self, n):
        v = self.d[self.p:self.p + n]
        self.p += n
        return v

    def assign_handle(self, obj):
        self.handles[self.next_handle] = obj
        self.next_handle += 1
        return obj

    def read_stream(self):
        assert self.d[self.p:self.p + 2] == b"\xac\xed", "not a java stream"
        self.p += 2
        ver = self.u2()
        assert ver == 5, f"unsupported serialization version {ver}"
        items = []
        while self.p < len(self.d):
            items.append(self.read_content())
        return items

    def read_content(self):
        t = self.u1()
        if t == TC_OBJECT:
            return self.read_object()
        if t == TC_CLASS:
            cd = self.read_classdesc()
            return self.assign_handle(("class", cd))
        if t == TC_ARRAY:
            return self.read_array()
        if t == TC_STRING:
            n = self.u2()
            return self.assign_handle(self.bytes(n).decode("utf-8", "replace"))
        if t == TC_LONGSTRING:
            n = self.u8()
            return self.assign_handle(self.bytes(n).decode("utf-8", "replace"))
        if t == TC_REFERENCE:
            h = self.u4()
            if h not in self.handles:
                raise RuntimeError(f"dangling handle {h:x}")
            return self.handles[h]
        if t == TC_NULL:
            return None
        if t == TC_ENUM:
            cd = self.read_classdesc()
            enum_obj = JavaEnum(cd, None)
            self.assign_handle(enum_obj)  # handle assigned before constant name
            cst = self.read_content()
            enum_obj.constant = cst
            return enum_obj
        if t == TC_BLOCKDATA:
            n = self.u1()
            return ("blockdata", self.bytes(n))
        if t == TC_BLOCKDATALONG:
            n = self.u4()
            return ("blockdata", self.bytes(n))
        if t == TC_RESET:
            self.handles = {}
            self.next_handle = 0x7E0000
            return ("reset",)
        if t == TC_EXCEPTION:
            raise RuntimeError("TC_EXCEPTION in stream")
        raise RuntimeError(f"unhandled tag 0x{t:02x} at {self.p-1}")

    def read_classdesc(self):
        t = self.u1()
        if t == TC_NULL:
            return None
        if t == TC_REFERENCE:
            h = self.u4()
            return self.handles[h]
        if t == TC_PROXYCLASSDESC:
            raise RuntimeError("proxy classdesc not supported")
        assert t == TC_CLASSDESC, f"expected classdesc got 0x{t:02x} at {self.p-1}"
        n = self.u2()
        name = self.bytes(n).decode("utf-8")
        suid = self.s8()
        # Per Java spec, the classdesc handle is assigned right after suid,
        # BEFORE fields/annotations/superclass (circular refs depend on this).
        cd = ClassDesc(name, suid, [], None)
        self.assign_handle(cd)
        flags = self.u1()
        cd.flags = flags
        nf = self.u2()
        fields = []
        for _ in range(nf):
            tc = chr(self.u1())
            ln = self.u2()
            fname = self.bytes(ln).decode("utf-8")
            fcls = None
            if tc == "L" or tc == "[":
                fcls = self.read_classdesc_or_ref()
            fields.append((tc, fname, fcls))
        # annotations (class annotations, usually empty)
        self.skip_annotations()
        super_desc = self.read_classdesc_or_ref()
        cd.fields = fields
        cd.super_desc = super_desc
        return cd

    def skip_annotations(self):
        while True:
            t2 = self.d[self.p]
            if t2 == TC_ENDBLOCKDATA:
                self.p += 1
                break
            self.read_content()

    def read_classdesc_or_ref(self):
        t = self.d[self.p]
        if t == TC_NULL:
            self.p += 1
            return None
        if t == TC_REFERENCE:
            self.p += 1
            h = self.u4()
            return self.handles[h]
        if t in (TC_STRING, TC_LONGSTRING):
            # A field-type class name is a first-class string object: it
            # CONSUMES a handle in the writer's numbering. Register it.
            self.p += 1
            if t == TC_STRING:
                n = self.u2()
            else:
                n = self.u8()
            name = self.bytes(n).decode("utf-8", "replace")
            self.assign_handle(name)
            return ClassDesc(name, 0, [], None)
        return self.read_classdesc()

    def read_object(self):
        cd = self.read_classdesc()
        values = []
        obj = JavaObj(cd, values)
        self.assign_handle(obj)
        for tc, fname, fcls in cd.all_fields():
            if tc in PRIM_SIZES:
                values.append(self.read_primitive(tc))
            else:
                values.append(self.read_content())
        if cd.has_write_method():
            # consume custom writeObject data until TC_ENDBLOCKDATA
            while True:
                t = self.d[self.p]
                if t == TC_ENDBLOCKDATA:
                    self.p += 1
                    break
                v = self.read_content()
                obj.extra.append(v)
        return obj

    def read_array(self):
        cd = self.read_classdesc()
        n = self.u4()
        arr = []
        self.assign_handle(arr)
        aname = cd.name if cd else "?"
        for _ in range(n):
            if aname.startswith("[L") or aname == "[Ljava.lang.String;":
                arr.append(self.read_content())
            elif aname.startswith("[") and len(aname) == 2:
                tc = aname[1]
                if tc in PRIM_SIZES:
                    arr.append(self.read_primitive(tc))
                else:
                    arr.append(self.read_content())
            else:
                arr.append(self.read_content())
        return arr

    def read_primitive(self, tc):
        if tc == "B":
            v = self.bytes(1)[0]
            if v >= 128:
                v -= 256
            return v
        if tc == "C":
            return self.u2()
        if tc == "D":
            return self.f8()
        if tc == "F":
            return self.f4()
        if tc == "I":
            return self.s4()
        if tc == "J":
            return self.s8()
        if tc == "S":
            return struct.unpack_from(">h", self.d, self.p)[0]
        if tc == "Z":
            return self.u1() != 0
        raise RuntimeError(f"unknown primitive {tc}")


def to_jsonable(x, depth=0):
    if depth > 12:
        return "<deep>"
    if x is None or isinstance(x, (bool, int, str)):
        return x
    if isinstance(x, float):
        if x != x or x in (float("inf"), float("-inf")):
            return None
        return x
    if isinstance(x, JavaObj):
        out = {"@class": x.cls.name}
        for (tc, fname, _), v in zip(x.cls.all_fields(), x.values):
            out[fname] = to_jsonable(v, depth + 1)
        if x.extra:
            out["@extra"] = [f"<{type(e).__name__}:{len(e) if isinstance(e,(bytes,)) else ''}>" for e in x.extra]
        return out
    if isinstance(x, JavaEnum):
        return f"{x.cls.name}.{x.constant}"
    if isinstance(x, list):
        return [to_jsonable(v, depth + 1) for v in x]
    if isinstance(x, tuple) and x and x[0] == "blockdata":
        return {"@blockdata": len(x[1])}
    if isinstance(x, tuple) and x and x[0] == "class":
        return {"@classdesc": x[1].name if x[1] else None}
    if isinstance(x, bytes):
        return {"@bytes": len(x)}
    return str(x)


def main():
    path, out_path = sys.argv[1], sys.argv[2]
    data = open(path, "rb").read()
    r = Reader(data)
    items = r.read_stream()
    json.dump([to_jsonable(i) for i in items], open(out_path, "w"),
              indent=1, ensure_ascii=False)
    n_meas = 0
    for it in items:
        if isinstance(it, JavaObj) and it.cls.name == "roomeqwizard.MeasData":
            n_meas += 1
    print(f"top-level items: {len(items)}; MeasData objects: {n_meas}")
    classes = {}
    for it in items:
        if isinstance(it, JavaObj):
            classes.setdefault(it.cls.name, 0)
            classes[it.cls.name] += 1
    for k, v in sorted(classes.items()):
        print(f"  {k}: {v}")


if __name__ == "__main__":
    main()
