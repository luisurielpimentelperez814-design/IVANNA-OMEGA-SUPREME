#!/usr/bin/env python3
"""
Cliente LLM de IVANNA vía OmniRoute (tools/evolution/llm_client.py).

El motor de evolución redacta sus reescrituras con el LLM que sirve el
gateway OmniRoute de esta VM (OpenAI-compatible), NO con Gemini directo:

  endpoint  http://localhost:20128/v1/chat/completions
            (o IVANNA_OMNIROUTE_URL si se define, p. ej. para pruebas)
  modelo    "ivanna-free" (pool ddgw que sí funciona en v3.8.53:
            ddgw/claude-haiku-4-5, ddgw/gpt-5.6-luna, ddgw/gpt-5.4-mini,
            ddgw/mistral-small-2603. Pollinations NO existe en v3.8.53.)
  API key   SOLO en ~/.omniroute/ivanna-app-key (modo 600, solo host).
            Se lee en cada llamada, nunca se imprime, nunca se guarda en
            ningún archivo del repo, nunca sale en errores ni en logs.

Uso:
  from llm_client import chat, extract_patch, health
  ok, ms, models = health()
  text = chat([{"role": "system", "content": "..."},
               {"role": "user", "content": "..."}])
  patch = extract_patch(text)   # None si el LLM no devolvió un diff

Reglas de seguridad (no negociables):
  - El LLM recibe SOLO código fuente relevante + diagnóstico (autorizado
    por Esse 2026-11-14 para redactar reescrituras). NUNCA credenciales,
    NUNCA secretos, NUNCA PATs, NUNCA tokens.
  - Timeouts: 15 s connect / 60 s read. Reintentos: 3 con backoff
    1 s / 2 s / 4 s, solo ante errores transitorios (429, 5xx, red).
    Un 4xx distinto de 429 NO se reintenta (es un error de petición).
  - Solo stdlib (http.client). Sin dependencias nuevas, sin `requests`.

Coste: $0 (pool gratuito del gateway; la key es de la VM, no de pago).
"""
import http.client
import json
import os
import re
import time
import urllib.parse
from pathlib import Path

DEFAULT_BASE_URL = "http://localhost:20128/v1"
DEFAULT_MODEL = "ivanna-free"
KEY_FILE = Path.home() / ".omniroute" / "ivanna-app-key"

CONNECT_TIMEOUT_S = 15
READ_TIMEOUT_S = 60
MAX_RETRIES = 3
BACKOFF_S = (1.0, 2.0, 4.0)


class LlmError(RuntimeError):
    """Error del cliente LLM. Nunca contiene la API key."""


def _read_key() -> str:
    """Lee la API key de OmniRoute. Falla si no existe o está vacía.

    La key NUNCA se devuelve en mensajes de error ni se registra."""
    try:
        key = KEY_FILE.read_text().strip()
    except OSError as e:
        raise LlmError(
            f"sin API key de OmniRoute: no se puede leer {KEY_FILE} ({e.strerror})")
    if not key:
        raise LlmError(f"API key de OmniRoute vacía en {KEY_FILE}")
    return key


def _base_url() -> str:
    return os.environ.get("IVANNA_OMNIROUTE_URL", DEFAULT_BASE_URL).rstrip("/")


def _model() -> str:
    return os.environ.get("IVANNA_OMNIROUTE_MODEL", DEFAULT_MODEL)


def _split_url(url: str):
    u = urllib.parse.urlparse(url)
    if u.scheme not in ("http", "https"):
        raise LlmError(f"esquema no soportado en URL de OmniRoute: {u.scheme}")
    return u.scheme, u.hostname, u.port or (443 if u.scheme == "https" else 80)


def _parse_chat_body(body: str) -> str:
    """Extrae choices[0].message.content de JSON o de un cuerpo SSE.

    El gateway responde JSON cuando stream=false; el fallback SSE existe
    porque IvannaGeminiAgent ya tuvo que aceptar ambos (2026-11-14)."""
    body = body.strip()
    if not body:
        raise LlmError("respuesta vacía del gateway")
    if body.startswith("data:"):
        last = None
        for line in body.splitlines():
            line = line.strip()
            if line.startswith("data:") and line[5:].strip() != "[DONE]":
                last = line[5:].strip()
        if last is None:
            raise LlmError("cuerpo SSE sin ningún evento data:")
        body = last
    try:
        data = json.loads(body)
    except json.JSONDecodeError as e:
        raise LlmError(f"respuesta no-JSON del gateway: {e}")
    try:
        content = data["choices"][0]["message"]["content"]
    except (KeyError, IndexError, TypeError):
        raise LlmError("respuesta sin choices[0].message.content")
    if not isinstance(content, str) or not content.strip():
        raise LlmError("el LLM devolvió contenido vacío")
    return content


def _post_once(path: str, payload: dict, key: str,
               read_timeout_s: int = READ_TIMEOUT_S) -> tuple:
    """Un intento POST. Devuelve (status, body). Sin reintentos aquí."""
    scheme, host, port = _split_url(_base_url())
    conn_cls = http.client.HTTPSConnection if scheme == "https" else http.client.HTTPConnection
    conn = conn_cls(host, port, timeout=CONNECT_TIMEOUT_S)
    try:
        conn.connect()
        # Tras conectar, el timeout pasa a ser el de lectura.
        if conn.sock is not None:
            conn.sock.settimeout(read_timeout_s)
        body = json.dumps(payload).encode("utf-8")
        conn.request("POST", path, body=body, headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {key}",
            "Accept": "application/json",
        })
        resp = conn.getresponse()
        return resp.status, resp.read().decode("utf-8", errors="replace")
    finally:
        conn.close()


def chat(messages, model=None, temperature=0.2, max_tokens=8192,
         read_timeout_s=READ_TIMEOUT_S):
    """Una llamada chat/completions con reintentos. Devuelve el texto.

    `messages`: lista OpenAI [{role, content}]. La temperatura baja (0.2)
    es deliberada: la reescritura debe ser determinista, no creativa.
    Lanza LlmError si el gateway no responde tras los reintentos."""
    key = _read_key()
    payload = {
        "model": model or _model(),
        "messages": messages,
        "temperature": temperature,
        "max_tokens": max_tokens,
        "stream": False,
    }
    last_error = None
    for attempt in range(MAX_RETRIES):
        try:
            status, body = _post_once("/chat/completions", payload, key,
                                      read_timeout_s=read_timeout_s)
        except (OSError, http.client.HTTPException) as e:
            last_error = LlmError(f"red/conexión con el gateway: {e}")
            if attempt < MAX_RETRIES - 1:
                time.sleep(BACKOFF_S[attempt])
                continue
            raise last_error
        if status == 200:
            return _parse_chat_body(body)
        if status in (429, 500, 502, 503, 504) and attempt < MAX_RETRIES - 1:
            last_error = LlmError(f"gateway HTTP {status} (reintentable)")
            time.sleep(BACKOFF_S[attempt])
            continue
        # 4xx (salvo 429) o 5xx agotado: no reintentar. Recortar el cuerpo
        # para no arrastrar basura; la key nunca está en el cuerpo.
        raise LlmError(f"gateway HTTP {status}: {body[:300]}")
    raise last_error  # pragma: no cover - el bucle siempre retorna o lanza


def health():
    """GET /v1/models. Devuelve (ok, latencia_ms, lista_de_modelos).

    No lanza: un gateway caído es un dato (ok=False), no una excepción.
    La key no se envía en el health-check si el endpoint no la exige;
    aquí sí se envía porque /v1/models la requiere en v3.8.53."""
    try:
        key = _read_key()
    except LlmError as e:
        return False, 0.0, [str(e)]
    scheme, host, port = _split_url(_base_url())
    conn_cls = http.client.HTTPSConnection if scheme == "https" else http.client.HTTPConnection
    conn = conn_cls(host, port, timeout=CONNECT_TIMEOUT_S)
    t0 = time.monotonic()
    try:
        conn.connect()
        if conn.sock is not None:
            conn.sock.settimeout(READ_TIMEOUT_S)
        conn.request("GET", "/v1/models", headers={
            "Authorization": f"Bearer {key}",
            "Accept": "application/json",
        })
        resp = conn.getresponse()
        body = resp.read().decode("utf-8", errors="replace")
        ms = (time.monotonic() - t0) * 1000.0
        if resp.status != 200:
            return False, ms, [f"HTTP {resp.status}"]
        try:
            models = [m.get("id", "?") for m in json.loads(body).get("data", [])]
        except (json.JSONDecodeError, AttributeError):
            models = []
        return True, ms, models
    except (OSError, http.client.HTTPException) as e:
        return False, (time.monotonic() - t0) * 1000.0, [f"red: {e}"]
    finally:
        conn.close()


_FENCE_RE = re.compile(r"```(?:diff|patch)\n(.*?)```", re.S)


def _apply_patch_to_unified(text: str, base_dir):
    """Convierte un parche estilo `*** Begin Patch` (formato apply_patch)
    a unified diff. El pool 'ivanna-free' emite este formato a menudo
    (medido 2026-11-14: 2 respuestas vacías `*** End Patch` y 1 completa
    `*** Begin Patch` con `*** Update File:`) aunque se le pida unified
    diff: rechazarlo dejaba el draft inservible.

    Reglas fail-closed: cada hunk `@@` (sin números de línea) se ubica por
    coincidencia EXACTA de su contexto (líneas ` ` + `-`) en el archivo
    actual; si el contexto no aparece exactamente 1 vez, o el archivo no
    existe, o hay `*** Delete File:`, devuelve None (no se inventa nada).
    `*** Add File:` crea el archivo (old = /dev/null)."""
    import re as _re
    lines = text.splitlines()
    # Localizar el bloque apply_patch (con o sin valla ```diff).
    start = next((i for i, l in enumerate(lines)
                  if l.strip() == "*** Begin Patch"), None)
    if start is None:
        return None
    end = next((i for i, l in enumerate(lines[start:], start)
                if l.strip() == "*** End Patch"), len(lines))
    body = lines[start + 1:end]
    sections = []
    current = None
    for line in body:
        m = _re.match(r"\*\*\* (Update|Add) File: (.+)$", line.strip())
        if m:
            current = {"op": m.group(1), "path": m.group(2).strip(),
                       "hunks": []}
            sections.append(current)
            continue
        if line.strip().startswith("*** Delete File:"):
            return None
        if line.strip() == "@@":
            if current is None:
                return None
            current["hunks"].append([])
            continue
        if current is not None and current["hunks"]:
            if line[:1] in (" ", "+", "-"):
                current["hunks"][-1].append(line)
            elif line == "":
                current["hunks"][-1].append(" ")
            else:
                return None  # línea fuera de formato: no adivinar
    if not sections:
        return None
    out = []
    for sec in sections:
        path = sec["path"]
        if sec["op"] == "Add":
            new_lines = [h[1:] for hunk in sec["hunks"] for h in hunk
                         if h.startswith("+")]
            if not new_lines or any(not h.startswith("+")
                                    for hunk in sec["hunks"] for h in hunk):
                return None
            out.append(f"diff --git a/{path} b/{path}\n")
            out.append("new file mode 100644\n")
            out.append("--- /dev/null\n")
            out.append(f"+++ b/{path}\n")
            out.append(f"@@ -0,0 +1,{len(new_lines)} @@\n")
            out.extend(f"+{l}\n" for l in new_lines)
            continue
        fpath = Path(base_dir) / path
        if not fpath.exists():
            return None
        old_lines = fpath.read_text(errors="ignore").splitlines()
        for hunk in sec["hunks"]:
            old_seq = [h[1:] for h in hunk if h.startswith((" ", "-"))]
            new_seq = [h[1:] for h in hunk if h.startswith((" ", "+"))]
            if not old_seq and not new_seq:
                return None
            # Buscar la secuencia vieja (contexto + eliminadas) en el archivo.
            hits = [i for i in range(len(old_lines) - len(old_seq) + 1)
                    if old_lines[i:i + len(old_seq)] == old_seq]
            if len(hits) != 1:
                return None  # 0 = contexto inexistente; >1 = ambiguo
            s0 = hits[0]
            out.append(f"diff --git a/{path} b/{path}\n")
            out.append(f"--- a/{path}\n")
            out.append(f"+++ b/{path}\n")
            out.append(f"@@ -{s0 + 1},{len(old_seq)} +{s0 + 1},{len(new_seq)} @@\n")
            out.extend(h + "\n" for h in hunk)
            old_lines = old_lines[:s0] + new_seq + old_lines[s0 + len(old_seq):]
    return "".join(out) or None


def extract_patch(text: str, base_dir=None):
    """Extrae un unified diff de la respuesta del LLM.

    Acepta, en orden: bloque ```diff / ```patch con `diff --git`, texto que
    ya empieza por "diff --git", y parche estilo `*** Begin Patch`
    (convertido a unified diff por contexto; ver _apply_patch_to_unified).
    Devuelve None si no hay diff utilizable (el llamador debe tratarlo
    como fallo de redacción, no inventar un parche)."""
    if not text:
        return None
    m = _FENCE_RE.search(text)
    if m:
        patch = m.group(1).strip()
        if patch.startswith("diff --git"):
            return patch
        if "*** Begin Patch" in patch:
            return _apply_patch_to_unified(
                patch, base_dir or Path.cwd())
        return None
    stripped = text.strip()
    if stripped.startswith("diff --git"):
        return stripped
    if "*** Begin Patch" in text:
        return _apply_patch_to_unified(text, base_dir or Path.cwd())
    return None


def draft_rewrite(system_prompt: str, user_prompt: str, model=None,
                  max_tokens=8192, base_dir=None):
    """Redacta una reescritura y devuelve (texto_completo, patch_o_None).

    El read timeout del draft es 240 s (no 60 s): medido 2026-11-14, una
    respuesta trivial ("OK") tardó 37.6 s por el gateway; un diff completo
    necesita más. El llamador decide qué hacer con el parche (validarlo
    en scratch); esta función no aplica nada al árbol."""
    text = chat([{"role": "system", "content": system_prompt},
                 {"role": "user", "content": user_prompt}],
                model=model, max_tokens=max_tokens, read_timeout_s=240)
    return text, extract_patch(text, base_dir=base_dir)
