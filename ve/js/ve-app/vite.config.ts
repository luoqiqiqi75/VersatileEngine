import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      // Match ve/program/ve.json ve/server/node/http|ws config when using stock dev config.
      '/ve': {
        target: 'http://localhost:12000',
        changeOrigin: true,
      },
      '/at': {
        target: 'http://localhost:12000',
        changeOrigin: true,
      },
      '/jsonrpc': {
        target: 'http://localhost:12000',
        changeOrigin: true,
      },
      '/health': {
        target: 'http://localhost:12000',
        changeOrigin: true,
      },
      '/ws': {
        target: 'ws://localhost:12100',
        ws: true,
        rewrite: (path) => path.replace(/^\/ws/, ''),
      },
    },
  },
});
