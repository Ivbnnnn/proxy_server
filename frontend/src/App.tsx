import {
  Activity,
  Database,
  Download,
  Filter,
  Gauge,
  Globe2,
  ListRestart,
  Pencil,
  Plus,
  RefreshCw,
  Save,
  Search,
  Send,
  Settings,
  Shield,
  Trash2,
} from 'lucide-react';
import { FormEvent, useEffect, useMemo, useState } from 'react';
import {
  AdminStats,
  CacheStats,
  MatchType,
  ProxyResult,
  RequestFilters,
  RequestRecord,
  WhitelistRule,
  api,
  apiBaseUrl,
} from './api';

const matchTypes: MatchType[] = ['EXACT', 'PREFIX', 'CONTAINS', 'REGEX'];
const httpMethods = ['GET', 'POST', 'PUT', 'DELETE', 'PATCH'];

type Notice = { tone: 'ok' | 'error'; text: string };

const emptyRule = {
  urlPattern: '',
  matchType: 'PREFIX' as MatchType,
  enabled: true,
  comment: '',
};

function asUnixSeconds(value: string): string {
  if (!value) return '';
  const ms = new Date(value).getTime();
  return Number.isNaN(ms) ? '' : String(Math.floor(ms / 1000));
}

function StatCell({ label, value }: { label: string; value: string | number }) {
  return (
    <div className="border-r border-line px-4 py-3 last:border-r-0">
      <div className="text-xs uppercase tracking-wide text-slate-500">{label}</div>
      <div className="mt-1 text-xl font-semibold text-ink">{value}</div>
    </div>
  );
}

function StatusPill({ ok, text }: { ok: boolean; text: string }) {
  return (
    <span
      className={[
        'inline-flex h-6 items-center rounded-full px-2 text-xs font-medium',
        ok ? 'bg-emerald-50 text-emerald-700' : 'bg-rose-50 text-rose-700',
      ].join(' ')}
    >
      {text}
    </span>
  );
}

export function App() {
  const [notice, setNotice] = useState<Notice | null>(null);
  const [loading, setLoading] = useState(false);

  const [adminStats, setAdminStats] = useState<AdminStats | null>(null);
  const [cacheStats, setCacheStats] = useState<CacheStats | null>(null);
  const [rules, setRules] = useState<WhitelistRule[]>([]);
  const [requests, setRequests] = useState<RequestRecord[]>([]);

  const [proxyUrl, setProxyUrl] = useState('http://example.com/');
  const [proxyMethod, setProxyMethod] = useState('GET');
  const [proxyResult, setProxyResult] = useState<ProxyResult | null>(null);

  const [ruleDraft, setRuleDraft] = useState(emptyRule);
  const [editingRuleId, setEditingRuleId] = useState<number | null>(null);

  const [filters, setFilters] = useState<RequestFilters>({});
  const [fromDate, setFromDate] = useState('');
  const [toDate, setToDate] = useState('');

  const [cacheForm, setCacheForm] = useState({ defaultTtlSeconds: '', maxEntries: '', maxBytes: '' });
  const [cacheDeleteUrl, setCacheDeleteUrl] = useState('');
  const [exportFile, setExportFile] = useState('');

  const selectedRule = useMemo(
    () => rules.find((rule) => rule.id === editingRuleId) ?? null,
    [rules, editingRuleId],
  );

  const showNotice = (tone: Notice['tone'], text: string) => {
    setNotice({ tone, text });
    window.setTimeout(() => setNotice(null), 4000);
  };

  const run = async (task: () => Promise<void>, okText?: string) => {
    setLoading(true);
    try {
      await task();
      if (okText) showNotice('ok', okText);
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Request failed';
      showNotice('error', message);
    } finally {
      setLoading(false);
    }
  };

  const loadAll = async () => {
    const [nextStats, nextCacheStats, nextRules, nextRequests] = await Promise.all([
      api.stats(),
      api.cacheStats(),
      api.listWhitelist(),
      api.listRequests({
        ...filters,
        from: asUnixSeconds(fromDate),
        to: asUnixSeconds(toDate),
      }),
    ]);

    setAdminStats(nextStats);
    setCacheStats(nextCacheStats);
    setRules(nextRules);
    setRequests(nextRequests);
    setCacheForm({
      defaultTtlSeconds: String(nextCacheStats.defaultTtlSeconds),
      maxEntries: String(nextCacheStats.maxEntries),
      maxBytes: String(nextCacheStats.maxBytes),
    });
  };

  useEffect(() => {
    void run(loadAll);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const submitProxy = (event: FormEvent) => {
    event.preventDefault();
    void run(async () => {
      const result = await api.proxy(proxyUrl, proxyMethod);
      setProxyResult(result);
      await loadAll();
    }, 'Proxy request completed');
  };

  const submitRule = (event: FormEvent) => {
    event.preventDefault();
    void run(async () => {
      if (editingRuleId === null) {
        await api.createWhitelist(ruleDraft);
      } else {
        await api.updateWhitelist(editingRuleId, ruleDraft);
      }
      setRuleDraft(emptyRule);
      setEditingRuleId(null);
      await loadAll();
    }, editingRuleId === null ? 'Whitelist rule created' : 'Whitelist rule updated');
  };

  const editRule = (rule: WhitelistRule) => {
    setEditingRuleId(rule.id);
    setRuleDraft({
      urlPattern: rule.urlPattern,
      matchType: rule.matchType,
      enabled: rule.enabled,
      comment: rule.comment,
    });
  };

  const deleteRule = (id: number) => {
    void run(async () => {
      await api.deleteWhitelist(id);
      if (editingRuleId === id) {
        setEditingRuleId(null);
        setRuleDraft(emptyRule);
      }
      await loadAll();
    }, 'Whitelist rule deleted');
  };

  const submitFilters = (event: FormEvent) => {
    event.preventDefault();
    void run(async () => {
      setRequests(
        await api.listRequests({
          ...filters,
          from: asUnixSeconds(fromDate),
          to: asUnixSeconds(toDate),
        }),
      );
    });
  };

  const submitCacheSettings = (event: FormEvent) => {
    event.preventDefault();
    void run(async () => {
      await api.updateCacheSettings({
        defaultTtlSeconds: cacheForm.defaultTtlSeconds ? Number(cacheForm.defaultTtlSeconds) : undefined,
        maxEntries: cacheForm.maxEntries ? Number(cacheForm.maxEntries) : undefined,
        maxBytes: cacheForm.maxBytes ? Number(cacheForm.maxBytes) : undefined,
      });
      await loadAll();
    }, 'Cache settings updated');
  };

  const deleteCache = (event: FormEvent) => {
    event.preventDefault();
    void run(async () => {
      const removed = await api.deleteCacheByUrl(cacheDeleteUrl);
      await loadAll();
      showNotice('ok', `Removed ${removed} cache entries`);
    });
  };

  const exportRequests = () => {
    void run(async () => {
      const file = await api.exportRequests();
      setExportFile(file);
      window.location.href = api.downloadRequestsUrl();
    }, 'CSV export created');
  };

  return (
    <main className="min-h-screen bg-surface">
      <header className="border-b border-line bg-white">
        <div className="mx-auto flex max-w-7xl flex-col gap-4 px-4 py-5 sm:px-6 lg:flex-row lg:items-center lg:justify-between">
          <div>
            <h1 className="text-2xl font-semibold text-ink">Proxy Admin</h1>
            <div className="mt-1 text-sm text-slate-500">{apiBaseUrl}</div>
          </div>
          <div className="flex flex-wrap gap-2">
            <button className="btn" type="button" onClick={() => void run(loadAll, 'Data refreshed')} disabled={loading}>
              <RefreshCw size={16} />
              Refresh
            </button>
            <a className="btn btn-primary" href={api.downloadRequestsUrl()}>
              <Download size={16} />
              Download CSV
            </a>
          </div>
        </div>
      </header>

      {notice && (
        <div className="mx-auto max-w-7xl px-4 pt-4 sm:px-6">
          <div
            className={[
              'rounded-md border px-4 py-3 text-sm',
              notice.tone === 'ok'
                ? 'border-emerald-200 bg-emerald-50 text-emerald-800'
                : 'border-rose-200 bg-rose-50 text-rose-800',
            ].join(' ')}
          >
            {notice.text}
          </div>
        </div>
      )}

      <div className="mx-auto grid max-w-7xl gap-5 px-4 py-5 sm:px-6 lg:grid-cols-[minmax(0,1fr)_360px]">
        <section className="space-y-5">
          <div className="panel overflow-hidden">
            <div className="grid grid-cols-2 md:grid-cols-3 xl:grid-cols-6">
              <StatCell label="Requests" value={adminStats?.totalRequests ?? '-'} />
              <StatCell label="Allowed" value={adminStats?.allowedRequests ?? '-'} />
              <StatCell label="Blocked" value={adminStats?.blockedRequests ?? '-'} />
              <StatCell label="Cache hits" value={adminStats?.cacheHits ?? '-'} />
              <StatCell label="Rules" value={adminStats?.whitelistRules ?? '-'} />
              <StatCell label="Admins" value={adminStats?.adminUsers ?? '-'} />
            </div>
          </div>

          <section className="panel">
            <div className="flex items-center justify-between border-b border-line px-4 py-3">
              <div className="flex items-center gap-2 font-semibold">
                <Globe2 size={18} />
                Proxy
              </div>
              {proxyResult && (
                <StatusPill
                  ok={proxyResult.status >= 200 && proxyResult.status < 400}
                  text={`${proxyResult.status} ${proxyResult.decision || ''}`}
                />
              )}
            </div>
            <form className="grid gap-3 p-4 md:grid-cols-[140px_minmax(0,1fr)_auto]" onSubmit={submitProxy}>
              <select className="field" value={proxyMethod} onChange={(event) => setProxyMethod(event.target.value)}>
                {httpMethods.map((method) => (
                  <option key={method} value={method}>
                    {method}
                  </option>
                ))}
              </select>
              <input className="field" value={proxyUrl} onChange={(event) => setProxyUrl(event.target.value)} />
              <button className="btn btn-primary" type="submit" disabled={loading || !proxyUrl}>
                <Send size={16} />
                Send
              </button>
            </form>
            {proxyResult && (
              <div className="border-t border-line p-4">
                <div className="mb-2 flex flex-wrap gap-2 text-sm">
                  <StatusPill ok={proxyResult.cacheHit} text={proxyResult.cacheHit ? 'Cache hit' : 'Cache miss'} />
                  <span className="rounded-full bg-slate-100 px-2 py-1 text-xs text-slate-600">
                    {proxyResult.responseTimeMs || '0'} ms
                  </span>
                  <span className="rounded-full bg-slate-100 px-2 py-1 text-xs text-slate-600">
                    upstream {proxyResult.upstreamStatus}
                  </span>
                </div>
                <pre className="max-h-48 overflow-auto rounded-md bg-slate-950 p-3 text-xs text-slate-100">
                  {proxyResult.body}
                </pre>
              </div>
            )}
          </section>

          <section className="panel">
            <div className="flex items-center justify-between border-b border-line px-4 py-3">
              <div className="flex items-center gap-2 font-semibold">
                <Shield size={18} />
                Whitelist
              </div>
              {selectedRule && <StatusPill ok text={`Editing #${selectedRule.id}`} />}
            </div>
            <form className="grid gap-3 p-4 lg:grid-cols-[minmax(0,1fr)_140px_120px_minmax(0,1fr)_auto]" onSubmit={submitRule}>
              <input
                className="field"
                value={ruleDraft.urlPattern}
                onChange={(event) => setRuleDraft((current) => ({ ...current, urlPattern: event.target.value }))}
              />
              <select
                className="field"
                value={ruleDraft.matchType}
                onChange={(event) => setRuleDraft((current) => ({ ...current, matchType: event.target.value as MatchType }))}
              >
                {matchTypes.map((type) => (
                  <option key={type} value={type}>
                    {type}
                  </option>
                ))}
              </select>
              <label className="inline-flex h-10 items-center gap-2 text-sm text-slate-700">
                <input
                  className="h-4 w-4 accent-accent"
                  type="checkbox"
                  checked={ruleDraft.enabled}
                  onChange={(event) => setRuleDraft((current) => ({ ...current, enabled: event.target.checked }))}
                />
                Enabled
              </label>
              <input
                className="field"
                value={ruleDraft.comment}
                onChange={(event) => setRuleDraft((current) => ({ ...current, comment: event.target.value }))}
              />
              <div className="flex gap-2">
                <button className="btn btn-primary" type="submit" disabled={loading || !ruleDraft.urlPattern}>
                  {editingRuleId === null ? <Plus size={16} /> : <Save size={16} />}
                  {editingRuleId === null ? 'Create' : 'Save'}
                </button>
                {editingRuleId !== null && (
                  <button
                    className="btn"
                    type="button"
                    onClick={() => {
                      setEditingRuleId(null);
                      setRuleDraft(emptyRule);
                    }}
                  >
                    Clear
                  </button>
                )}
              </div>
            </form>
            <div className="overflow-x-auto border-t border-line">
              <table className="min-w-full text-left text-sm">
                <thead className="bg-slate-50 text-xs uppercase text-slate-500">
                  <tr>
                    <th className="px-4 py-3">ID</th>
                    <th className="px-4 py-3">Pattern</th>
                    <th className="px-4 py-3">Type</th>
                    <th className="px-4 py-3">State</th>
                    <th className="px-4 py-3">Comment</th>
                    <th className="px-4 py-3"></th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-line">
                  {rules.map((rule) => (
                    <tr key={rule.id}>
                      <td className="px-4 py-3 font-mono text-xs">{rule.id}</td>
                      <td className="max-w-sm px-4 py-3 font-medium">{rule.urlPattern}</td>
                      <td className="px-4 py-3">{rule.matchType}</td>
                      <td className="px-4 py-3">
                        <StatusPill ok={rule.enabled} text={rule.enabled ? 'Enabled' : 'Disabled'} />
                      </td>
                      <td className="px-4 py-3 text-slate-600">{rule.comment}</td>
                      <td className="px-4 py-3">
                        <div className="flex justify-end gap-2">
                          <button className="btn" type="button" onClick={() => editRule(rule)}>
                            <Pencil size={15} />
                          </button>
                          <button className="btn btn-danger" type="button" onClick={() => deleteRule(rule.id)}>
                            <Trash2 size={15} />
                          </button>
                        </div>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </section>

          <section className="panel">
            <div className="flex items-center justify-between border-b border-line px-4 py-3">
              <div className="flex items-center gap-2 font-semibold">
                <Activity size={18} />
                Requests
              </div>
              <button className="btn" type="button" onClick={exportRequests} disabled={loading}>
                <Download size={16} />
                Export
              </button>
            </div>
            <form className="grid grid-cols-[repeat(auto-fit,minmax(170px,1fr))] gap-3 p-4" onSubmit={submitFilters}>
              <input
                className="field"
                placeholder="URL"
                value={filters.url ?? ''}
                onChange={(event) => setFilters((current) => ({ ...current, url: event.target.value }))}
              />
              <input
                className="field"
                placeholder="IP"
                value={filters.ip ?? ''}
                onChange={(event) => setFilters((current) => ({ ...current, ip: event.target.value }))}
              />
              <input
                className="field"
                placeholder="Search"
                value={filters.search ?? ''}
                onChange={(event) => setFilters((current) => ({ ...current, search: event.target.value }))}
              />
              <input className="field" type="datetime-local" value={fromDate} onChange={(event) => setFromDate(event.target.value)} />
              <input className="field" type="datetime-local" value={toDate} onChange={(event) => setToDate(event.target.value)} />
              <button className="btn btn-primary w-full" type="submit" disabled={loading}>
                <Search size={16} />
                Search
              </button>
            </form>
            {exportFile && <div className="border-t border-line px-4 py-3 text-sm text-slate-600">{exportFile}</div>}
            <div className="max-h-[520px] overflow-auto border-t border-line">
              <table className="min-w-full text-left text-sm">
                <thead className="sticky top-0 bg-slate-50 text-xs uppercase text-slate-500">
                  <tr>
                    <th className="px-4 py-3">Time</th>
                    <th className="px-4 py-3">Method</th>
                    <th className="px-4 py-3">URL</th>
                    <th className="px-4 py-3">IP</th>
                    <th className="px-4 py-3">Status</th>
                    <th className="px-4 py-3">Cache</th>
                    <th className="px-4 py-3">ms</th>
                  </tr>
                </thead>
                <tbody className="divide-y divide-line">
                  {requests.map((request) => (
                    <tr key={request.id}>
                      <td className="whitespace-nowrap px-4 py-3 text-xs text-slate-600">{request.requestedAt}</td>
                      <td className="px-4 py-3 font-semibold">{request.method}</td>
                      <td className="max-w-md truncate px-4 py-3">{request.url}</td>
                      <td className="whitespace-nowrap px-4 py-3 font-mono text-xs">{request.clientIp}</td>
                      <td className="px-4 py-3">
                        <StatusPill ok={request.allowed} text={String(request.statusCode)} />
                      </td>
                      <td className="px-4 py-3">{request.cacheHit ? 'hit' : 'miss'}</td>
                      <td className="px-4 py-3">{request.responseTimeMs}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </section>
        </section>

        <aside className="space-y-5">
          <section className="panel">
            <div className="flex items-center gap-2 border-b border-line px-4 py-3 font-semibold">
              <Gauge size={18} />
              Cache
            </div>
            <div className="grid grid-cols-2 gap-px bg-line">
              <div className="bg-white p-4">
                <div className="text-xs uppercase text-slate-500">TTL</div>
                <div className="mt-1 text-xl font-semibold">{cacheStats?.defaultTtlSeconds ?? '-'}</div>
              </div>
              <div className="bg-white p-4">
                <div className="text-xs uppercase text-slate-500">Entries</div>
                <div className="mt-1 text-xl font-semibold">{cacheStats?.entries ?? '-'}</div>
              </div>
              <div className="bg-white p-4">
                <div className="text-xs uppercase text-slate-500">Bytes</div>
                <div className="mt-1 text-xl font-semibold">{cacheStats?.bytes ?? '-'}</div>
              </div>
              <div className="bg-white p-4">
                <div className="text-xs uppercase text-slate-500">Overflow</div>
                <div className="mt-1 text-xl font-semibold">{cacheStats?.overflowEvents ?? '-'}</div>
              </div>
              <div className="bg-white p-4">
                <div className="text-xs uppercase text-slate-500">Max entries</div>
                <div className="mt-1 text-xl font-semibold">{cacheStats?.maxEntries ?? '-'}</div>
              </div>
              <div className="bg-white p-4">
                <div className="text-xs uppercase text-slate-500">Max bytes</div>
                <div className="mt-1 text-xl font-semibold">{cacheStats?.maxBytes ?? '-'}</div>
              </div>
            </div>
            <form className="space-y-3 border-t border-line p-4" onSubmit={submitCacheSettings}>
              <div className="flex items-center gap-2 font-medium">
                <Settings size={16} />
                Settings
              </div>
              <label className="block space-y-1">
                <span className="text-xs font-medium uppercase text-slate-500">Default TTL, sec</span>
                <input
                  className="field w-full"
                  type="number"
                  min="1"
                  value={cacheForm.defaultTtlSeconds}
                  onChange={(event) => setCacheForm((current) => ({ ...current, defaultTtlSeconds: event.target.value }))}
                />
              </label>
              <label className="block space-y-1">
                <span className="text-xs font-medium uppercase text-slate-500">Max entries</span>
                <input
                  className="field w-full"
                  type="number"
                  min="0"
                  value={cacheForm.maxEntries}
                  onChange={(event) => setCacheForm((current) => ({ ...current, maxEntries: event.target.value }))}
                />
              </label>
              <label className="block space-y-1">
                <span className="text-xs font-medium uppercase text-slate-500">Max bytes</span>
                <input
                  className="field w-full"
                  type="number"
                  min="0"
                  value={cacheForm.maxBytes}
                  onChange={(event) => setCacheForm((current) => ({ ...current, maxBytes: event.target.value }))}
                />
              </label>
              <button className="btn btn-primary w-full" type="submit" disabled={loading}>
                <Save size={16} />
                Save
              </button>
            </form>
            <form className="space-y-3 border-t border-line p-4" onSubmit={deleteCache}>
              <div className="flex items-center gap-2 font-medium">
                <Database size={16} />
                Delete by URL
              </div>
              <textarea
                className="field-area w-full"
                value={cacheDeleteUrl}
                onChange={(event) => setCacheDeleteUrl(event.target.value)}
              />
              <button className="btn btn-danger w-full" type="submit" disabled={loading || !cacheDeleteUrl}>
                <Trash2 size={16} />
                Delete
              </button>
            </form>
          </section>

          <section className="panel">
            <div className="flex items-center gap-2 border-b border-line px-4 py-3 font-semibold">
              <Filter size={18} />
              Filters
            </div>
            <div className="space-y-3 p-4">
              <button
                className="btn w-full"
                type="button"
                onClick={() => {
                  setFilters({});
                  setFromDate('');
                  setToDate('');
                  void run(async () => {
                    setRequests(await api.listRequests({}));
                  }, 'Filters cleared');
                }}
              >
                <ListRestart size={16} />
                Reset
              </button>
              <button className="btn w-full" type="button" onClick={() => void run(loadAll, 'Data refreshed')} disabled={loading}>
                <RefreshCw size={16} />
                Refresh
              </button>
            </div>
          </section>
        </aside>
      </div>
    </main>
  );
}
