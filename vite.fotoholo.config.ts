import tailwindcss from '@tailwindcss/vite';
import react from '@vitejs/plugin-react';
import path from 'path';
import { defineConfig } from 'vite';

export default defineConfig({
  // This dedicated build is copied under Android assets/fotoholo-web/.
  base: './',
  plugins: [react(), tailwindcss()],
  resolve: {
    alias: { '@': path.resolve(__dirname, '.') },
  },
  build: {
    outDir: 'dist-fotoholo',
    emptyOutDir: true,
    rollupOptions: {
      input: path.resolve(__dirname, 'fotoholo-embed.html'),
    },
  },
});
