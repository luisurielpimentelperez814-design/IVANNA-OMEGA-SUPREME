import express from 'express';
import { GoogleGenAI } from '@google/genai';
import path from 'path';
import dotenv from 'dotenv';
import { createServer as createViteServer } from 'vite';

dotenv.config();

const __dirname = path.resolve();

const app = express();
app.use(express.json());

const ai = new GoogleGenAI({ apiKey: process.env.GEMINI_API_KEY });

app.post('/api/chat', async (req, res) => {
  try {
    const { messages, system } = req.body;
    
    // Create the conversation history for Gemini
    const contents = messages.map((m: any) => ({
      role: m.role === 'assistant' ? 'model' : 'user',
      parts: [{ text: m.content }]
    }));

    const response = await ai.models.generateContent({
      model: 'gemini-2.5-pro', // The latest model
      contents: contents,
      config: {
        systemInstruction: system
      }
    });

    res.json({ text: response.text });
  } catch (error: any) {
    console.error('Chat error:', error);
    res.status(500).json({ error: error.message });
  }
});

async function startServer() {
  const isProd = process.env.NODE_ENV === 'production';
  
  if (isProd) {
    app.use(express.static(__dirname));
    app.get('*', (req, res) => {
      res.sendFile(path.join(__dirname, 'index.html'));
    });
  } else {
    const vite = await createViteServer({
      server: { middlewareMode: true },
      appType: 'spa',
    });
    app.use(vite.middlewares);
  }

  const port = process.env.PORT || 3000;
  app.listen(port, () => {
    console.log(`Server listening on port ${port}`);
  });
}

startServer();
