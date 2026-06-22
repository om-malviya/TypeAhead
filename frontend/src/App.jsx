import { useEffect, useRef, useState, useCallback } from 'react'
import './App.css'

const DEBOUNCE_MS = 200
const RECENT_KEY = 'ta.recent'
const THEME_KEY = 'ta.theme'

function Highlight({ text, prefix }) {
  const p = prefix.trim().toLowerCase()
  if (p && text.toLowerCase().startsWith(p)) {
    return (
      <span>
        <strong>{text.slice(0, p.length)}</strong>
        {text.slice(p.length)}
      </span>
    )
  }
  return <span>{text}</span>
}

export default function App() {
  const [query, setQuery] = useState('')
  const [suggestions, setSuggestions] = useState([])
  const [open, setOpen] = useState(false)
  const [active, setActive] = useState(-1)
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState(null)
  const [toast, setToast] = useState(null)
  const [trending, setTrending] = useState([])
  const [recent, setRecent] = useState(() => {
    try {
      return JSON.parse(localStorage.getItem(RECENT_KEY)) || []
    } catch {
      return []
    }
  })
  const [theme, setTheme] = useState(
    () => localStorage.getItem(THEME_KEY) || 'light'
  )
  const inputRef = useRef(null)
  const reqId = useRef(0)

  useEffect(() => {
    document.documentElement.dataset.theme = theme
    localStorage.setItem(THEME_KEY, theme)
  }, [theme])

  const loadTrending = useCallback(async () => {
    try {
      const r = await fetch('/trending?n=10')
      const d = await r.json()
      setTrending(d.trending || [])
    } catch {
      /* non-critical */
    }
  }, [])

  useEffect(() => {
    loadTrending()
  }, [loadTrending])

  useEffect(() => {
    const q = query.trim()
    if (!q) {
      setSuggestions([])
      setOpen(false)
      setError(null)
      return
    }
    const id = ++reqId.current
    setLoading(true)
    const t = setTimeout(async () => {
      try {
        const r = await fetch(`/suggest?q=${encodeURIComponent(q)}`)
        if (!r.ok) throw new Error(`HTTP ${r.status}`)
        const d = await r.json()
        if (id !== reqId.current) return
        setSuggestions(d.suggestions || [])
        setOpen(true)
        setActive(-1)
        setError(null)
      } catch {
        if (id !== reqId.current) return
        setError('Could not reach the suggestions API.')
        setSuggestions([])
      } finally {
        if (id === reqId.current) setLoading(false)
      }
    }, DEBOUNCE_MS)
    return () => clearTimeout(t)
  }, [query])

  function pushRecent(q) {
    setRecent((prev) => {
      const next = [q, ...prev.filter((x) => x !== q)].slice(0, 8)
      localStorage.setItem(RECENT_KEY, JSON.stringify(next))
      return next
    })
  }

  async function submitSearch(text) {
    const q = (text ?? query).trim()
    if (!q) return
    setOpen(false)
    setQuery(q)
    try {
      const r = await fetch('/search', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ query: q }),
      })
      const d = await r.json()
      setToast(d.message || 'Searched')
      setTimeout(() => setToast(null), 2500)
      pushRecent(q)
      setTimeout(loadTrending, 1200)
    } catch {
      setError('Search request failed.')
    }
  }

  function clearRecent() {
    setRecent([])
    localStorage.removeItem(RECENT_KEY)
  }

  function onKeyDown(e) {
    if (!open || suggestions.length === 0) {
      if (e.key === 'Enter') submitSearch()
      return
    }
    if (e.key === 'ArrowDown') {
      e.preventDefault()
      setActive((i) => Math.min(i + 1, suggestions.length - 1))
    } else if (e.key === 'ArrowUp') {
      e.preventDefault()
      setActive((i) => Math.max(i - 1, -1))
    } else if (e.key === 'Enter') {
      e.preventDefault()
      submitSearch(active >= 0 ? suggestions[active].query : query)
    } else if (e.key === 'Escape') {
      setOpen(false)
    }
  }

  return (
    <div className="page">
      <header className="topbar">
        <div className="brand">
          <span className="logo" />
          <span>Search Typeahead</span>
        </div>
        <button
          className="theme-toggle"
          onClick={() => setTheme((t) => (t === 'dark' ? 'light' : 'dark'))}
          title="Toggle theme"
          aria-label="Toggle dark mode"
        >
          {theme === 'dark' ? '☀' : '☾'}
        </button>
      </header>

      <div className="hero">
        <h1>What are you looking for?</h1>
      </div>

      <div className="searchbar">
        <div className="input-wrap">
          <svg className="icon" viewBox="0 0 24 24" aria-hidden="true">
            <path d="M21 21l-4.3-4.3M11 18a7 7 0 1 1 0-14 7 7 0 0 1 0 14z" />
          </svg>
          <input
            ref={inputRef}
            value={query}
            placeholder="Search Wikipedia titles…"
            autoFocus
            onChange={(e) => setQuery(e.target.value)}
            onKeyDown={onKeyDown}
            onFocus={() => suggestions.length && setOpen(true)}
            aria-label="Search"
            autoComplete="off"
          />
          {loading && <span className="spinner" aria-label="loading" />}
          {query && !loading && (
            <button
              className="clear"
              onClick={() => {
                setQuery('')
                inputRef.current?.focus()
              }}
              aria-label="Clear"
            >
              ×
            </button>
          )}
          {open && (suggestions.length > 0 || loading) && (
            <ul className="dropdown" role="listbox">
              {suggestions.length === 0 && loading
                ? Array.from({ length: 5 }).map((_, i) => (
                    <li key={i} className="row skeleton">
                      <span className="bar" />
                    </li>
                  ))
                : suggestions.map((s, i) => (
                    <li
                      key={s.query}
                      role="option"
                      aria-selected={i === active}
                      className={i === active ? 'row active' : 'row'}
                      onMouseEnter={() => setActive(i)}
                      onMouseDown={(e) => {
                        e.preventDefault()
                        submitSearch(s.query)
                      }}
                    >
                      <span className="q">
                        <Highlight text={s.query} prefix={query} />
                      </span>
                    </li>
                  ))}
            </ul>
          )}
        </div>
        <button className="go" onClick={() => submitSearch()}>
          Search
        </button>
      </div>

      {toast && <div className="toast">{toast}</div>}
      {error && <div className="error">{error}</div>}


      {recent.length > 0 && (
        <section className="block">
          <div className="block-head">
            <h2>Recent searches</h2>
            <button className="link" onClick={clearRecent}>
              clear
            </button>
          </div>
          <div className="chips">
            {recent.map((q) => (
              <button
                key={q}
                className="chip ghost"
                onClick={() => submitSearch(q)}
              >
                ↺ {q}
              </button>
            ))}
          </div>
        </section>
      )}

      <section className="block">
        <div className="block-head">
          <h2>Trending searches</h2>
          <button className="refresh" onClick={loadTrending} title="Refresh">
            ↻
          </button>
        </div>
        {trending.length === 0 ? (
          <p className="muted">No trending data yet — try searching above.</p>
        ) : (
          <ol className="chips">
            {trending.map((t, i) => (
              <li key={t.query}>
                <button className="chip" onClick={() => submitSearch(t.query)}>
                  <span className="rank">{i + 1}</span>
                  {t.query}
                </button>
              </li>
            ))}
          </ol>
        )}
      </section>

      <footer className="foot">
        Type to see suggestions · ↑/↓ to navigate · Enter to search
      </footer>
    </div>
  )
}
