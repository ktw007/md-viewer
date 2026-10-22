(function () {
  const article = document.getElementById("doc");
  const root = document.documentElement;
  const body = document.body;
  const main = document.getElementById("main");
  if (!article || !body || !main) return;

  const reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  const motion = reduced ? "auto" : "smooth";
  const sources = new WeakMap();

  function post(message) {
    if (window.chrome && window.chrome.webview) window.chrome.webview.postMessage(message);
  }

  function readJson(key) {
    try {
      const text = localStorage.getItem(key);
      return text ? JSON.parse(text) : null;
    } catch (error) {
      return null;
    }
  }

  function writeJson(key, value) {
    try {
      localStorage.setItem(key, JSON.stringify(value));
    } catch (error) {
      /* the page still works if storage is blocked */
    }
  }

  const defaults = {
    size: 17,
    sizeSet: false,
    measure: "mid",
    font: "serif",
    leading: "standard",
    theme: "system",
    side: 260,
    sideOpen: true,
    lines: false
  };
  const saved = readJson("mdv-reading");
  const settings = Object.assign({}, defaults, saved || {});
  if (!saved || saved.sideOpen == null) {
    settings.sideOpen = window.matchMedia("(min-width: 900px)").matches;
  }
  settings.side = Math.min(420, Math.max(200, Number(settings.side) || 260));
  settings.size = Math.min(21, Math.max(15, Number(settings.size) || 17));

  function saveSettings() {
    writeJson("mdv-reading", settings);
  }

  const themeBtn = document.getElementById("theme-toggle");
  let published = "";

  function resolvedTheme(choice) {
    if (choice === "paper" || choice === "ink" || choice === "mist") return choice;
    return root.dataset.system === "dark" ? "ink" : "paper";
  }

  function themeGlyph() {
    if (settings.theme === "system") return "随";
    return { paper: "纸", ink: "墨", mist: "雾" }[settings.theme] || "纸";
  }

  function applyTheme(choice, announce) {
    settings.theme = choice;
    const name = resolvedTheme(choice);
    root.dataset.theme = name;
    if (themeBtn) {
      themeBtn.textContent = themeGlyph();
      themeBtn.setAttribute("aria-label", "主题：" + themeGlyph());
    }
    if (announce !== false && published !== name) {
      published = name;
      post({ kind: "theme", name: name });
    }
  }

  function applyReading() {
    root.style.setProperty("--reading-size", settings.size + "px");
    const measures = { narrow: "600px", mid: "720px", wide: "860px" };
    root.style.setProperty("--measure", measures[settings.measure] || "720px");
    if (settings.sizeSet) root.dataset.sizeSet = "1";
    else delete root.dataset.sizeSet;
    body.dataset.font = settings.font === "sans" ? "sans" : "serif";
    body.dataset.leading = settings.leading || "standard";
    body.style.setProperty("--side", settings.sideOpen ? settings.side + "px" : "0px");
    body.classList.toggle("side-open", settings.sideOpen);
    body.classList.toggle("side-closed", !settings.sideOpen);
    const toggle = document.getElementById("nav-toggle");
    if (toggle) toggle.setAttribute("aria-expanded", settings.sideOpen ? "true" : "false");
    const resize = document.getElementById("side-resize");
    if (resize) resize.setAttribute("aria-valuenow", String(settings.side));
    applyTheme(settings.theme, true);
    syncScrim();
  }

  if (root.dataset.theme === "dark" || root.dataset.system === "dark") root.dataset.system = "dark";
  else root.dataset.system = "light";
  let scrim = null;
  applyReading();

  function docKey() {
    return body.dataset.path || document.title || "welcome";
  }

  function headingText(heading) {
    const clone = heading.cloneNode(true);
    clone.querySelectorAll(".anchor").forEach(function (node) { node.remove(); });
    return (clone.textContent || "").replace(/\s+/g, " ").trim();
  }

  function maxScroll() {
    return Math.max(0, main.scrollHeight - main.clientHeight);
  }

  const progress = document.getElementById("progress");
  function paintProgress() {
    if (!progress) return;
    const max = maxScroll();
    const ratio = max > 0 ? main.scrollTop / max : 0;
    progress.style.width = (ratio * 100) + "%";
  }

  let settling = true;
  let quiet = false;
  let userMoved = false;
  let scrollTimer = 0;
  function reportScroll() {
    if (settling || quiet) return;
    clearTimeout(scrollTimer);
    scrollTimer = setTimeout(function () {
      const max = maxScroll();
      post({ kind: "scroll", y: max > 0 ? main.scrollTop / max : 0 });
    }, 180);
  }

  let resumeBtn = null;
  function restorePosition() {
    const anchor = body.dataset.anchor;
    if (anchor) {
      const target = document.getElementById(anchor);
      if (target) {
        quiet = true;
        target.scrollIntoView({ block: "start" });
        quiet = false;
        return;
      }
    }
    const ratio = parseFloat(body.dataset.scroll || "0");
    if (!(ratio > 0)) return;
    if (body.dataset.resume === "force") {
      quiet = true;
      main.scrollTop = maxScroll() * ratio;
      quiet = false;
      return;
    }
    if (ratio <= 0.02 || resumeBtn) return;
    resumeBtn = document.createElement("button");
    resumeBtn.type = "button";
    resumeBtn.className = "resume";
    resumeBtn.textContent = "回到上次阅读处";
    main.insertBefore(resumeBtn, article);
    resumeBtn.addEventListener("click", function () {
      quiet = true;
      main.scrollTop = maxScroll() * ratio;
      quiet = false;
      resumeBtn.remove();
      resumeBtn = null;
      reportScroll();
    });
  }

  main.addEventListener("scroll", function () {
    paintProgress();
    if (!settling && !quiet) userMoved = true;
    if (resumeBtn && main.scrollTop > 80) {
      resumeBtn.remove();
      resumeBtn = null;
    }
    hideBarOnScroll();
    spy();
    reportScroll();
  }, { passive: true });

  let lastTop = 0;
  function hideBarOnScroll() {
    if (!window.matchMedia("(max-width: 899px)").matches) {
      body.classList.remove("bar-away");
      return;
    }
    const top = main.scrollTop;
    if (top > lastTop + 8 && top > 80) body.classList.add("bar-away");
    else if (top < lastTop - 8) body.classList.remove("bar-away");
    lastTop = top;
  }

  function finishSettle() {
    restorePosition();
    settling = false;
    paintProgress();
    history.replaceState({ top: main.scrollTop }, "");
    post({ kind: "ready" });
  }

  window.addEventListener("load", function () {
    restorePosition();
    setTimeout(restorePosition, 80);
    setTimeout(finishSettle, 280);
  });
  article.querySelectorAll("img").forEach(function (img) {
    img.addEventListener("load", function () {
      if (body.dataset.resume === "force" && !userMoved) restorePosition();
    });
  });

  function jump(target) {
    if (!target) return;
    history.pushState({ top: main.scrollTop }, "");
    target.scrollIntoView({ block: "start", behavior: motion });
  }

  window.addEventListener("popstate", function (event) {
    const top = event.state && typeof event.state.top === "number" ? event.state.top : 0;
    quiet = true;
    main.scrollTo({ top: top, behavior: motion });
    quiet = false;
  });

  const h1 = article.querySelector("h1");
  function countUnits(text) {
    const cjk = (text.match(/[\u3400-\u9fff]/g) || []).length;
    const latin = (text.replace(/[\u3400-\u9fff]/g, " ").match(/[A-Za-z0-9]+/g) || []).length;
    return cjk + latin;
  }
  if (h1 && !body.dataset.welcome) {
    const count = countUnits(article.innerText || "");
    const minutes = Math.max(1, Math.round(count / 400));
    const meta = document.createElement("p");
    meta.className = "doc-meta";
    const bits = [];
    if (body.dataset.modified) bits.push(body.dataset.modified);
    bits.push(count.toLocaleString("zh-CN") + " 字");
    bits.push("约 " + minutes + " 分钟");
    bits.forEach(function (bit) {
      const span = document.createElement("span");
      span.textContent = bit;
      meta.appendChild(span);
    });
    h1.insertAdjacentElement("afterend", meta);
  }

  const crumbFile = document.getElementById("crumb-file");
  const crumbTitle = document.getElementById("crumb-title");
  if (crumbTitle) {
    const title = h1 ? headingText(h1) : (document.title || "阅读");
    crumbTitle.textContent = body.dataset.welcome ? "阅读" : title;
    if (crumbFile) {
      const file = body.dataset.file || "";
      if (file && file !== title) crumbFile.textContent = file;
      else crumbFile.hidden = true;
    }
  }

  if ((article.textContent || "").length > 50000) article.classList.add("long-doc");

  const panelOutline = document.getElementById("panel-outline");
  const panelFiles = document.getElementById("panel-files");
  const headings = Array.prototype.slice.call(article.querySelectorAll("h1, h2, h3, h4, h5, h6"));
  const outlineLinks = [];
  if (panelOutline) {
    if (!headings.length) {
      const empty = document.createElement("p");
      empty.className = "side-empty";
      empty.textContent = "这篇文档没有章节";
      panelOutline.appendChild(empty);
    } else {
      let owner = "";
      headings.forEach(function (heading) {
        if (!heading.id) return;
        const level = heading.tagName.toLowerCase();
        if (level === "h1" || level === "h2") owner = heading.id;
        const link = document.createElement("a");
        link.href = "#" + heading.id;
        link.textContent = headingText(heading);
        link.title = link.textContent;
        link.dataset.level = level;
        link.dataset.owner = owner;
        if (level !== "h1" && level !== "h2") link.hidden = true;
        link.addEventListener("click", function (event) {
          event.preventDefault();
          jump(heading);
          if (window.matchMedia("(max-width: 899px)").matches) closeSide();
        });
        panelOutline.appendChild(link);
        outlineLinks.push(link);
      });
    }
  }

  let spyFrame = 0;
  function spy() {
    if (spyFrame) return;
    spyFrame = requestAnimationFrame(function () {
      spyFrame = 0;
      if (!headings.length) return;
      const line = main.getBoundingClientRect().top + 28;
      let current = headings[0];
      headings.forEach(function (heading) {
        if (heading.getBoundingClientRect().top <= line) current = heading;
      });
      const id = current.id;
      outlineLinks.forEach(function (link) { link.classList.remove("is-current"); });
      const active = panelOutline && panelOutline.querySelector('a[href="#' + CSS.escape(id) + '"]');
      let owner = id;
      if (active) {
        active.classList.add("is-current");
        owner = active.dataset.owner || id;
        const panel = panelOutline.parentElement;
        if (panel && active.offsetTop < panel.scrollTop) panel.scrollTop = active.offsetTop;
        if (panel && active.offsetTop + active.offsetHeight > panel.scrollTop + panel.clientHeight) {
          panel.scrollTop = active.offsetTop - panel.clientHeight + active.offsetHeight;
        }
      }
      outlineLinks.forEach(function (link) {
        const deep = link.dataset.level !== "h1" && link.dataset.level !== "h2";
        if (deep) link.hidden = link.dataset.owner !== owner;
      });
    });
  }

  function readRecent() {
    const list = readJson("mdv-recent");
    return Array.isArray(list) ? list : [];
  }

  function rememberRecent() {
    const path = body.dataset.path;
    if (!path) return;
    const title = h1 ? headingText(h1) : (body.dataset.file || path);
    const next = readRecent().filter(function (item) { return item.path !== path; });
    next.unshift({ path: path, title: title });
    writeJson("mdv-recent", next.slice(0, 8));
  }
  rememberRecent();

  function fileButton(item) {
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = item.title || item.path;
    button.title = item.path;
    button.addEventListener("click", function () { post({ kind: "open-path", path: item.path }); });
    return button;
  }

  function renderFiles() {
    if (!panelFiles) return;
    panelFiles.textContent = "";
    const open = document.createElement("button");
    open.type = "button";
    open.className = "file-open";
    open.textContent = "打开文件";
    open.addEventListener("click", function () { post({ kind: "open" }); });
    panelFiles.appendChild(open);
    const path = body.dataset.path || "";
    if (path) {
      const name = document.createElement("p");
      name.className = "file-name";
      name.textContent = body.dataset.file || path;
      const full = document.createElement("p");
      full.className = "file-path";
      full.textContent = path;
      panelFiles.append(name, full);
    }
    const recent = readRecent().filter(function (item) { return item.path !== path; });
    if (!recent.length) return;
    const label = document.createElement("p");
    label.className = "recent-label";
    label.textContent = "最近打开";
    const list = document.createElement("ul");
    recent.forEach(function (item) {
      const li = document.createElement("li");
      li.appendChild(fileButton(item));
      list.appendChild(li);
    });
    panelFiles.append(label, list);
  }
  renderFiles();

  const welcomeRecent = document.getElementById("recent");
  if (welcomeRecent) {
    const list = welcomeRecent.querySelector("ul");
    const recent = readRecent();
    if (list && recent.length) {
      recent.forEach(function (item) {
        const li = document.createElement("li");
        li.appendChild(fileButton(item));
        list.appendChild(li);
      });
      welcomeRecent.hidden = false;
    }
  }
  const emptyPick = document.getElementById("empty-pick");
  if (emptyPick) emptyPick.addEventListener("click", function () { post({ kind: "open" }); });

  function showTab(name) {
    const outline = name === "outline";
    if (panelOutline) panelOutline.hidden = !outline;
    if (panelFiles) panelFiles.hidden = outline;
    const tabOutline = document.getElementById("tab-outline");
    const tabFiles = document.getElementById("tab-files");
    if (tabOutline) tabOutline.setAttribute("aria-selected", outline ? "true" : "false");
    if (tabFiles) tabFiles.setAttribute("aria-selected", outline ? "false" : "true");
  }
  const tabOutline = document.getElementById("tab-outline");
  const tabFiles = document.getElementById("tab-files");
  if (tabOutline) tabOutline.addEventListener("click", function () { showTab("outline"); });
  if (tabFiles) tabFiles.addEventListener("click", function () { showTab("files"); });

  scrim = document.createElement("div");
  scrim.className = "scrim";
  scrim.hidden = true;
  body.appendChild(scrim);
  syncScrim();
  function syncScrim() {
    if (!scrim) return;
    const mobileSide = window.matchMedia("(max-width: 899px)").matches && settings.sideOpen;
    const drawerRail = window.matchMedia("(max-width: 1279px)").matches && body.classList.contains("rail-open");
    scrim.hidden = !(mobileSide || drawerRail);
  }
  function closeSide() {
    settings.sideOpen = false;
    saveSettings();
    applyReading();
  }
  scrim.addEventListener("click", function () {
    if (window.matchMedia("(max-width: 899px)").matches) closeSide();
    setRail(false);
  });
  const navToggle = document.getElementById("nav-toggle");
  if (navToggle) {
    navToggle.addEventListener("click", function () {
      settings.sideOpen = !settings.sideOpen;
      saveSettings();
      applyReading();
    });
  }
  window.mdvOutline = function () {
    settings.sideOpen = !settings.sideOpen;
    saveSettings();
    applyReading();
  };

  const resize = document.getElementById("side-resize");
  if (resize) {
    resize.setAttribute("aria-valuemin", "200");
    resize.setAttribute("aria-valuemax", "420");
    resize.addEventListener("pointerdown", function (event) {
      if (window.matchMedia("(max-width: 899px)").matches) return;
      resize.setPointerCapture(event.pointerId);
      const startX = event.clientX;
      const start = settings.side;
      function move(e) {
        settings.side = Math.min(420, Math.max(200, start + e.clientX - startX));
        settings.sideOpen = true;
        applyReading();
      }
      function up() {
        resize.removeEventListener("pointermove", move);
        resize.removeEventListener("pointerup", up);
        saveSettings();
      }
      resize.addEventListener("pointermove", move);
      resize.addEventListener("pointerup", up);
      event.preventDefault();
    });
    resize.addEventListener("keydown", function (event) {
      if (event.key !== "ArrowLeft" && event.key !== "ArrowRight" && event.key !== "Home") return;
      event.preventDefault();
      if (event.key === "Home") settings.side = 260;
      else settings.side = Math.min(420, Math.max(200, settings.side + (event.key === "ArrowRight" ? 16 : -16)));
      settings.sideOpen = true;
      saveSettings();
      applyReading();
    });
  }
  window.addEventListener("resize", function () {
    syncScrim();
    article.querySelectorAll(".table-frame").forEach(updateFades);
  });
  document.fonts.ready.then(function () {
    article.querySelectorAll(".table-frame").forEach(updateFades);
  });

  function makeButton(label) {
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = label;
    return button;
  }

  function copyText(text) {
    if (navigator.clipboard && navigator.clipboard.writeText) {
      return navigator.clipboard.writeText(text).catch(function () { return copyFallback(text); });
    }
    return copyFallback(text);
  }
  function copyFallback(text) {
    const area = document.createElement("textarea");
    area.value = text;
    body.appendChild(area);
    area.select();
    try { document.execCommand("copy"); } catch (error) { /* ignore */ }
    area.remove();
    return Promise.resolve();
  }

  let toastTimer = 0;
  function toast(text) {
    let node = document.querySelector(".toast");
    if (!node) {
      node = document.createElement("div");
      node.className = "toast";
      node.setAttribute("role", "status");
      body.appendChild(node);
    }
    node.textContent = text;
    node.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { node.hidden = true; }, 1500);
  }

  const keywords = {
    cpp: "alignas auto bool break case catch char class const constexpr continue default delete do double else enum explicit export extern false float for friend goto if inline int long namespace new noexcept nullptr operator private protected public return short signed sizeof static struct switch template this throw true try typedef typeid typename union unsigned using virtual void volatile while",
    c: "auto break case char const continue default do double else enum extern float for goto if inline int long register return short signed sizeof static struct switch typedef union unsigned void volatile while",
    js: "async await break case catch class const continue default else export extends false finally for function if import let new null return static super switch this throw true try typeof var void while yield",
    ts: "async await break case catch class const continue default else export extends false finally for function if implements import interface let new null private protected public return static super switch this throw true try type typeof var void while yield",
    py: "and as assert async await break class continue def del elif else except false finally for from global if import in is lambda none nonlocal not or pass raise return true try while with yield",
    rs: "as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move mut pub ref return self static struct super trait true type unsafe use where while",
    go: "break case chan const continue default defer else fallthrough for func go goto if import interface map package range return select struct switch type var",
    sh: "if then else elif fi for while do done case esac function return in"
  };
  const languageNames = {
    cpp: "C++", c: "C", js: "JavaScript", ts: "TypeScript", py: "Python", rs: "Rust", go: "Go", sh: "Shell"
  };

  function rawLanguage(element) {
    const match = /language-([\w#+.-]+)/.exec(element.className || "");
    return match ? match[1] : "";
  }
  function languageKey(name) {
    const value = (name || "").toLowerCase();
    if (value === "c++" || value === "cc" || value === "cxx" || value === "hpp") return "cpp";
    if (value === "javascript" || value === "jsx") return "js";
    if (value === "typescript" || value === "tsx") return "ts";
    if (value === "python") return "py";
    if (value === "rust") return "rs";
    if (value === "golang") return "go";
    if (value === "bash" || value === "shell" || value === "zsh") return "sh";
    return keywords[value] ? value : "";
  }
  function cjkRatio(text) {
    let cjk = 0;
    let count = 0;
    for (const char of text) {
      if (char === "\n" || char === " " || char === "\t") continue;
      count += 1;
      const code = char.codePointAt(0);
      if (code >= 0x3400 && code <= 0x9fff) cjk += 1;
    }
    return count ? cjk / count : 0;
  }
  function escapeHtml(text) {
    return text.replace(/[&<>]/g, function (c) {
      return c === "&" ? "&amp;" : c === "<" ? "&lt;" : "&gt;";
    });
  }
  function highlightLine(source, words) {
    const set = words ? new Set(words.split(" ")) : null;
    let html = "";
    let i = 0;
    while (i < source.length) {
      if (source.startsWith("//", i) || (source[i] === "#" && i === 0)) {
        html += '<span class="tok-c">' + escapeHtml(source.slice(i)) + "</span>";
        break;
      }
      const quote = source[i];
      if (quote === '"' || quote === "'" || quote === "`") {
        let j = i + 1;
        while (j < source.length) {
          if (source[j] === "\\") { j += 2; continue; }
          if (source[j] === quote) { j += 1; break; }
          j += 1;
        }
        html += '<span class="tok-s">' + escapeHtml(source.slice(i, j)) + "</span>";
        i = j;
        continue;
      }
      if (/[0-9]/.test(source[i]) && (i === 0 || !/[A-Za-z_]/.test(source[i - 1]))) {
        let j = i + 1;
        while (j < source.length && /[0-9.]/.test(source[j])) j += 1;
        html += '<span class="tok-n">' + escapeHtml(source.slice(i, j)) + "</span>";
        i = j;
        continue;
      }
      if (set && /[A-Za-z_]/.test(source[i])) {
        let j = i + 1;
        while (j < source.length && /[A-Za-z0-9_]/.test(source[j])) j += 1;
        const word = source.slice(i, j);
        html += set.has(word) ? '<span class="tok-k">' + escapeHtml(word) + "</span>" : escapeHtml(word);
        i = j;
        continue;
      }
      html += escapeHtml(source[i]);
      i += 1;
    }
    return html;
  }

  function renderCode(code, source, withLines) {
    const key = languageKey(rawLanguage(code));
    const words = keywords[key];
    const lines = source.replace(/\n$/, "").split("\n");
    code.textContent = "";
    lines.forEach(function (line) {
      const span = document.createElement("span");
      span.className = "line";
      span.innerHTML = highlightLine(line, words) || " ";
      code.appendChild(span);
    });
    code.classList.toggle("has-lines", withLines);
  }

  function renderProse(code, source) {
    const lines = source.replace(/\n$/, "").split("\n");
    code.textContent = "";
    lines.forEach(function (line) {
      if (!line.length) {
        const gap = document.createElement("span");
        gap.className = "gap";
        code.appendChild(gap);
      } else {
        code.appendChild(document.createTextNode(line + "\n"));
      }
    });
  }

  article.querySelectorAll("pre").forEach(function (pre) {
    if (pre.classList.contains("raw-fallback") || pre.closest(".block")) return;
    const code = pre.querySelector("code") || pre;
    const source = code.textContent || "";
    sources.set(pre, source);
    const prose = !rawLanguage(code) || cjkRatio(source) > 0.3;
    const figure = document.createElement("figure");
    figure.className = "block " + (prose ? "block-text" : "block-code");
    const bar = document.createElement("div");
    bar.className = "block-bar";
    const label = document.createElement("span");
    label.className = "block-label";
    const key = languageKey(rawLanguage(code));
    label.textContent = prose ? "文本" : (languageNames[key] || rawLanguage(code) || "代码");
    const actions = document.createElement("div");
    actions.className = "block-actions";
    const copyBtn = makeButton("复制");
    const wrapBtn = makeButton("换行");
    const lineBtn = makeButton("行号");
    const foldBtn = makeButton("折叠");
    actions.append(copyBtn, wrapBtn);
    if (!prose) actions.appendChild(lineBtn);
    actions.appendChild(foldBtn);
    bar.append(label, actions);
    const blockBody = document.createElement("div");
    blockBody.className = "block-body";
    const more = makeButton("展开全部");
    more.className = "block-more";
    more.hidden = true;
    pre.parentNode.insertBefore(figure, pre);
    figure.append(bar, blockBody, more);
    blockBody.appendChild(pre);
    if (prose) {
      renderProse(code, source);
      figure.classList.add("is-wrap");
      wrapBtn.setAttribute("aria-pressed", "true");
    } else if (source.length <= 80000) {
      renderCode(code, source, false);
    }
    copyBtn.addEventListener("click", function () {
      copyText(sources.get(pre) || "").then(function () {
        copyBtn.textContent = "已复制 ✓";
        setTimeout(function () { copyBtn.textContent = "复制"; }, 1500);
      });
    });
    wrapBtn.addEventListener("click", function () {
      const on = figure.classList.toggle("is-wrap");
      wrapBtn.setAttribute("aria-pressed", on ? "true" : "false");
    });
    lineBtn.addEventListener("click", function () {
      const on = !code.classList.contains("has-lines");
      renderCode(code, source, on);
      lineBtn.setAttribute("aria-pressed", on ? "true" : "false");
    });
    const lineCount = source.length ? source.replace(/\n$/, "").split("\n").length : 0;
    function setFolded(folded) {
      blockBody.classList.toggle("is-folded", folded);
      foldBtn.textContent = folded ? "展开" : "折叠";
      more.hidden = !folded;
    }
    if (lineCount > 24) {
      setFolded(true);
      foldBtn.addEventListener("click", function () { setFolded(!blockBody.classList.contains("is-folded")); });
      more.addEventListener("click", function () { setFolded(false); });
    } else {
      foldBtn.hidden = true;
    }
  });

  function statusDot(on) {
    const span = document.createElement("span");
    span.className = on ? "status status-on" : "status status-off";
    span.setAttribute("role", "img");
    span.setAttribute("aria-label", on ? "启用" : "未启用");
    return span;
  }
  function replaceStatus(rootNode) {
    const nodes = [];
    const walker = document.createTreeWalker(rootNode, NodeFilter.SHOW_TEXT);
    while (walker.nextNode()) nodes.push(walker.currentNode);
    nodes.forEach(function (node) {
      if (!node.textContent || !/[●○]/.test(node.textContent)) return;
      const frag = document.createDocumentFragment();
      node.textContent.split(/([●○])/).forEach(function (part) {
        if (part === "●") frag.appendChild(statusDot(true));
        else if (part === "○") frag.appendChild(statusDot(false));
        else if (part) frag.appendChild(document.createTextNode(part));
      });
      node.parentNode.replaceChild(frag, node);
    });
  }
  function updateFades(frame) {
    const scroller = frame.querySelector(".table-scroll");
    if (!scroller) return;
    const max = scroller.scrollWidth - scroller.clientWidth;
    frame.classList.toggle("show-left", scroller.scrollLeft > 2);
    frame.classList.toggle("show-right", max - scroller.scrollLeft > 2);
  }
  article.querySelectorAll("table").forEach(function (table) {
    if (table.closest(".table-frame")) return;
    const frame = document.createElement("div");
    frame.className = "table-frame";
    const left = document.createElement("div");
    left.className = "table-fade table-fade-left";
    const right = document.createElement("div");
    right.className = "table-fade table-fade-right";
    const scroller = document.createElement("div");
    scroller.className = "table-scroll";
    table.parentNode.insertBefore(frame, table);
    frame.append(scroller, left, right);
    scroller.appendChild(table);
    replaceStatus(table);
    scroller.addEventListener("scroll", function () { updateFades(frame); }, { passive: true });
    updateFades(frame);
  });

  const admonitionNames = { note: "注", tip: "提示", important: "重要", warning: "警告", caution: "小心" };
  const admonitionIcon = '<svg width="14" height="14" viewBox="0 0 16 16" aria-hidden="true"><circle cx="8" cy="8" r="6.25" fill="none" stroke="currentColor" stroke-width="1.3"/><path d="M8 7.2v4" stroke="currentColor" stroke-width="1.3"/><circle cx="8" cy="5" r="0.7" fill="currentColor"/></svg>';
  article.querySelectorAll("blockquote").forEach(function (quote) {
    const walker = document.createTreeWalker(quote, NodeFilter.SHOW_TEXT);
    let node = walker.nextNode();
    while (node && !/\S/.test(node.textContent || "")) node = walker.nextNode();
    if (!node) return;
    const match = /^\s*\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\]\s*/i.exec(node.textContent || "");
    if (!match) return;
    node.textContent = node.textContent.slice(match[0].length);
    const kind = match[1].toLowerCase();
    quote.classList.add("admonition");
    const title = document.createElement("p");
    title.className = "admonition-title";
    title.innerHTML = admonitionIcon;
    title.appendChild(document.createTextNode(admonitionNames[kind] || kind));
    quote.insertBefore(title, quote.firstChild);
  });

  const lightbox = document.createElement("div");
  lightbox.className = "lightbox";
  lightbox.hidden = true;
  lightbox.setAttribute("role", "dialog");
  lightbox.setAttribute("aria-modal", "true");
  lightbox.setAttribute("aria-label", "图片");
  const lightboxClose = makeButton("关闭");
  lightboxClose.className = "lightbox-close";
  const lightboxImg = document.createElement("img");
  lightbox.append(lightboxClose, lightboxImg);
  body.appendChild(lightbox);
  let lightboxReturn = null;
  function closeLightbox() {
    lightbox.hidden = true;
    lightboxImg.removeAttribute("src");
    if (lightboxReturn) lightboxReturn.focus();
  }
  lightboxClose.addEventListener("click", closeLightbox);
  lightbox.addEventListener("click", function (event) {
    if (event.target === lightbox) closeLightbox();
  });

  article.querySelectorAll("img").forEach(function (img) {
    const width = img.getAttribute("width");
    const height = img.getAttribute("height");
    if (width && height) img.style.aspectRatio = width + " / " + height;
    function loaded() { img.dataset.loaded = "1"; }
    if (img.complete && img.naturalWidth) loaded();
    img.addEventListener("load", loaded);
    img.addEventListener("error", function () {
      if (img.dataset.failed) return;
      img.dataset.failed = "1";
      const box = document.createElement("div");
      box.className = "img-failed";
      const text = document.createElement("p");
      text.textContent = img.alt || "图片没有加载";
      const retry = makeButton("重试");
      retry.addEventListener("click", function () {
        const src = img.getAttribute("src");
        img.dataset.failed = "";
        delete img.dataset.loaded;
        box.replaceWith(img);
        img.src = src;
      });
      box.append(text, retry);
      img.replaceWith(box);
    });
    if (img.closest("a")) return;
    img.addEventListener("click", function () {
      lightboxReturn = img;
      lightboxImg.src = img.currentSrc || img.src;
      lightboxImg.alt = img.alt || "";
      lightbox.hidden = false;
      lightboxClose.focus();
    });
  });

  const footPop = document.createElement("div");
  footPop.className = "footnote-pop";
  footPop.hidden = true;
  body.appendChild(footPop);
  article.querySelectorAll('a[href^="#"]').forEach(function (link) {
    const id = decodeURIComponent(link.getAttribute("href").slice(1));
    const target = document.getElementById(id);
    if (!target) return;
    if (/^fn/.test(id) || target.closest(".footnotes, section.footnotes")) {
      link.addEventListener("mouseenter", function () {
        footPop.textContent = (target.textContent || "").trim();
        const rect = link.getBoundingClientRect();
        footPop.hidden = false;
        footPop.style.left = Math.min(rect.left, window.innerWidth - 360) + "px";
        footPop.style.top = (rect.bottom + 6) + "px";
      });
      link.addEventListener("mouseleave", function () { footPop.hidden = true; });
    }
  });
  article.addEventListener("click", function (event) {
    const link = event.target.closest('a[href^="#"]');
    if (!link || !article.contains(link)) return;
    const id = decodeURIComponent(link.getAttribute("href").slice(1));
    const target = document.getElementById(id);
    if (!target) return;
    event.preventDefault();
    jump(target);
    target.classList.add("flash");
    setTimeout(function () { target.classList.remove("flash"); }, 1500);
  });

  article.querySelectorAll("h2, h3, h4, h5, h6").forEach(function (heading) {
    if (!heading.id) return;
    const button = makeButton("#");
    button.className = "anchor";
    button.setAttribute("aria-label", "复制章节链接");
    button.addEventListener("click", function (event) {
      event.preventDefault();
      copyText("#" + heading.id).then(function () { toast("已复制链接"); });
    });
    heading.appendChild(button);
  });

  const canHighlight = window.CSS && CSS.highlights && typeof Highlight === "function";
  const findBar = document.createElement("form");
  findBar.className = "find";
  findBar.hidden = true;
  findBar.setAttribute("role", "search");
  const findInput = document.createElement("input");
  findInput.type = "search";
  findInput.placeholder = "查找";
  findInput.setAttribute("aria-label", "查找");
  findInput.autocomplete = "off";
  findInput.spellcheck = false;
  const findCount = document.createElement("span");
  const findPrev = makeButton("↑");
  findPrev.setAttribute("aria-label", "上一处");
  const findNext = makeButton("↓");
  findNext.setAttribute("aria-label", "下一处");
  findBar.append(findInput, findCount, findPrev, findNext);
  body.appendChild(findBar);
  const ranges = [];
  let active = 0;
  function clearMarks() {
    if (canHighlight) {
      CSS.highlights.delete("find");
      CSS.highlights.delete("find-current");
    }
    ranges.length = 0;
    findCount.textContent = "";
  }
  function paintFind() {
    if (!ranges.length) {
      findCount.textContent = findInput.value ? "未找到" : "";
      if (canHighlight) {
        CSS.highlights.delete("find");
        CSS.highlights.delete("find-current");
      }
      return;
    }
    if (active < 0) active = ranges.length - 1;
    if (active >= ranges.length) active = 0;
    findCount.textContent = (active + 1) + " / " + ranges.length;
    if (canHighlight) {
      const rest = ranges.filter(function (_, index) { return index !== active; });
      putHighlight("find", rest);
      putHighlight("find-current", [ranges[active]]);
    }
    const node = ranges[active].startContainer.parentElement;
    if (node) node.scrollIntoView({ block: "center", behavior: motion });
  }
  function putHighlight(name, list) {
    const highlight = new Highlight();
    list.forEach(function (range) { highlight.add(range); });
    CSS.highlights.set(name, highlight);
  }
  function searchFind(text) {
    clearMarks();
    if (!text) { paintFind(); return; }
    const needle = text.toLowerCase();
    const walker = document.createTreeWalker(article, NodeFilter.SHOW_TEXT);
    let node = walker.nextNode();
    while (node) {
      const hay = node.textContent.toLowerCase();
      let from = 0;
      while (from < hay.length) {
        const at = hay.indexOf(needle, from);
        if (at < 0) break;
        const range = document.createRange();
        range.setStart(node, at);
        range.setEnd(node, at + needle.length);
        ranges.push(range);
        from = at + needle.length;
      }
      node = walker.nextNode();
    }
    active = 0;
    paintFind();
  }
  function openFind() {
    closePalette();
    findBar.hidden = false;
    findInput.focus();
    findInput.select();
  }
  window.mdvFind = openFind;
  findInput.addEventListener("input", function () { searchFind(findInput.value.trim()); });
  findNext.addEventListener("click", function () { active += 1; paintFind(); });
  findPrev.addEventListener("click", function () { active -= 1; paintFind(); });
  findBar.addEventListener("submit", function (event) { event.preventDefault(); active += 1; paintFind(); });
  findInput.addEventListener("keydown", function (event) {
    if (event.key === "Enter") {
      event.preventDefault();
      active += event.shiftKey ? -1 : 1;
      paintFind();
    } else if (event.key === "Escape") {
      event.preventDefault();
      findBar.hidden = true;
      clearMarks();
    }
  });

  const palette = document.createElement("div");
  palette.className = "palette";
  palette.hidden = true;
  const paletteCard = document.createElement("div");
  paletteCard.className = "palette-card";
  paletteCard.setAttribute("role", "dialog");
  paletteCard.setAttribute("aria-modal", "true");
  paletteCard.setAttribute("aria-label", "搜索");
  const paletteInput = document.createElement("input");
  paletteInput.type = "search";
  paletteInput.placeholder = "搜索标题和正文";
  paletteInput.setAttribute("aria-label", "搜索标题和正文");
  paletteInput.autocomplete = "off";
  paletteInput.spellcheck = false;
  const paletteList = document.createElement("div");
  paletteList.className = "palette-list";
  paletteList.setAttribute("role", "listbox");
  const paletteEmpty = document.createElement("p");
  paletteEmpty.className = "palette-empty";
  paletteEmpty.hidden = true;
  paletteCard.append(paletteInput, paletteList, paletteEmpty);
  palette.appendChild(paletteCard);
  body.appendChild(palette);
  let paletteItems = [];
  let paletteIndex = 0;
  function closePalette() { palette.hidden = true; }
  function renderPalette(items, query) {
    paletteList.textContent = "";
    paletteItems = items;
    paletteIndex = 0;
    paletteEmpty.hidden = items.length > 0 || !query;
    if (!items.length && query) {
      paletteEmpty.textContent = "未找到「" + query + "」。试试更短的词，或清空搜索。";
    }
    items.forEach(function (item, index) {
      const button = document.createElement("button");
      button.type = "button";
      button.setAttribute("role", "option");
      const kind = document.createElement("span");
      kind.className = "palette-kind";
      kind.textContent = item.kind === "heading" ? "标题" : "正文";
      const text = document.createElement("span");
      text.textContent = item.text;
      button.append(kind, text);
      button.addEventListener("click", function () { choosePalette(index); });
      paletteList.appendChild(button);
    });
    markPalette();
  }
  function markPalette() {
    const buttons = paletteList.querySelectorAll("button");
    buttons.forEach(function (button, index) {
      button.setAttribute("aria-selected", index === paletteIndex ? "true" : "false");
      if (index === paletteIndex) button.scrollIntoView({ block: "nearest" });
    });
  }
  function choosePalette(index) {
    const item = paletteItems[index];
    if (!item) return;
    closePalette();
    if (item.kind === "heading") jump(item.el);
    else if (item.node) {
      const range = document.createRange();
      range.setStart(item.node, item.at);
      range.setEnd(item.node, item.at + item.length);
      if (canHighlight) CSS.highlights.set("find-current", new Highlight(range));
      const parent = item.node.parentElement;
      if (parent) parent.scrollIntoView({ block: "center", behavior: motion });
    }
  }
  function collectPalette(query) {
    const q = query.trim().toLowerCase();
    const items = [];
    headings.forEach(function (heading) {
      const text = headingText(heading);
      if (!q || text.toLowerCase().includes(q)) items.push({ kind: "heading", text: text, el: heading });
    });
    if (!q) return items.slice(0, 20);
    const walker = document.createTreeWalker(article, NodeFilter.SHOW_TEXT);
    let node = walker.nextNode();
    let hits = 0;
    while (node && hits < 30) {
      if (node.parentElement && node.parentElement.closest("h1, h2, h3, h4, h5, h6, .anchor")) {
        node = walker.nextNode();
        continue;
      }
      const hay = node.textContent || "";
      const at = hay.toLowerCase().indexOf(q);
      if (at >= 0) {
        const start = Math.max(0, at - 20);
        const end = Math.min(hay.length, at + q.length + 20);
        let text = hay.slice(start, end).replace(/\s+/g, " ").trim();
        if (start > 0) text = "…" + text;
        if (end < hay.length) text += "…";
        items.push({ kind: "hit", text: text, node: node, at: at, length: q.length });
        hits += 1;
      }
      node = walker.nextNode();
    }
    return items;
  }
  function openPalette() {
    palette.hidden = false;
    paletteInput.value = "";
    renderPalette(collectPalette(""), "");
    paletteInput.focus();
  }
  window.mdvPalette = function () {
    if (palette.hidden) openPalette();
    else closePalette();
  };
  const cmdk = document.getElementById("cmdk");
  if (cmdk) cmdk.addEventListener("click", openPalette);
  palette.addEventListener("click", function (event) { if (event.target === palette) closePalette(); });
  paletteInput.addEventListener("input", function () {
    renderPalette(collectPalette(paletteInput.value), paletteInput.value.trim());
  });
  paletteInput.addEventListener("keydown", function (event) {
    if (event.key === "ArrowDown") {
      event.preventDefault();
      paletteIndex = Math.min(paletteItems.length - 1, paletteIndex + 1);
      markPalette();
    } else if (event.key === "ArrowUp") {
      event.preventDefault();
      paletteIndex = Math.max(0, paletteIndex - 1);
      markPalette();
    } else if (event.key === "Enter") {
      event.preventDefault();
      choosePalette(paletteIndex);
    } else if (event.key === "Escape") {
      event.preventDefault();
      closePalette();
    }
  });

  const readBtn = document.getElementById("read-settings");
  const popover = document.createElement("div");
  popover.className = "popover";
  popover.hidden = true;
  popover.setAttribute("role", "dialog");
  popover.setAttribute("aria-label", "阅读设置");
  const popTitle = document.createElement("h2");
  popTitle.textContent = "阅读设置";
  popover.appendChild(popTitle);
  function choiceRow(label, key, options) {
    const row = document.createElement("div");
    row.className = "setting";
    const name = document.createElement("span");
    name.textContent = label;
    const choices = document.createElement("div");
    choices.className = "choices";
    options.forEach(function (option) {
      const button = makeButton(option.label);
      button.dataset.value = option.value;
      button.addEventListener("click", function () {
        settings[key] = option.value;
        if (key === "theme") applyTheme(option.value, true);
        saveSettings();
        applyReading();
        syncPopover();
      });
      choices.appendChild(button);
    });
    row.append(name, choices);
    popover.appendChild(row);
  }
  const sizeRow = document.createElement("label");
  sizeRow.className = "setting";
  const sizeName = document.createElement("span");
  sizeName.textContent = "字号";
  const sizeValue = document.createElement("span");
  const sizeInput = document.createElement("input");
  sizeInput.type = "range";
  sizeInput.min = "15";
  sizeInput.max = "21";
  sizeInput.step = "1";
  sizeInput.value = String(settings.size);
  sizeInput.addEventListener("input", function () {
    settings.size = Number(sizeInput.value);
    settings.sizeSet = true;
    saveSettings();
    applyReading();
    sizeValue.textContent = settings.size + "px";
  });
  sizeRow.append(sizeName, sizeInput, sizeValue);
  popover.appendChild(sizeRow);
  choiceRow("版心", "measure", [
    { label: "窄", value: "narrow" },
    { label: "中", value: "mid" },
    { label: "宽", value: "wide" }
  ]);
  choiceRow("字体", "font", [
    { label: "衬线", value: "serif" },
    { label: "黑体", value: "sans" }
  ]);
  choiceRow("行距", "leading", [
    { label: "紧凑", value: "compact" },
    { label: "标准", value: "standard" },
    { label: "宽松", value: "loose" }
  ]);
  choiceRow("主题", "theme", [
    { label: "纸", value: "paper" },
    { label: "墨", value: "ink" },
    { label: "雾灰", value: "mist" },
    { label: "跟随系统", value: "system" }
  ]);
  body.appendChild(popover);
  function syncPopover() {
    sizeInput.value = String(settings.size);
    sizeValue.textContent = settings.size + "px";
    popover.querySelectorAll(".choices button").forEach(function (button) {
      const row = button.parentElement.parentElement;
      const label = row.querySelector("span").textContent;
      const key = { "版心": "measure", "字体": "font", "行距": "leading", "主题": "theme" }[label];
      button.setAttribute("aria-pressed", settings[key] === button.dataset.value ? "true" : "false");
    });
  }
  function placePopover() {
    const rect = readBtn.getBoundingClientRect();
    popover.style.top = (rect.bottom + 6) + "px";
    popover.style.right = Math.max(8, window.innerWidth - rect.right) + "px";
  }
  if (readBtn) {
    readBtn.addEventListener("click", function () {
      const open = popover.hidden;
      popover.hidden = !open;
      readBtn.setAttribute("aria-expanded", open ? "true" : "false");
      if (open) {
        syncPopover();
        placePopover();
      }
    });
  }
  if (themeBtn) {
    themeBtn.addEventListener("click", function () {
      const order = ["paper", "ink", "mist", "system"];
      const index = order.indexOf(settings.theme);
      applyTheme(order[(index + 1) % order.length], true);
      saveSettings();
      syncPopover();
    });
  }

  const help = document.createElement("div");
  help.className = "help";
  help.hidden = true;
  help.setAttribute("role", "dialog");
  help.setAttribute("aria-modal", "true");
  help.setAttribute("aria-label", "快捷键");
  help.innerHTML = "<h2>快捷键</h2><ul></ul>";
  const helpList = help.querySelector("ul");
  [
    ["Ctrl K", "搜索标题和正文"],
    ["Ctrl F", "在文中查找"],
    ["[", "收起侧栏"],
    ["J / K", "下一节 / 上一节"],
    ["T", "切换主题"],
    ["G G", "回到顶部"],
    ["?", "显示这张表"]
  ].forEach(function (row) {
    const li = document.createElement("li");
    const keys = document.createElement("span");
    keys.className = "help-keys";
    keys.textContent = row[0];
    const text = document.createElement("span");
    text.textContent = row[1];
    li.append(keys, text);
    helpList.appendChild(li);
  });
  const helpClose = makeButton("关闭");
  helpClose.addEventListener("click", function () { help.hidden = true; });
  help.appendChild(helpClose);
  body.appendChild(help);

  function typingTarget(event) {
    const target = event.target;
    if (!target) return false;
    const tag = target.tagName;
    return tag === "INPUT" || tag === "TEXTAREA" || target.isContentEditable;
  }

  function currentHeading() {
    const line = main.getBoundingClientRect().top + 28;
    let current = null;
    headings.forEach(function (heading) {
      if (heading.getBoundingClientRect().top <= line) current = heading;
    });
    return current;
  }
  function stepHeading(direction) {
    if (!headings.length) return;
    const current = currentHeading();
    let index = current ? headings.indexOf(current) : -1;
    index = Math.min(headings.length - 1, Math.max(0, index + direction));
    jump(headings[index]);
  }

  let gAt = 0;
  document.addEventListener("keydown", function (event) {
    if (!palette.hidden && event.key === "Escape") { closePalette(); return; }
    if (!help.hidden && event.key === "Escape") { help.hidden = true; return; }
    if (!lightbox.hidden && event.key === "Escape") { closeLightbox(); return; }
    if (!popover.hidden && event.key === "Escape") {
      popover.hidden = true;
      if (readBtn) readBtn.setAttribute("aria-expanded", "false");
      return;
    }
    if ((event.key === "k" || event.key === "K") && (event.ctrlKey || event.metaKey)) {
      event.preventDefault();
      window.mdvPalette();
      return;
    }
    if ((event.key === "f" || event.key === "F") && (event.ctrlKey || event.metaKey)) {
      event.preventDefault();
      openFind();
      return;
    }
    if (event.key === "Escape" && !findBar.hidden) {
      findBar.hidden = true;
      clearMarks();
      return;
    }
    if (typingTarget(event) || event.ctrlKey || event.metaKey || event.altKey) return;
    if (event.key === "[") {
      event.preventDefault();
      window.mdvOutline();
    } else if (event.key === "j" || event.key === "J") {
      event.preventDefault();
      stepHeading(1);
    } else if (event.key === "k" || event.key === "K") {
      event.preventDefault();
      stepHeading(-1);
    } else if (event.key === "t" || event.key === "T") {
      event.preventDefault();
      if (themeBtn) themeBtn.click();
    } else if (event.key === "?") {
      event.preventDefault();
      help.hidden = false;
      helpClose.focus();
    } else if (event.key === "g") {
      const now = Date.now();
      if (now - gAt < 500) {
        main.scrollTo({ top: 0, behavior: motion });
        gAt = 0;
      } else {
        gAt = now;
      }
    }
  });

  function fragmentToMarkdown(fragment) {
    let out = "";
    function walk(node) {
      if (node.nodeType === Node.TEXT_NODE) {
        out += node.textContent;
        return;
      }
      if (node.nodeType !== Node.ELEMENT_NODE) return;
      const tag = node.tagName;
      if (tag === "STRONG" || tag === "B") { out += "**"; node.childNodes.forEach(walk); out += "**"; return; }
      if (tag === "EM" || tag === "I") { out += "*"; node.childNodes.forEach(walk); out += "*"; return; }
      if (tag === "CODE" && node.parentElement.tagName !== "PRE") { out += "`"; out += node.textContent; out += "`"; return; }
      if (tag === "PRE") { out += "\n```\n" + node.textContent + "\n```\n"; return; }
      if (tag === "A") { out += "["; node.childNodes.forEach(walk); out += "](" + (node.getAttribute("href") || "") + ")"; return; }
      if (tag === "BR") { out += "\n"; return; }
      if (/^H[1-6]$/.test(tag)) {
        out += "\n" + "#".repeat(Number(tag[1])) + " ";
        node.childNodes.forEach(walk);
        out += "\n";
        return;
      }
      if (tag === "LI") { out += "\n- "; node.childNodes.forEach(walk); return; }
      if (tag === "P") { node.childNodes.forEach(walk); out += "\n\n"; return; }
      node.childNodes.forEach(walk);
    }
    fragment.childNodes.forEach(walk);
    return out.replace(/\n{3,}/g, "\n\n").trim();
  }

  const selectionBar = document.createElement("div");
  selectionBar.className = "selection-bar";
  selectionBar.hidden = true;
  ["复制", "复制为 Markdown", "高亮", "添加批注"].forEach(function (label) {
    const button = makeButton(label);
    button.addEventListener("click", function () { runSelection(label); });
    selectionBar.appendChild(button);
  });
  body.appendChild(selectionBar);
  selectionBar.addEventListener("mousedown", function (event) { event.preventDefault(); });

  function selectedRange() {
    const selection = window.getSelection();
    if (!selection || selection.isCollapsed || selection.rangeCount === 0) return null;
    const range = selection.getRangeAt(0);
    if (!article.contains(range.commonAncestorContainer)) return null;
    return range;
  }
  function placeSelectionBar() {
    const range = selectedRange();
    if (!range || palette.contains(document.activeElement)) {
      selectionBar.hidden = true;
      return;
    }
    const rect = range.getBoundingClientRect();
    if (!rect || (!rect.width && !rect.height)) {
      selectionBar.hidden = true;
      return;
    }
    selectionBar.hidden = false;
    const width = selectionBar.offsetWidth || 280;
    const height = selectionBar.offsetHeight || 36;
    let left = rect.left;
    let top = rect.bottom + 8;
    if (top + height > window.innerHeight) top = Math.max(8, rect.top - height - 8);
    if (left + width > window.innerWidth - 8) left = window.innerWidth - width - 8;
    selectionBar.style.left = Math.max(8, left) + "px";
    selectionBar.style.top = top + "px";
  }
  document.addEventListener("selectionchange", function () {
    if (selectionBar.hidden) return;
    if (!selectedRange()) selectionBar.hidden = true;
  });
  document.addEventListener("mouseup", function () { setTimeout(placeSelectionBar, 0); });

  function notesKey() { return "mdv-notes:" + docKey(); }
  function marksKey() { return "mdv-marks:" + docKey(); }
  function loadNotes() {
    const notes = readJson(notesKey());
    return Array.isArray(notes) ? notes : [];
  }
  function loadMarks() {
    const marks = readJson(marksKey());
    return Array.isArray(marks) ? marks : [];
  }
  function nearestHeadingId(node) {
    let current = node.nodeType === Node.ELEMENT_NODE ? node : node.parentElement;
    while (current && current !== article) {
      if (/^H[1-6]$/.test(current.tagName) && current.id) return current.id;
      let prev = current.previousElementSibling;
      while (prev) {
        if (/^H[1-6]$/.test(prev.tagName) && prev.id) return prev.id;
        prev = prev.previousElementSibling;
      }
      current = current.parentElement;
    }
    return h1 && h1.id ? h1.id : "";
  }
  function wrapQuote(quote, headingId, className) {
    if (!quote) return null;
    const heading = headingId ? document.getElementById(headingId) : null;
    const walker = document.createTreeWalker(article, NodeFilter.SHOW_TEXT);
    let node = walker.nextNode();
    let passed = !heading;
    while (node) {
      if (!passed && heading && (heading.contains(node) || (heading.compareDocumentPosition(node) & Node.DOCUMENT_POSITION_FOLLOWING))) {
        passed = true;
      }
      if (passed && node.parentElement && !node.parentElement.closest("pre, .anchor, mark")) {
        const at = node.textContent.indexOf(quote);
        if (at >= 0) {
          const range = document.createRange();
          range.setStart(node, at);
          range.setEnd(node, at + quote.length);
          const mark = document.createElement("mark");
          mark.className = className;
          try {
            range.surroundContents(mark);
            return mark;
          } catch (error) {
            return null;
          }
        }
      }
      node = walker.nextNode();
    }
    return null;
  }

  const rail = document.getElementById("rail");
  const railBody = document.getElementById("rail-body");
  const railToggle = makeButton("批注");
  railToggle.className = "text-button";
  railToggle.hidden = true;
  if (themeBtn) themeBtn.before(railToggle);
  function setRail(open) {
    body.classList.toggle("rail-open", open);
    if (rail) rail.hidden = !open;
    body.style.setProperty("--rail", open ? "220px" : "0px");
    syncScrim();
    if (open) renderRail();
  }
  railToggle.addEventListener("click", function () { setRail(!body.classList.contains("rail-open")); });
  const railClose = document.getElementById("rail-close");
  if (railClose) railClose.addEventListener("click", function () { setRail(false); });

  function buildToc() {
    const toc = document.createElement("div");
    toc.className = "rail-toc";
    const section = currentSectionH2();
    shownSection = section ? section.id : "";
    if (section) {
      const kicker = document.createElement("p");
      kicker.className = "rail-kicker";
      kicker.textContent = headingText(section);
      toc.appendChild(kicker);
      let node = section.nextElementSibling;
      while (node && node.tagName !== "H1" && node.tagName !== "H2") {
        if (node.tagName === "H3" && node.id) {
          const link = document.createElement("a");
          link.href = "#" + node.id;
          link.textContent = headingText(node);
          const target = node;
          link.addEventListener("click", function (event) {
            event.preventDefault();
            jump(target);
          });
          toc.appendChild(link);
        }
        node = node.nextElementSibling;
      }
    }
    return toc;
  }
  function renderRail() {
    if (!railBody) return;
    railBody.textContent = "";
    railBody.appendChild(buildToc());
    const notes = loadNotes();
    railToggle.hidden = notes.length === 0 && !body.classList.contains("rail-open");
    railToggle.textContent = notes.length ? "批注 " + notes.length : "批注";
    notes.forEach(function (item) {
      const box = document.createElement("div");
      box.className = "note";
      const quote = document.createElement("p");
      quote.className = "note-quote";
      quote.textContent = item.quote;
      quote.addEventListener("click", function () {
        const mark = article.querySelector('mark.user-note[data-note="' + item.id + '"]');
        if (mark) jump(mark);
      });
      const field = document.createElement("textarea");
      field.value = item.note || "";
      field.setAttribute("aria-label", "批注");
      field.addEventListener("input", function () {
        const all = loadNotes();
        const found = all.find(function (note) { return note.id === item.id; });
        if (found) {
          found.note = field.value;
          writeJson(notesKey(), all);
        }
      });
      const remove = makeButton("删除");
      remove.addEventListener("click", function () {
        writeJson(notesKey(), loadNotes().filter(function (note) { return note.id !== item.id; }));
        const mark = article.querySelector('mark.user-note[data-note="' + item.id + '"]');
        if (mark) mark.replaceWith(document.createTextNode(mark.textContent));
        renderRail();
      });
      box.append(quote, field, remove);
      railBody.appendChild(box);
    });
  }
  function currentSectionH2() {
    const current = currentHeading();
    if (!current) return null;
    if (current.tagName === "H2" || current.tagName === "H1") return current.tagName === "H2" ? current : null;
    let prev = current.previousElementSibling;
    while (prev) {
      if (prev.tagName === "H2") return prev;
      if (prev.tagName === "H1") return null;
      prev = prev.previousElementSibling;
    }
    return null;
  }
  let shownSection = "";
  main.addEventListener("scroll", function () {
    if (!body.classList.contains("rail-open") || !railBody) return;
    const section = currentSectionH2();
    const id = section ? section.id : "";
    if (id === shownSection) return;
    shownSection = id;
    const toc = buildToc();
    const old = railBody.querySelector(".rail-toc");
    if (old) old.replaceWith(toc);
    else railBody.prepend(toc);
  }, { passive: true });

  function runSelection(action) {
    const range = selectedRange();
    if (!range) return;
    const text = range.toString();
    if (action === "复制") {
      copyText(text).then(function () { toast("已复制"); });
    } else if (action === "复制为 Markdown") {
      const fragment = range.cloneContents();
      copyText(fragmentToMarkdown(fragment)).then(function () { toast("已复制 Markdown"); });
    } else if (action === "高亮") {
      const mark = document.createElement("mark");
      mark.className = "user-mark";
      try { range.surroundContents(mark); } catch (error) { return; }
      const marks = loadMarks();
      marks.push({ quote: text, heading: nearestHeadingId(mark) });
      writeJson(marksKey(), marks);
      window.getSelection().removeAllRanges();
    } else if (action === "添加批注") {
      const mark = document.createElement("mark");
      mark.className = "user-note";
      const id = String(Date.now());
      mark.dataset.note = id;
      try { range.surroundContents(mark); } catch (error) { return; }
      const notes = loadNotes();
      notes.push({ id: id, quote: text, note: "", heading: nearestHeadingId(mark) });
      writeJson(notesKey(), notes);
      window.getSelection().removeAllRanges();
      setRail(true);
      const field = railBody && railBody.querySelector("textarea");
      if (field) field.focus();
    }
    selectionBar.hidden = true;
  }

  loadMarks().forEach(function (mark) { wrapQuote(mark.quote, mark.heading, "user-mark"); });
  loadNotes().forEach(function (note) {
    const mark = wrapQuote(note.quote, note.heading, "user-note");
    if (mark) mark.dataset.note = note.id;
  });
  if (loadNotes().length) railToggle.hidden = false;

  let bannerEl = null;
  function showBanner(text, action, onAction) {
    if (bannerEl) bannerEl.remove();
    bannerEl = document.createElement("div");
    bannerEl.className = "banner";
    bannerEl.setAttribute("role", "status");
    const label = document.createElement("span");
    label.textContent = text;
    const button = makeButton(action);
    button.addEventListener("click", function () {
      bannerEl.remove();
      bannerEl = null;
      onAction();
    });
    bannerEl.append(label, button);
    body.appendChild(bannerEl);
  }
  window.mdvFileChanged = function () {
    showBanner("文件已更新", "刷新", function () { post({ kind: "reload" }); });
  };
  window.mdvFileMissing = function () {
    showBanner("文件已移动或删除", "重新选择", function () { post({ kind: "open" }); });
  };
  window.mdvSystemTheme = function (mode) {
    root.dataset.system = mode === "dark" ? "dark" : "light";
    if (settings.theme === "system") applyTheme("system", true);
  };

  document.addEventListener("mousedown", function (event) {
    if (!popover.hidden && !popover.contains(event.target) && event.target !== readBtn) {
      popover.hidden = true;
      if (readBtn) readBtn.setAttribute("aria-expanded", "false");
    }
  });

  spy();
  paintProgress();
})();
