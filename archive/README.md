# archive/ — snapshots hist\u00f3ricos

Este directorio guarda ZIPs y parches que originalmente viv\u00edan en la ra\u00edz
del repo (v1.6, main auditado, main (11), y el parche visualizer freeze/FIR
Blackman/Aurora). No se borraron \u2014 se conservan aqu\u00ed por trazabilidad y
para reconstruir estados hist\u00f3ricos si alg\u00fan bug regresa. La ra\u00edz queda
limpia y los `.gitignore` del proyecto pueden ampliarse en el futuro para
no re-trackear estos artefactos accidentalmente.

## Contenido

| Archivo | Origen | Nota |
| --- | --- | --- |
| `IVANNA-OMEGA-SUPREME-main-11.zip` | Snapshot num\u00e9rico #11 del main | Renombrado desde `IVANNA-OMEGA-SUPREME-main (11).zip` (el espacio+par\u00e9ntesis rompe scripts CI que hacen `find`). |
| `IVANNA-OMEGA-SUPREME-main-auditado.zip` | Snapshot post-auditor\u00eda | \u2014 |
| `IVANNA-OMEGA-SUPREME-v1.6.zip` | Snapshot v1.6 | \u2014 |
| `ualizer-freeze-fir-blackman-aurora.patch` | Parche hist\u00f3rico | Nombre truncado (\u201cvis\u2026\u201d) preservado tal cual para no perder la trazabilidad de la referencia original. |

Nada de aqu\u00ed se compila ni se empaqueta. Puede eliminarse manualmente si el
espacio del repo se vuelve un problema real \u2014 pero solo con `git rm`
expl\u00edcito, no de forma silenciosa.
