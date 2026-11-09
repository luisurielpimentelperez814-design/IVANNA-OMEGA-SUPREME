#!/usr/bin/env python3
"""Knowledge Core — mecanismo de consulta.

Uso:
    python3 docs/KNOWLEDGE_CORE/query.py "volterra kernel medido"
    python3 docs/KNOWLEDGE_CORE/query.py --list
    python3 docs/KNOWLEDGE_CORE/query.py --domain psicoacustica
    python3 docs/KNOWLEDGE_CORE/query.py --full hrtf
    python3 docs/KNOWLEDGE_CORE/query.py --context "física nueva"

El mecanismo es léxico (sin dependencias): normaliza la consulta, puntúa
cada ficha por coincidencia de términos en título + cuerpo, y devuelve las
fichas ordenadas por relevancia. --context devuelve el texto concatenado de
las fichas relevantes, listo para pegar como contexto del prompt de Gemini
o para que un agente lo lea.
"""
import argparse
import re
import sys
import unicodedata
from pathlib import Path

CORE = Path(__file__).resolve().parent
FICHAS = CORE / "fichas"

DOMAINS = {
    "psicoacustica": ["01", "08", "11", "13", "14", "15", "25", "26"],
    "neuro": ["09", "11", "26"],
    "dsp": ["02", "03", "04", "05", "06", "10", "17", "18", "19"],
    "musica": ["07", "12", "25"],
    "salas": ["16"],
    "codecs": ["20"],
    "audicion": ["21"],
    "ml": ["22", "24"],
    "medicion": ["23"],
    "frontera": ["02", "03", "05", "12", "26"],
}


def norm(s: str) -> str:
    s = unicodedata.normalize("NFKD", s.lower())
    s = "".join(c for c in s if not unicodedata.combining(c))
    return s


def load_fichas():
    out = []
    for p in sorted(FICHAS.glob("*.md")):
        text = p.read_text(encoding="utf-8")
        m = re.match(r"# Ficha \d+ — (.+)", text)
        title = m.group(1).strip() if m else p.stem
        out.append({"id": p.stem[:2], "file": p.name, "title": title,
                    "text": text, "norm": norm(text)})
    return out


def score(ficha, terms):
    s = 0
    title_norm = norm(ficha["title"])
    for t in terms:
        if t in title_norm:
            s += 10
        s += ficha["norm"].count(t)
    return s


def summary(text, max_chars=600):
    # Primer párrafo de Concepto + primera línea de valores, si existen.
    m = re.search(r"## Concepto\n+(.+?)(?=\n## |\Z)", text, re.S)
    body = m.group(1).strip() if m else text[:max_chars]
    body = re.sub(r"\s+", " ", body)
    return body[:max_chars] + ("…" if len(body) > max_chars else "")


def main():
    ap = argparse.ArgumentParser(description="Knowledge Core — consulta")
    ap.add_argument("query", nargs="*", help="palabras clave / tema")
    ap.add_argument("--list", action="store_true", help="lista todas las fichas")
    ap.add_argument("--domain", help="dominio: " + ", ".join(sorted(DOMAINS)))
    ap.add_argument("--full", help="muestra una ficha completa (id o palabra del título)")
    ap.add_argument("--context", action="store_true",
                    help="devuelve el texto concatenado de las fichas relevantes (para Gemini)")
    ap.add_argument("--top", type=int, default=5, help="nº de fichas a devolver (def. 5)")
    args = ap.parse_args()
    fichas = load_fichas()

    if args.list:
        for f in fichas:
            print(f"{f['id']}  {f['title']}")
        print(f"\nTotal: {len(fichas)} fichas.")
        return 0

    if args.domain:
        ids = DOMAINS.get(args.domain.lower())
        if ids is None:
            print(f"Dominio desconocido: {args.domain} (prueba: {', '.join(sorted(DOMAINS))})",
                  file=sys.stderr)
            return 2
        for f in fichas:
            if f["id"] in ids:
                print(f"{f['id']}  {f['title']}")
        return 0

    if args.full:
        q = norm(args.full)
        for f in fichas:
            if f["id"] == q or q in norm(f["title"]) or q in f["file"]:
                print(f["text"])
                return 0
        print(f"No hay ficha para: {args.full}", file=sys.stderr)
        return 2

    query = " ".join(args.query).strip()
    if not query:
        ap.print_help()
        return 2
    terms = [t for t in norm(query).split() if len(t) > 2]
    ranked = sorted(((score(f, terms), f) for f in fichas), key=lambda x: -x[0])
    ranked = [(s, f) for s, f in ranked if s > 0][: args.top]
    if not ranked:
        print(f"Sin resultados para: {query}")
        print("Prueba con: hrtf, volterra, holos, phi, orbe, asrc, harman, erb, "
              "back-emf, evolutionary, npe, visceral, enmascaramiento, sonoridad, "
              "haas, salas, beamforming, compresion, dithering, codecs, oido, "
              "yamnet, thd, separacion, pitch, fatiga.")
        return 1

    if args.context:
        print(f"# Contexto Knowledge Core — consulta: {query}\n")
        for s, f in ranked:
            print(f["text"])
            print("\n---\n")
        return 0

    for s, f in ranked:
        print(f"[{f['id']}] {f['title']}  (relevancia {s})")
        print(f"      {summary(f['text'])}")
        print(f"      → docs/KNOWLEDGE_CORE/fichas/{f['file']}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
