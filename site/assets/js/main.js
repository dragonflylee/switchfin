/* GMCA promo site — progressive enhancements only.
   The <html> element gets a "js" class from an inline head script;
   all hide-then-reveal styling is scoped to it, so the page renders
   fully without JavaScript. */

const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');

// Scroll reveals
const observer = new IntersectionObserver(
  (entries) => {
    for (const e of entries) {
      if (e.isIntersecting) {
        e.target.classList.add('in');
        observer.unobserve(e.target);
      }
    }
  },
  { threshold: 0.12, rootMargin: '0px 0px -40px 0px' }
);
document.querySelectorAll('.reveal').forEach((el) => observer.observe(el));

// Compact nav slides in once the masthead has scrolled away
const nav = document.querySelector('.nav');
// Slide the compact nav in once the brand block (masthead, or the poster hero) leaves the top.
// Pages without such a block (e.g. the guide) keep the nav permanently visible via CSS (.page-guide).
const navSentinel = document.querySelector('.masthead') || document.querySelector('.hero');
if (nav && navSentinel) {
  const navObs = new IntersectionObserver(([e]) => {
    nav.classList.toggle('nav-shown', !e.isIntersecting && e.boundingClientRect.bottom <= 0);
  });
  navObs.observe(navSentinel);
}

// Live clock in the console bar, like on the real thing
const clock = document.getElementById('clock');
if (clock) {
  const tick = () => {
    clock.textContent = new Date().toLocaleTimeString('en-GB', { hour12: false });
  };
  tick();
  setInterval(tick, 1000);
}

// Latest release version, fetched once — falls back to static text
fetch('https://api.github.com/repos/thcolin/gamepad-media-center-aggregator/releases/latest')
  .then((r) => (r.ok ? r.json() : null))
  .then((release) => {
    if (!release || !release.tag_name) return;
    document.querySelectorAll('[data-version]').forEach((el) => {
      el.textContent = release.tag_name;
    });
  })
  .catch(() => {});

// Hero — rotate media center x device to prove any-on-any.
const heroStage = document.getElementById('hero-stage');
if (heroStage) {
  const imgs = Array.from(heroStage.querySelectorAll('.dev-img'));
  const rotMc = document.querySelector('.rot-mc');
  const rotDev = document.querySelector('.rot-dev');
  const capMc = document.getElementById('hero-cap-mc');
  const capDev = document.getElementById('hero-cap-dev');
  const hero = document.querySelector('.hero');

  // Media centers keep their brand colour.
  const MC = {
    plex: { name: 'Plex', color: '#e5a00d' },
    jellyfin: { name: 'Jellyfin', color: '#00a4dc' },
    emby: { name: 'Emby', color: '#52b54b' },
    // ink = lifted tint used for the small caption tag so it clears WCAG-AA on
    // the dark base; the large rotating word keeps the full brand colour.
    stremio: { name: 'Stremio', color: '#7b5bf5', ink: '#9575f7' },
  };
  // Each device colours its own two words: Switch = Nintendo (blue) / Switch (red),
  // Vita = PS (silver) / VITA lettered in the four PlayStation face-button colours
  // (△ green, ○ red, ✕ blue, □ pink), Pi = Raspberry (pink) / Pi (green).
  const DEV = {
    switch: { cap: 'Nintendo Switch', capColor: '#00c3e3', p1: 'Nintendo', c1: '#00c3e3', p2: 'Switch', c2: '#ff4554' },
    vita: { cap: 'PS Vita', capColor: '#cfd2d7', p1: 'PS', c1: '#5f6470', p2: 'Vita', p2colors: ['#3ec98a', '#e5484d', '#4c7ff0', '#e7509f'] },
    pi: { cap: 'Raspberry Pi', capColor: '#6cc04a', p1: 'Raspberry', c1: '#ff5a9e', p2: 'Pi', c2: '#6cc04a' },
  };
  // Every device × every media center = 12 combos. Stepping the device by +1
  // (mod 3) and the media center by +1 (mod 4) each tick walks ALL 12 pairings
  // exactly once per lap (gcd(3,4)=1), and — since neither index ever repeats
  // between steps — no two consecutive frames share a device OR a media center,
  // loop wrap included. The <img> order in the stage matches this same cycle.
  const DEVS = ['switch', 'vita', 'pi'];
  const MCS = ['plex', 'jellyfin', 'emby', 'stremio'];
  const N = DEVS.length * MCS.length; // 12

  const apply = (n) => {
    const mc = MC[MCS[n % MCS.length]];
    const dev = DEV[DEVS[n % DEVS.length]];
    imgs.forEach((im, k) => im.classList.toggle('on', k === n));
    if (hero) {
      hero.style.setProperty('--rot-mc', mc.color);
      hero.style.setProperty('--rot-mc-ink', mc.ink || mc.color);
    }
    if (rotMc) rotMc.textContent = mc.name;
    // Colour the device name word-by-word (second word may be lettered, e.g. VITA
    // in the four PS button colours). Same markup drives the big rotator and the
    // small caption below the device image, so both stay in sync.
    let p2html;
    if (dev.p2colors) {
      p2html = dev.p2.split('').map((ch, k) =>
        '<span style="color:' + dev.p2colors[k % dev.p2colors.length] + '">' + ch + '</span>'
      ).join('');
    } else {
      p2html = '<span style="color:' + dev.c2 + '">' + dev.p2 + '</span>';
    }
    const devHTML = '<span style="color:' + dev.c1 + '">' + dev.p1 + '</span> ' + p2html;
    if (rotDev) rotDev.innerHTML = devHTML;
    if (capMc) capMc.textContent = mc.name;
    if (capDev) capDev.innerHTML = devHTML;
  };

  apply(0);
  if (!reducedMotion.matches && imgs.length > 1) {
    let i = 0;
    setInterval(() => {
      i = (i + 1) % N;
      if (rotMc) rotMc.classList.add('swap');
      if (rotDev) rotDev.classList.add('swap');
      setTimeout(() => {
        apply(i);
        if (rotMc) rotMc.classList.remove('swap');
        if (rotDev) rotDev.classList.remove('swap');
      }, 300);
    }, 3000);
  }
}

// Screens — one server switch flips every screen card at once, proving the
// layout is identical whatever the source. Auto-rotates; a tab click pins one.
const screensRail = document.querySelector('.screens-rail');
if (screensRail) {
  const tabs = Array.from(document.querySelectorAll('.mc-tab'));
  const SERVERS = ['plex', 'jellyfin', 'emby', 'stremio'];
  const NAMES = { plex: 'Plex', jellyfin: 'Jellyfin', emby: 'Emby', stremio: 'Stremio' };
  const setServer = (mc) => {
    screensRail.dataset.mc = mc;
    // Some screens don't exist on every server (e.g. Stremio has no seasons):
    // that card dims and says so, honestly, instead of faking it.
    screensRail.querySelectorAll('.scr').forEach((card) => {
      const has = card.querySelector('.scr-img[data-mc="' + mc + '"]');
      card.classList.toggle('na', !has);
      card.querySelectorAll('.scr-img').forEach((im) => im.classList.toggle('on', !!has && im.dataset.mc === mc));
      const badge = card.querySelector('.scr-na');
      if (badge) badge.textContent = has ? '' : '· not on ' + NAMES[mc];
    });
    tabs.forEach((t) => {
      const on = t.dataset.mc === mc;
      t.classList.toggle('is-on', on);
      t.setAttribute('aria-selected', on ? 'true' : 'false');
    });
  };
  let si = 0;
  let timer = null;
  const start = () => {
    if (reducedMotion.matches || timer) return;
    timer = setInterval(() => {
      si = (si + 1) % SERVERS.length;
      setServer(SERVERS[si]);
    }, 3400);
  };
  tabs.forEach((t) =>
    t.addEventListener('click', () => {
      if (timer) { clearInterval(timer); timer = null; } // a manual pick stops the carousel
      si = SERVERS.indexOf(t.dataset.mc);
      setServer(t.dataset.mc);
    })
  );
  start();
}

// Screenshot lightbox — .scr cards open whichever server is currently shown.
const lightbox = document.getElementById('lightbox');
if (lightbox) {
  const img = lightbox.querySelector('img');
  document.querySelectorAll('.shot, .scr').forEach((card) => {
    card.addEventListener('click', () => {
      const active = card.querySelector('.scr-img.on') || card.querySelector('img');
      img.src = card.dataset.full || active?.src || '';
      img.alt = active?.alt || '';
      lightbox.showModal();
    });
  });
  lightbox.addEventListener('click', (e) => {
    if (e.target === lightbox) lightbox.close();
  });
  lightbox.querySelector('.lightbox-close').addEventListener('click', () => lightbox.close());
}
