#!/usr/bin/env python3
"""Evidence-based IVANNA auditory gate. Missing evidence never passes."""
import argparse, json, sys
from pathlib import Path
VALID={"PASS","FAIL","NOT_TESTED"}
def evaluate(m):
    checks=m.get("checks")
    if not isinstance(checks,list) or not checks: raise ValueError("checks must be a non-empty list")
    failures=[]; missing=[]
    for i,c in enumerate(checks):
        if not isinstance(c,dict) or not isinstance(c.get("id"),str): raise ValueError(f"checks[{i}] needs id")
        s=c.get("status")
        if s not in VALID: raise ValueError(f"checks[{i}].status must be PASS, FAIL, or NOT_TESTED")
        if s=="FAIL": failures.append(c["id"])
        if s=="NOT_TESTED" or (c.get("critical") is True and s!="PASS"): missing.append(c["id"])
    scope=m.get("device_scope",{})
    if not isinstance(scope,dict): raise ValueError("device_scope must be an object")
    routes=scope.get("routes",[]); required=scope.get("required_routes",[])
    if not isinstance(routes,list) or not isinstance(required,list): raise ValueError("routes and required_routes must be lists")
    missing.extend("route:"+str(r) for r in required if r not in routes)
    if scope.get("physical_device_evidence") is not True: missing.append("physical_device_evidence")
    if scope.get("level_matched_listening_evidence") is not True: missing.append("level_matched_listening_evidence")
    status="RED" if failures else ("AMBER" if missing else "GREEN")
    code={"RED":"BLOCKED","AMBER":"PARTIAL_EVIDENCE","GREEN":"AUDITORY_MAGNIFICENCE_VALIDATED"}[status]
    return {"status":status,"code":code,"failed_checks":sorted(set(failures)),"missing_evidence":sorted(set(missing)),
            "commit":m.get("commit"),"app_version":m.get("app_version"),"device":m.get("device"),"routes":routes,
            "policy":"GREEN requires every check PASS, every required route represented, physical-device evidence and level-matched listening evidence."}
def main():
    p=argparse.ArgumentParser(description=__doc__); p.add_argument("manifest"); p.add_argument("--json-out"); a=p.parse_args()
    try: result=evaluate(json.loads(Path(a.manifest).read_text(encoding="utf-8")))
    except (OSError,json.JSONDecodeError,ValueError) as e: print("ERROR: "+str(e),file=sys.stderr); return 2
    rendered=json.dumps(result,indent=2,ensure_ascii=False); print(rendered)
    if a.json_out: Path(a.json_out).write_text(rendered+"\n",encoding="utf-8")
    return {"GREEN":0,"AMBER":1,"RED":2}[result["status"]]
if __name__=="__main__": raise SystemExit(main())
