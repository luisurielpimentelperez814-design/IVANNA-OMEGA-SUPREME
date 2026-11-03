from pathlib import Path

root = Path(__file__).parent
src = (root / "ivanna_pal_presence_probe.c").read_text()
assert "__attribute__((constructor))" in src
assert "dl_iterate_phdr" in src and "dlsym(RTLD_DEFAULT" in src
for forbidden in ("pal_stream_write(", "pal_get_timestamp(", "ptrace(", "process_vm_writev("):
    assert forbidden not in src, f"unexpected active hook: {forbidden}"
assert "no PAL interposition" in src
module = (root / "module/service.sh").read_text()
assert "automatic_injection=no" in module
assert "ctl.restart" not in module
print("PAL presence probe source safety checks: PASS")
