const fallbackHost = typeof window !== 'undefined' ? window.location.hostname : 'localhost';
const fallbackProtocol = typeof window !== 'undefined' && window.location.protocol === 'https:' ? 'https' : 'http';

export const API_BASE_URL = (
  import.meta.env.VITE_API_URL ?? `${fallbackProtocol}://${fallbackHost}:8000/api`
).replace(/\/$/, '');

export const WS_URL = (
  import.meta.env.VITE_WS_URL ?? API_BASE_URL.replace(/^http/, 'ws') + '/ws'
).replace(/\/$/, '');

export const apiUrl = (path: string): string => {
  const normalizedPath = path.startsWith('/') ? path : `/${path}`;
  return `${API_BASE_URL}${normalizedPath}`;
};
