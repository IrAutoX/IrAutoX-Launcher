const API_BASE = window.IRAUTOX_API_BASE || '/api';
const gamesHost = document.getElementById('games');
const storeMessage = document.getElementById('storeMessage');
const launcherPrompt = document.getElementById('launcherPrompt');

function escapeHtml(value) {
  return String(value ?? '').replace(/[&<>'"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;',"'":'&#39;','"':'&quot;'}[c]));
}

function normalizeGame(game) {
  return {
    id: Number(game.id || 0),
    name: String(game.name || 'بدون نام'),
    version: String(game.version || '1.0'),
    description: String(game.desc || game.description || ''),
    icon: String(game.icon_url || game.icon || '../resources/logo.svg'),
    banner: String(game.banner_url || game.banner || game.icon_url || game.icon || '../resources/logo.svg')
  };
}

function renderGames(items) {
  gamesHost.innerHTML = '';
  const games = items.map(normalizeGame).filter(g => g.id > 0);
  if (!games.length) {
    gamesHost.innerHTML = '<div class="message">فعلاً بازی‌ای برای نمایش ثبت نشده است.</div>';
    return;
  }
  for (const game of games) {
    const card = document.createElement('article');
    card.className = 'game-card';
    card.innerHTML = `
      <div class="game-banner"><img loading="lazy" src="${escapeHtml(game.banner)}" alt="${escapeHtml(game.name)}"></div>
      <div class="game-body">
        <div class="game-title"><img loading="lazy" src="${escapeHtml(game.icon)}" alt=""><div><strong>${escapeHtml(game.name)}</strong><div class="game-meta">نسخه ${escapeHtml(game.version)}</div></div></div>
        <div class="game-meta">${escapeHtml(game.description)}</div>
        <div class="game-actions"><button class="button primary play" data-id="${game.id}">Play</button><a class="button" href="#account">حساب</a></div>
      </div>`;
    gamesHost.appendChild(card);
  }
  document.querySelectorAll('.play').forEach(button => button.addEventListener('click', () => launchGame(button.dataset.id)));
}

async function loadGames() {
  storeMessage.textContent = 'در حال دریافت فروشگاه…';
  try {
    const response = await fetch(`${API_BASE}/games`, {credentials:'include',headers:{Accept:'application/json'}});
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const data = await response.json();
    renderGames(Array.isArray(data) ? data : data.games || []);
    storeMessage.textContent = '';
  } catch (error) {
    try {
      const response = await fetch('games.json');
      const data = await response.json();
      renderGames(Array.isArray(data) ? data : data.games || []);
      storeMessage.textContent = 'برای فروشگاه آنلاین وارد حساب شوید یا اتصال API را بررسی کنید.';
    } catch {
      renderGames([]);
      storeMessage.textContent = `اتصال فروشگاه برقرار نشد: ${error.message}`;
    }
  }
}

function launchGame(id) {
  launcherPrompt.hidden = true;
  const started = performance.now();
  window.location.href = `irautox://launch/${encodeURIComponent(id)}`;
  setTimeout(() => {
    if (!document.hidden && performance.now() - started >= 900) launcherPrompt.hidden = false;
  }, 1200);
}

document.getElementById('closePrompt').addEventListener('click', () => launcherPrompt.hidden = true);
document.getElementById('refreshGames').addEventListener('click', loadGames);

document.getElementById('loginForm').addEventListener('submit', async event => {
  event.preventDefault();
  const target = document.getElementById('accountMessage');
  target.textContent = 'در حال ورود…';
  const username = document.getElementById('username').value.trim();
  const password = document.getElementById('password').value;
  try {
    const response = await fetch(`${API_BASE}/login`, {
      method: 'POST',
      headers: {'Content-Type':'application/json','Accept':'application/json'},
      credentials: 'include',
      body: JSON.stringify({username, password})
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(data.message || `HTTP ${response.status}`);
    target.textContent = `خوش آمدی ${data.username || username}`;
    document.getElementById('password').value = '';
    await loadGames();
  } catch (error) {
    target.textContent = `ورود انجام نشد: ${error.message}`;
  }
});

loadGames();
