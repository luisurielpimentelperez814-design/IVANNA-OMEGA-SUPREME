from pathlib import Path
src = (Path(__file__).parent / "ivanna_pal_write_counter.c").read_text()
for bad in ("ptrace(", "process_vm_writev(", "memcpy(", "memset(", "buf->buffer", "->buffer", "pthread_mutex", "malloc(", "/proc/"):
    assert bad not in src, f"forbidden: {bad}"
assert "dlsym(RTLD_NEXT" in src and "return r;" in src
assert src.count("real(stream, buf)") == 1, "must forward exactly once, unchanged"
body = src.split("ssize_t pal_stream_write", 1)[1].split("static void dump_once", 1)[0]
assert "write(" not in body and "open(" not in body and "snprintf" not in body, "no I/O/format on audio thread"
print("PAL write counter source safety checks: PASS")
