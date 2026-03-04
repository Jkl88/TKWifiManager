/**
 * TKWifiManager — переключатель темы
 * Загрузите в LittleFS как /theme.js
 *
 * Режимы:  💻 system  →  🌙 dark  →  ☀️ light  →  💻 system  → ...
 * Хранение: localStorage, ключ «tkwm-theme»
 *
 * Кнопка появляется автоматически в правом нижнем углу всех страниц,
 * которые подключают этот файл через <script src="/theme.js">.
 */
(function () {
  'use strict';

  const KEY    = 'tkwm-theme';
  const CYCLE  = ['system', 'dark', 'light'];
  const LABELS = { system: '💻', dark: '🌙', light: '☀️' };
  const TITLES = { system: 'Системная тема', dark: 'Тёмная тема', light: 'Светлая тема' };

  /* ── 1. Применить тему немедленно (до рендера) ── */
  function applyTheme(theme) {
    if (theme === 'system') {
      delete document.documentElement.dataset.theme;
    } else {
      document.documentElement.dataset.theme = theme;
    }
  }

  function getSaved() {
    try { return localStorage.getItem(KEY) || 'system'; } catch (_) { return 'system'; }
  }

  function setSaved(theme) {
    try { localStorage.setItem(KEY, theme); } catch (_) {}
  }

  // Применяем сразу, чтобы не мигала неверная тема
  applyTheme(getSaved());

  /* ── 2. Добавить кнопку-переключатель ── */
  function updateButton(btn, theme) {
    btn.textContent = LABELS[theme];
    btn.setAttribute('title', TITLES[theme] + '\n(нажмите для смены)');
    btn.setAttribute('aria-label', TITLES[theme]);
  }

  function createButton() {
    var btn = document.createElement('button');
    btn.id = 'tkwm-theme-btn';
    btn.style.cssText = [
      'position:fixed',
      'bottom:16px',
      'right:16px',
      'z-index:9999',
      'width:40px',
      'height:40px',
      'border-radius:50%',
      'border:1px solid var(--br,#1b2a44)',
      'background:var(--card,#0d1728)',
      'color:var(--ink,#e8eef7)',
      'font-size:18px',
      'line-height:1',
      'cursor:pointer',
      'display:flex',
      'align-items:center',
      'justify-content:center',
      'padding:0',
      'box-shadow:0 2px 10px rgba(0,0,0,.35)',
      'transition:opacity .2s',
      'opacity:.7'
    ].join(';');

    btn.onmouseenter = function () { btn.style.opacity = '1'; };
    btn.onmouseleave = function () { btn.style.opacity = '.7'; };

    btn.onclick = function () {
      var current = getSaved();
      var next = CYCLE[(CYCLE.indexOf(current) + 1) % CYCLE.length];
      setSaved(next);
      applyTheme(next);
      updateButton(btn, next);
    };

    updateButton(btn, getSaved());
    document.body.appendChild(btn);
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', createButton);
  } else {
    createButton();
  }
})();
