#!/usr/bin/env sh
set -eu
python -m pip install -r requirements.txt
python -m unittest discover -s tests -v
python -m deh6_optics.cli --config examples/target.json --out build/deh6-demo
