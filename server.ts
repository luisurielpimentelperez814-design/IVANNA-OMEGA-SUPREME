import express, { Request, Response, NextFunction } from 'express';
import { GoogleGenAI } from '@google/genai';
import path from 'path';
import fs from 'fs';
import dotenv from 'dotenv';
import { createServer as createViteServer } from 'vite';

dotenv.config();

const __dirname = path.resolve();

const app = express();

// Limita el body JSON para que un payload gigante no agote memoria del proceso.
app.use(express.json({ limit: '256kb' }));

// Cabeceras de seguridad mínimas para un panel de control servido al usuario.
app.use((_req: Request, res: Response, next: NextFunction) => {
  res.setHeader('X-Content-Type-Options', 'nosniff');
  res.setHeader('X-Frame-Options', 'DENY');
  res.setHeader('Referrer-Policy', 'no-referrer');
  next();
});

const apiKey = process.env.GEMINI_API_KEY;
if (!apiKey) {
  // No se aborta el arranque: el dashboard (UI estática) sigue sirviéndose,
  // solo el proxy de chat queda deshabilitado con un 503 explícito.
  console.warn('[server] GEMINI_API_KEY no definida — /api/chat responderá 503');
}
const ai = apiKey ? new GoogleGenAI({ apiKey }) : null;

interface ChatMessage {
  role: 'user' | 'assistant';
  content: string;
}

function isValidMessages(value: unknown): value is ChatMessage[] {
  return (
    Array.isArray(value) &&
    value.length > 0 &&
    value.length <= 100 &&
    value.every(
      (m) =>
        m !== null &&
        typeof m === 'object' &&
        (m.role === 'user' || m.role === 'assistant') &&
        typeof m.content === 'string' &&
        m.content.length > 0 &&
        m.content.length <= 32_000
    )
  );
}

app.post('/api/chat', async (req: Request, res: Response) => {
  if (!ai) {
    res.status(503).json({ error: 'Chat no disponible: GEMINI_API_KEY no configurada en el servidor.' });
    return;
  }

  const { messages, system } = req.body ?? {};

  if (!isValidMessages(messages)) {
    res.status(400).json({
      error: 'Body inválido: se espera { messages: [{role, content}, ...] } (1–100 mensajes, content no vacío).',
    });
    return;
  }
  if (system !== undefined && typeof system !== 'string') {
    res.status(400).json({ error: 'Body inválido: "system" debe ser un string.' });
    return;
  }

  try {
    const contents = messages.map((m) => ({
      role: m.role === 'assistant' ? ('model' as const) : ('user' as const),
      parts: [{ text: m.content }],
    }));

    const response = await ai.models.generateContent({
      model: 'gemini-2.5-pro',
      contents,
      config: system ? { systemInstruction: system } : undefined,
    });

    res.json({ text: response.text ?? '' });
  } catch (error) {
    const message = error instanceof Error ? error.message : 'Error desconocido';
    console.error('[server] Error en /api/chat:', message);
    res.status(502).json({ error: `Error del modelo: ${message}` });
  }
});

async function startServer() {
  const isProd = process.env.NODE_ENV === 'production';

  if (isProd) {
    const distDir = path.join(__dirname, 'dist');
    const indexHtml = path.join(distDir, 'index.html');

    if (!fs.existsSync(indexHtml)) {
      console.error(`[server] Build de producción no encontrado en ${distDir}. Ejecuta "npm run build" primero.`);
      process.exit(1);
    }

    // Estáticos del build de Vite con caché inmutable para assets fingerprinted.
    app.use(
      express.static(distDir, {
        index: false,
        setHeaders: (res, filePath) => {
          if (filePath.includes(`${path.sep}assets${path.sep}`)) {
            res.setHeader('Cache-Control', 'public, max-age=31536000, immutable');
          }
        },
      })
    );

    // Fallback SPA: cualquier GET que no sea /api/* devuelve index.html.
    // Se usa middleware final (no app.get('*')) para no depender de la
    // sintaxis de wildcards que cambió entre Express 4 y 5.
    app.use((req: Request, res: Response, next: NextFunction) => {
      if (req.method !== 'GET' || req.path.startsWith('/api/')) {
        next();
        return;
      }
      res.sendFile(indexHtml);
    });
  } else {
    const vite = await createViteServer({
      server: { middlewareMode: true },
      appType: 'spa',
    });
    app.use(vite.middlewares);
  }

  // 404 para rutas /api/* desconocidas — nunca caen al fallback SPA.
  app.use('/api', (_req: Request, res: Response) => {
    res.status(404).json({ error: 'Ruta de API no encontrada.' });
  });

  // Manejador de errores final: JSON malformado en body-parser y cualquier
  // excepción no capturada llegan aquí en vez de tumbar el proceso.
  app.use((err: unknown, _req: Request, res: Response, _next: NextFunction) => {
    if (err instanceof SyntaxError && 'body' in (err as object)) {
      res.status(400).json({ error: 'JSON malformado en el body de la petición.' });
      return;
    }
    console.error('[server] Error no manejado:', err);
    res.status(500).json({ error: 'Error interno del servidor.' });
  });

  const port = Number(process.env.PORT) || 3000;
  app.listen(port, () => {
    console.log(`[server] IVANNA OMEGA SUPREME dashboard escuchando en puerto ${port} (${isProd ? 'producción' : 'desarrollo'})`);
  });
}

startServer().catch((err) => {
  console.error('[server] Fallo fatal al arrancar:', err);
  process.exit(1);
});
