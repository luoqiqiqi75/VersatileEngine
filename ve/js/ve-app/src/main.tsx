import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import App from './App';
import { veHttp, veWs, useVeStore } from './store/ve-store';

declare global {
  interface Window {
    ve: {
      http: typeof veHttp;
      ws: typeof veWs;
      store: typeof useVeStore;
    };
  }
}

window.ve = { http: veHttp, ws: veWs, store: useVeStore };

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
