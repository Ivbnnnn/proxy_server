import axios from 'axios';

export type MatchType = 'EXACT' | 'PREFIX' | 'CONTAINS' | 'REGEX';

export interface WhitelistRule {
  id: number;
  urlPattern: string;
  matchType: MatchType;
  enabled: boolean;
  comment: string;
  createdAt: string;
  updatedAt: string;
}

export interface RequestRecord {
  id: number;
  url: string;
  method: string;
  clientIp: string;
  statusCode: number;
  allowed: boolean;
  cacheHit: boolean;
  matchedRuleId: number | null;
  responseTimeMs: number;
  requestedAt: string;
}

export interface AdminStats {
  totalRequests: number;
  allowedRequests: number;
  blockedRequests: number;
  cacheHits: number;
  whitelistRules: number;
  adminUsers: number;
}

export interface CacheStats {
  defaultTtlSeconds: number;
  maxEntries: number;
  maxBytes: number;
  entries: number;
  bytes: number;
  overflowEvents: number;
}

export interface ProxyResult {
  status: number;
  upstreamStatus: string;
  body: string;
  decision: string;
  cacheHit: boolean;
  responseTimeMs: string;
}

export interface RequestFilters {
  url?: string;
  ip?: string;
  search?: string;
  from?: string;
  to?: string;
}

const configuredApiBaseUrl = import.meta.env.VITE_API_BASE_URL?.trim();
export const apiBaseUrl = configuredApiBaseUrl || 'http://127.0.0.1:8080';
const normalizedApiBaseUrl = apiBaseUrl.replace(/\/+$/, '');

const http = axios.create({
  baseURL: apiBaseUrl,
  timeout: 15000,
});

export const api = {
  async proxy(url: string, method: string): Promise<ProxyResult> {
    const params = new URLSearchParams({ url, method });
    const response = await http.request<string>({
      url: `${normalizedApiBaseUrl}/proxy?${params.toString()}`,
      method: method.toLowerCase(),
      responseType: 'text',
      transformResponse: [(data) => data],
      validateStatus: () => true,
    });

    return {
      status: response.status,
      upstreamStatus: response.headers['x-proxy-upstream-status'] ?? String(response.status),
      body: response.data,
      decision: response.headers['x-proxy-decision'] ?? '',
      cacheHit: response.headers['x-proxy-cache-hit'] === 'true',
      responseTimeMs: response.headers['x-proxy-response-time-ms'] ?? '',
    };
  },

  async listWhitelist(): Promise<WhitelistRule[]> {
    const response = await http.get<WhitelistRule[]>('/admin/whitelist');
    return response.data;
  },

  async createWhitelist(payload: Pick<WhitelistRule, 'urlPattern' | 'matchType' | 'enabled' | 'comment'>): Promise<WhitelistRule> {
    const response = await http.post<WhitelistRule>('/admin/whitelist', payload);
    return response.data;
  },

  async updateWhitelist(id: number, payload: Pick<WhitelistRule, 'urlPattern' | 'matchType' | 'enabled' | 'comment'>): Promise<void> {
    await http.put('/admin/whitelist', payload, { params: { id } });
  },

  async deleteWhitelist(id: number): Promise<void> {
    await http.delete('/admin/whitelist', { params: { id } });
  },

  async listRequests(filters: RequestFilters): Promise<RequestRecord[]> {
    const params = Object.fromEntries(Object.entries(filters).filter(([, value]) => value !== undefined && value !== ''));
    const response = await http.get<RequestRecord[]>('/admin/requests', { params });
    return response.data;
  },

  async stats(): Promise<AdminStats> {
    const response = await http.get<AdminStats>('/admin/stats');
    return response.data;
  },

  async cacheStats(): Promise<CacheStats> {
    const response = await http.get<CacheStats>('/admin/cache/stats');
    return response.data;
  },

  async updateCacheSettings(payload: Partial<Pick<CacheStats, 'defaultTtlSeconds'>> & { maxEntries?: number; maxBytes?: number }): Promise<void> {
    await http.patch('/admin/cache/settings', payload);
  },

  async deleteCacheByUrl(url: string): Promise<number> {
    const response = await http.delete<{ removed: number }>('/admin/cache', { params: { url } });
    return response.data.removed;
  },

  async exportRequests(): Promise<string> {
    const response = await http.get<{ file: string }>('/admin/export/requests');
    return response.data.file;
  },

  downloadRequestsUrl(): string {
    return `${normalizedApiBaseUrl}/admin/export/requests/download`;
  },
};
