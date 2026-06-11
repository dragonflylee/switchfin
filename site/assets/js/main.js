/* pleNx promo site — progressive enhancements only.
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

// Homepage nav: hidden until the hero spec box has scrolled past the top
const nav = document.querySelector('.nav');
const heroSpec = document.querySelector('.hero-spec');
if (nav && heroSpec) {
  const navObserver = new IntersectionObserver(([e]) => {
    const scrolledPast = !e.isIntersecting && e.boundingClientRect.bottom <= 0;
    nav.classList.toggle('nav-shown', scrolledPast);
  });
  navObserver.observe(heroSpec);
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
fetch('https://api.github.com/repos/thcolin/pleNx/releases/latest')
  .then((r) => (r.ok ? r.json() : null))
  .then((release) => {
    if (!release || !release.tag_name) return;
    document.querySelectorAll('[data-version]').forEach((el) => {
      el.textContent = release.tag_name;
    });
  })
  .catch(() => {});

// Screenshot lightbox
const lightbox = document.getElementById('lightbox');
if (lightbox) {
  const img = lightbox.querySelector('img');
  document.querySelectorAll('.shot').forEach((btn) => {
    btn.addEventListener('click', () => {
      img.src = btn.dataset.full;
      img.alt = btn.querySelector('img')?.alt || '';
      lightbox.showModal();
    });
  });
  lightbox.addEventListener('click', (e) => {
    // backdrop click: the dialog itself is the target only outside the image
    if (e.target === lightbox) lightbox.close();
  });
  lightbox.querySelector('.lightbox-close').addEventListener('click', () => lightbox.close());
}

// Smooth back-to-top (the Ⓑ button) — instant under reduced motion
const backTop = document.getElementById('back-top');
if (backTop) {
  backTop.addEventListener('click', (e) => {
    e.preventDefault();
    window.scrollTo({ top: 0, behavior: reducedMotion.matches ? 'auto' : 'smooth' });
  });
}
