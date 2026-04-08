import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      // Match ve/program/ve.json ve/server/node/http|ws config when using stock dev config.
      '/api': {
        target: 'http://localhost:5080',
        changeOrigin: true,
      },
      '/health': {
        target: 'http://localhost:5080',
        changeOrigin: true,
      },
      '/ws': {
        target: 'ws://localhost:5081',
        ws: true,
        rewrite: (path) => path.replace(/^\/ws/, ''),
      },
    },
  },
});
