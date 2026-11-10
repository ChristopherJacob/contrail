// The showcase's animated previews (6d): each still with a clip gets a muted looping video over it as it nears the
// viewport. The still stays underneath, so with reduced motion, Save-Data, no autoplay or a missing file the page is
// the stills.
(() => {
  const spots = document.querySelectorAll(".clip > img[data-clip]");
  if (!spots.length || !("IntersectionObserver" in window)) return;
  if (matchMedia("(prefers-reduced-motion: reduce)").matches || navigator.connection?.saveData) return;

  let paused = false;
  const inView = new Set();
  const buttons = document.querySelectorAll("[data-motion]");
  const play = (v) => v.play().catch(() => {});

  function attach(img) {
    const v = document.createElement("video");
    v.muted = true;
    v.loop = true;
    v.playsInline = true;
    v.preload = "metadata";
    v.setAttribute("aria-hidden", "true");
    v.addEventListener("playing", () => v.classList.add("on"), { once: true });
    v.src = img.dataset.clip;
    img.after(v);
    return v;
  }

  const watcher = new IntersectionObserver((entries) => {
    for (const e of entries) {
      let v = e.target.nextElementSibling;
      if (e.isIntersecting) {
        v = v || attach(e.target);
        inView.add(v);
        if (!paused) play(v);
      } else if (v) {
        inView.delete(v);
        v.pause();
      }
    }
  }, { rootMargin: "200px" });
  spots.forEach((img) => watcher.observe(img));

  buttons.forEach((b) => {
    b.hidden = false;
    b.addEventListener("click", () => {
      paused = !paused;
      buttons.forEach((x) => { x.textContent = paused ? "Play animation" : "Pause animation"; });
      inView.forEach((v) => (paused ? v.pause() : play(v)));
    });
  });
})();
