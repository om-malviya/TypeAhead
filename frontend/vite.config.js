import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// In dev the React app runs on :5173 and the C++ backend on :8080.
// Proxy the API paths so the browser can use same-origin relative URLs.
export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      '/suggest': 'http://localhost:8080',
      '/search': 'http://localhost:8080',
      '/trending': 'http://localhost:8080',
      '/cache': 'http://localhost:8080',
      '/stats': 'http://localhost:8080',
      '/admin': 'http://localhost:8080',
      '/healthz': 'http://localhost:8080',
    },
  },
})
