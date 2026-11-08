/* Browser environment for pages: window, document, Element, events, timers, fetch ... built on the native node primitives `__n`. */
(function (g) {
'use strict';
const n = __n;
const nodes = new Map();                               // node id -> wrapper (identity is preserved)
const kebab = s => s.replace(/[A-Z]/g, m => '-' + m.toLowerCase());
const camel = s => s.replace(/-([a-z])/g, (_, c) => c.toUpperCase());

// ---------- events ----------
class Event {
  constructor(type, init) { init = init || {}; this.type = type; this.bubbles = !!init.bubbles; this.cancelable = !!init.cancelable; this.defaultPrevented = false; this.target = null; this.currentTarget = null; this.timeStamp = n.now(); this.eventPhase = 0; this.isTrusted = false; this._stop = false; this._stopNow = false; }
  preventDefault() { if (this.cancelable) this.defaultPrevented = true; }
  stopPropagation() { this._stop = true; }
  stopImmediatePropagation() { this._stop = true; this._stopNow = true; }
  composedPath() { return []; }
}
class UIEvent extends Event { constructor(t, i) { super(t, i); this.detail = (i && i.detail) || 0; this.view = g; } }
class MouseEvent extends UIEvent { constructor(t, i) { super(t, i); i = i || {}; this.clientX = i.clientX || 0; this.clientY = i.clientY || 0; this.pageX = this.clientX; this.pageY = this.clientY; this.screenX = this.clientX; this.screenY = this.clientY; this.button = i.button || 0; this.buttons = i.buttons || 0; this.ctrlKey = !!i.ctrlKey; this.shiftKey = !!i.shiftKey; this.altKey = !!i.altKey; this.metaKey = !!i.metaKey; } }
class KeyboardEvent extends UIEvent { constructor(t, i) { super(t, i); i = i || {}; this.key = i.key || ''; this.code = i.code || ''; this.keyCode = i.keyCode || 0; this.which = this.keyCode; this.ctrlKey = !!i.ctrlKey; this.shiftKey = !!i.shiftKey; this.altKey = !!i.altKey; this.metaKey = !!i.metaKey; this.repeat = false; } }
class InputEvent extends UIEvent { constructor(t, i) { super(t, i); this.data = (i && i.data) || null; this.inputType = (i && i.inputType) || ''; } }
class FocusEvent extends UIEvent {}
class CustomEvent extends Event { constructor(t, i) { super(t, i); this.detail = i && i.detail !== undefined ? i.detail : null; } }
class ErrorEvent extends Event {}
class MessageEvent extends Event { constructor(t, i) { super(t, i); this.data = i && i.data; this.origin = (i && i.origin) || ''; } }
class PromiseRejectionEvent extends Event {}

class EventTarget {
  addEventListener(type, fn, opts) { if (!fn) return; const L = this.__l || (this.__l = {}); const a = L[type] || (L[type] = []); const cap = opts === true || !!(opts && opts.capture); if (a.some(e => e.fn === fn && e.cap === cap)) return; a.push({ fn, cap, once: !!(opts && opts.once) }); }
  removeEventListener(type, fn, opts) { const a = this.__l && this.__l[type]; if (!a) return; const cap = opts === true || !!(opts && opts.capture); const i = a.findIndex(e => e.fn === fn && e.cap === cap); if (i >= 0) a.splice(i, 1); }
  dispatchEvent(ev) {
    ev.target = ev.target || this; const path = []; for (let t = this; t; t = t.__parent ? t.__parent() : null) path.push(t);
    const run = (t, capture) => {
      ev.currentTarget = t; const a = t.__l && t.__l[ev.type]; if (a) for (const e of a.slice()) { if (e.cap !== capture && t !== ev.target) continue; if (e.once) t.removeEventListener(ev.type, e.fn, e.cap); try { typeof e.fn === 'function' ? e.fn.call(t, ev) : e.fn.handleEvent(ev); } catch (x) { console.error('event handler: ' + x + (x && x.stack ? '\n' + x.stack : '')); } if (ev._stopNow) return; }
      if (!capture) { const h = t['on' + ev.type]; if (typeof h === 'function') { try { if (h.call(t, ev) === false) ev.preventDefault(); } catch (x) { console.error('on' + ev.type + ': ' + x); } } }
    };
    for (let i = path.length - 1; i > 0 && !ev._stop; i--) run(path[i], true);
    if (!ev._stop) run(path[0], false);
    if (ev.bubbles) for (let i = 1; i < path.length && !ev._stop; i++) run(path[i], false);
    ev.currentTarget = null; return !ev.defaultPrevented;
  }
}

// ---------- inline styles ----------
function parseStyle(s) { const m = new Map(); if (!s) return m; for (const part of s.split(';')) { const i = part.indexOf(':'); if (i > 0) m.set(part.slice(0, i).trim().toLowerCase(), part.slice(i + 1).trim()); } return m; }
function writeStyle(el, m) { let s = ''; for (const [k, v] of m) s += k + ': ' + v + '; '; if (s) n.setAttr(el.__id, 'style', s.trim()); else n.removeAttr(el.__id, 'style'); }
function styleOf(el) {
  const api = {
    getPropertyValue: k => parseStyle(n.attr(el.__id, 'style')).get(kebab(k)) || '',
    setProperty(k, v) { const m = parseStyle(n.attr(el.__id, 'style')); if (v === '' || v == null) m.delete(kebab(k)); else m.set(kebab(k), String(v)); writeStyle(el, m); },
    removeProperty(k) { const m = parseStyle(n.attr(el.__id, 'style')); const old = m.get(kebab(k)) || ''; m.delete(kebab(k)); writeStyle(el, m); return old; },
  };
  return new Proxy(api, {
    get(t, k) { if (k in t) return t[k]; if (k === 'cssText') return n.attr(el.__id, 'style') || ''; if (typeof k !== 'string') return undefined; if (k === 'length') return parseStyle(n.attr(el.__id, 'style')).size; return parseStyle(n.attr(el.__id, 'style')).get(kebab(k)) || ''; },
    set(t, k, v) { if (k === 'cssText') { if (v) n.setAttr(el.__id, 'style', String(v)); else n.removeAttr(el.__id, 'style'); } else t.setProperty(k, v); return true; },
  });
}

// ---------- nodes ----------
const W = id => { if (id < 0) return null; let w = nodes.get(id); if (w) return w; w = n.type(id) === 3 ? new Text(id) : new HTMLElement(id); nodes.set(id, w); return w; };
const idOf = x => x && x.__id !== undefined ? x.__id : -1;
class Node extends EventTarget {
  constructor(id) { super(); this.__id = id; }
  __parent() { const p = n.parent(this.__id); return p >= 0 ? W(p) : (this.__id === n.root() ? document : null); }
  get parentNode() { const p = n.parent(this.__id); return p >= 0 ? W(p) : (this.__id === n.root() ? document : null); }
  get parentElement() { const p = n.parent(this.__id); return p >= 0 ? W(p) : null; }
  get firstChild() { return W(n.first(this.__id)); }
  get lastChild() { return W(n.last(this.__id)); }
  get nextSibling() { return W(n.next(this.__id)); }
  get previousSibling() { return W(n.prev(this.__id)); }
  get childNodes() { const a = []; for (let c = n.first(this.__id); c >= 0; c = n.next(c)) a.push(W(c)); return a; }
  get ownerDocument() { return document; }
  get isConnected() { let p = this.__id; while (n.parent(p) >= 0) p = n.parent(p); return p === n.root(); }
  hasChildNodes() { return n.first(this.__id) >= 0; }
  contains(o) { for (let p = idOf(o); p >= 0; p = n.parent(p)) if (p === this.__id) return true; return false; }
  get textContent() { return n.textContent(this.__id); }
  set textContent(v) { n.setTextContent(this.__id, String(v)); }
  appendChild(c) { if (c instanceof DocumentFragment) { for (const k of c.childNodes) n.append(this.__id, k.__id); return c; } n.append(this.__id, c.__id); return c; }
  insertBefore(c, ref) { if (c instanceof DocumentFragment) { for (const k of c.childNodes) n.insertBefore(this.__id, k.__id, idOf(ref)); return c; } n.insertBefore(this.__id, c.__id, idOf(ref)); return c; }
  removeChild(c) { n.remove(c.__id); return c; }
  replaceChild(nw, old) { n.insertBefore(this.__id, nw.__id, old.__id); n.remove(old.__id); return old; }
  remove() { n.remove(this.__id); }
  cloneNode(deep) { return cloneInto(this, deep); }
  getRootNode() { return document; }
  normalize() {}
}
function cloneInto(src, deep) {
  if (src.nodeType === 3) return document.createTextNode(src.data);
  const c = document.createElement(src.localName); for (const [k, v] of n.attrs(src.__id)) n.setAttr(c.__id, k, v);
  if (deep) for (let ch = n.first(src.__id); ch >= 0; ch = n.next(ch)) n.append(c.__id, W(ch).cloneNode(true).__id);
  return c;
}
class CharacterData extends Node {
  get data() { return n.text(this.__id) || ''; } set data(v) { n.setText(this.__id, String(v)); }
  get nodeValue() { return this.data; } set nodeValue(v) { this.data = v; }
  get textContent() { return this.data; } set textContent(v) { this.data = v; }
  get length() { return this.data.length; }
}
class Text extends CharacterData { get nodeType() { return 3; } get nodeName() { return '#text'; } get wholeText() { return this.data; } }
class Comment extends CharacterData { get nodeType() { return 8; } get nodeName() { return '#comment'; } }
class DocumentFragment extends Node { constructor() { super(n.create('div')); } get nodeType() { return 11; } get nodeName() { return '#document-fragment'; } querySelector(s) { return qs(this.__id, s); } querySelectorAll(s) { return qsa(this.__id, s); } get children() { return this.childNodes; } }
const qs = (root, s) => W(n.query(root, s, 0));
const qsa = (root, s) => n.query(root, s, 1).map(W);
const toNode = x => x instanceof Node ? x : document.createTextNode(String(x));
const REFLECT = { href: 'href', src: 'src', alt: 'alt', title: 'title', name: 'name', type: 'type', placeholder: 'placeholder', rel: 'rel', target: 'target', htmlFor: 'for', action: 'action', method: 'method', role: 'role', lang: 'lang', width: 'width', height: 'height', content: 'content', charset: 'charset', media: 'media', colSpan: 'colspan', rowSpan: 'rowspan', min: 'min', max: 'max', step: 'step', pattern: 'pattern', accept: 'accept', label: 'label', download: 'download' };
const BOOL = { disabled: 'disabled', hidden: 'hidden', readOnly: 'readonly', required: 'required', multiple: 'multiple', autofocus: 'autofocus', selected: 'selected', open: 'open', async: 'async', defer: 'defer' };
class Element extends Node {
  get nodeType() { return 1; }
  get localName() { return n.tag(this.__id); }
  get tagName() { return this.localName.toUpperCase(); }
  get nodeName() { return this.tagName; }
  get id() { return n.attr(this.__id, 'id') || ''; } set id(v) { n.setAttr(this.__id, 'id', String(v)); }
  get className() { return n.attr(this.__id, 'class') || ''; } set className(v) { n.setAttr(this.__id, 'class', String(v)); }
  get classList() { const el = this; const list = () => (n.attr(el.__id, 'class') || '').split(/\s+/).filter(Boolean); const set = a => n.setAttr(el.__id, 'class', a.join(' '));
    return { add(...c) { const a = list(); for (const x of c) if (!a.includes(x)) a.push(x); set(a); }, remove(...c) { set(list().filter(x => !c.includes(x))); }, contains: c => list().includes(c),
      toggle(c, force) { const has = list().includes(c); const want = force === undefined ? !has : !!force; if (want && !has) this.add(c); else if (!want && has) this.remove(c); return want; },
      replace(a, b) { const l = list(); const i = l.indexOf(a); if (i < 0) return false; l[i] = b; set(l); return true; }, item: i => list()[i] || null, get length() { return list().length; }, toString: () => list().join(' '), [Symbol.iterator]: () => list()[Symbol.iterator]() }; }
  get style() { return this.__style || (this.__style = styleOf(this)); }
  set style(v) { this.style.cssText = v; }
  get dataset() { const el = this; return new Proxy({}, { get: (t, k) => { const v = n.attr(el.__id, 'data-' + kebab(String(k))); return v === null ? undefined : v; }, set: (t, k, v) => { n.setAttr(el.__id, 'data-' + kebab(String(k)), String(v)); return true; }, has: (t, k) => n.attr(el.__id, 'data-' + kebab(String(k))) !== null, deleteProperty: (t, k) => { n.removeAttr(el.__id, 'data-' + kebab(String(k))); return true; }, ownKeys: () => n.attrs(el.__id).filter(a => a[0].startsWith('data-')).map(a => camel(a[0].slice(5))), getOwnPropertyDescriptor: (t, k) => ({ enumerable: true, configurable: true, value: n.attr(el.__id, 'data-' + kebab(String(k))) }) }); }
  getAttribute(k) { return n.attr(this.__id, String(k).toLowerCase()); }
  setAttribute(k, v) { n.setAttr(this.__id, k, String(v)); }
  removeAttribute(k) { n.removeAttr(this.__id, String(k).toLowerCase()); }
  hasAttribute(k) { return n.attr(this.__id, String(k).toLowerCase()) !== null; }
  toggleAttribute(k, f) { const h = this.hasAttribute(k); const w = f === undefined ? !h : !!f; if (w && !h) this.setAttribute(k, ''); else if (!w && h) this.removeAttribute(k); return w; }
  getAttributeNames() { return n.attrs(this.__id).map(a => a[0]); }
  get attributes() { return n.attrs(this.__id).map(a => ({ name: a[0], value: a[1], nodeName: a[0], nodeValue: a[1] })); }
  get children() { const a = []; for (let c = n.first(this.__id); c >= 0; c = n.next(c)) if (n.type(c) === 1) a.push(W(c)); return a; }
  get childElementCount() { return this.children.length; }
  get firstElementChild() { return this.children[0] || null; }
  get lastElementChild() { const c = this.children; return c[c.length - 1] || null; }
  get nextElementSibling() { for (let c = n.next(this.__id); c >= 0; c = n.next(c)) if (n.type(c) === 1) return W(c); return null; }
  get previousElementSibling() { for (let c = n.prev(this.__id); c >= 0; c = n.prev(c)) if (n.type(c) === 1) return W(c); return null; }
  append(...a) { for (const x of a) this.appendChild(toNode(x)); }
  prepend(...a) { const f = this.firstChild; for (const x of a) this.insertBefore(toNode(x), f); }
  before(...a) { const p = this.parentNode; if (p) for (const x of a) p.insertBefore(toNode(x), this); }
  after(...a) { const p = this.parentNode; if (p) { const nx = this.nextSibling; for (const x of a) p.insertBefore(toNode(x), nx); } }
  replaceWith(...a) { this.before(...a); this.remove(); }
  replaceChildren(...a) { this.textContent = ''; this.append(...a); }
  get innerHTML() { return n.innerHTML(this.__id, 0); } set innerHTML(v) { n.setInnerHTML(this.__id, String(v)); }
  get outerHTML() { return n.innerHTML(this.__id, 1); }
  set outerHTML(v) { const p = this.parentNode; if (!p) return; const t = document.createElement('div'); t.innerHTML = v; for (const k of t.childNodes) p.insertBefore(k, this); this.remove(); }
  insertAdjacentHTML(pos, html) { const t = document.createElement('div'); t.innerHTML = html; const kids = t.childNodes;
    if (pos === 'beforeend') kids.forEach(k => this.appendChild(k)); else if (pos === 'afterbegin') { const f = this.firstChild; kids.forEach(k => this.insertBefore(k, f)); } else if (pos === 'beforebegin') kids.forEach(k => this.before(k)); else if (pos === 'afterend') { let a = this; kids.forEach(k => { a.after(k); a = k; }); } }
  insertAdjacentElement(pos, el) { if (pos === 'beforeend') this.appendChild(el); else if (pos === 'afterbegin') this.insertBefore(el, this.firstChild); else if (pos === 'beforebegin') this.before(el); else this.after(el); return el; }
  get innerText() { return this.textContent; } set innerText(v) { this.textContent = v; }
  querySelector(s) { return qs(this.__id, s); } querySelectorAll(s) { return qsa(this.__id, s); }
  getElementsByTagName(t) { t = t.toLowerCase(); return t === '*' ? qsa(this.__id, '*') : qsa(this.__id, t); }
  getElementsByClassName(c) { return qsa(this.__id, '.' + String(c).trim().split(/\s+/).join('.')); }
  matches(s) { return n.matches(this.__id, s); } webkitMatchesSelector(s) { return this.matches(s); }
  closest(s) { for (let e = this; e && e.nodeType === 1; e = e.parentElement) if (e.matches(s)) return e; return null; }
  get value() { return n.value(this.__id); } set value(v) { n.setValue(this.__id, String(v)); }
  get checked() { return n.checked(this.__id); } set checked(v) { n.setChecked(this.__id, !!v); }
  get defaultValue() { return n.attr(this.__id, 'value') || ''; }
  get selectedIndex() { return this.options ? this.options.findIndex(o => o.value === this.value) : -1; }
  get options() { return this.localName === 'select' ? qsa(this.__id, 'option') : undefined; }
  get form() { return this.closest('form'); }
  get elements() { return qsa(this.__id, 'input,select,textarea,button'); }
  get text() { return this.textContent; }
  get currentSrc() { return this.src; }
  get complete() { return true; } get naturalWidth() { return this.getBoundingClientRect().width; } get naturalHeight() { return this.getBoundingClientRect().height; }
  submit() { this.dispatchEvent(new Event('submit', { bubbles: true, cancelable: true })); }
  reset() {}
  focus() { n.focus(this.__id); this.dispatchEvent(new FocusEvent('focus')); }
  blur() { this.dispatchEvent(new FocusEvent('blur')); }
  click() { this.dispatchEvent(new MouseEvent('click', { bubbles: true, cancelable: true })); }
  select() {} scrollIntoView() { n.scrollTo(Math.max(0, this.getBoundingClientRect().top + n.size()[2] - 8)); }
  getBoundingClientRect() { const r = n.rect(this.__id); return { x: r[0], y: r[1], left: r[0], top: r[1], width: r[2], height: r[3], right: r[0] + r[2], bottom: r[1] + r[3], toJSON() { return this; } }; }
  getClientRects() { return [this.getBoundingClientRect()]; }
  get offsetWidth() { return this.getBoundingClientRect().width; } get offsetHeight() { return this.getBoundingClientRect().height; }
  get clientWidth() { return this.offsetWidth; } get clientHeight() { return this.offsetHeight; }
  get offsetLeft() { return this.getBoundingClientRect().left; } get offsetTop() { return this.getBoundingClientRect().top + n.size()[2]; }
  get offsetParent() { return this.parentElement; }
  get scrollWidth() { return this.offsetWidth; } get scrollHeight() { return this.offsetHeight; }
  get scrollTop() { return 0; } set scrollTop(v) {} get scrollLeft() { return 0; } set scrollLeft(v) {}
  scrollTo() {} scroll() {} animate() { return { finished: Promise.resolve(), cancel() {}, onfinish: null }; }
  attachShadow() { return this; } get shadowRoot() { return null; }
  get contentEditable() { return 'inherit'; } set contentEditable(v) {}
  get tabIndex() { return 0; } set tabIndex(v) {}
  get draggable() { return false; } set draggable(v) {}
  get isContentEditable() { return false; }
  hasPointerCapture() { return false; } setPointerCapture() {} releasePointerCapture() {}
  requestFullscreen() { return Promise.resolve(); }
  play() { return Promise.resolve(); } pause() {} load() {} get paused() { return true; } get volume() { return 1; } set volume(v) {} get muted() { return false; } set muted(v) {} get currentTime() { return 0; } set currentTime(v) {} get duration() { return NaN; }
  getContext() { return null; } toDataURL() { return ''; }
}
for (const [prop, attr] of Object.entries(REFLECT)) Object.defineProperty(Element.prototype, prop, { get() { const v = n.attr(this.__id, attr); if (v !== null && (prop === 'href' || prop === 'src' || prop === 'action')) { try { return new URL(v, location.href).href; } catch (e) { return v; } } return v === null ? '' : v; }, set(v) { n.setAttr(this.__id, attr, String(v)); }, configurable: true });
for (const [prop, attr] of Object.entries(BOOL)) Object.defineProperty(Element.prototype, prop, { get() { return n.attr(this.__id, attr) !== null; }, set(v) { if (v) n.setAttr(this.__id, attr, ''); else n.removeAttr(this.__id, attr); }, configurable: true });
class HTMLElement extends Element {}
const ev = (...names) => { for (const nm of names) Object.defineProperty(EventTarget.prototype, 'on' + nm, { get() { return (this.__h && this.__h[nm]) || null; }, set(f) { (this.__h || (this.__h = {}))[nm] = f; }, configurable: true }); };

// ---------- document ----------
class Document extends Node {
  constructor() { super(n.root()); }
  get nodeType() { return 9; } get nodeName() { return '#document'; }
  __parent() { return g; } get parentNode() { return null; } get parentElement() { return null; }
  get documentElement() { return W(n.root()); }
  get body() { return W(n.body()); } get head() { return W(n.head()); }
  get title() { return n.title(); } set title(v) { n.setTitle(String(v)); }
  get URL() { return n.url(); } get documentURI() { return n.url(); } get baseURI() { return n.url(); }
  get location() { return location; } set location(v) { location.href = v; }
  get readyState() { return readyState; }
  get cookie() { return cookieJar.join('; '); } set cookie(v) { const kv = String(v).split(';')[0]; const k = kv.split('=')[0].trim(); const i = cookieJar.findIndex(c => c.split('=')[0].trim() === k); if (i >= 0) cookieJar[i] = kv.trim(); else cookieJar.push(kv.trim()); }
  get referrer() { return ''; } get domain() { return location.hostname; } get characterSet() { return 'UTF-8'; } get contentType() { return 'text/html'; } get compatMode() { return 'CSS1Compat'; }
  get hidden() { return false; } get visibilityState() { return 'visible'; } hasFocus() { return true; }
  get activeElement() { return document.body; }
  get defaultView() { return g; }
  get scrollingElement() { return document.documentElement; }
  get forms() { return qsa(n.root(), 'form'); } get images() { return qsa(n.root(), 'img'); } get links() { return qsa(n.root(), 'a[href]'); } get scripts() { return qsa(n.root(), 'script'); } get styleSheets() { return []; }
  get fonts() { return { ready: Promise.resolve(), load: () => Promise.resolve([]), check: () => true, addEventListener() {} }; }
  createElement(t) { return W(n.create(String(t).toLowerCase())); }
  createElementNS(ns, t) { return this.createElement(t); }
  createTextNode(s) { return W(n.createText(String(s))); }
  createComment(s) { const c = W(n.createText('')); Object.setPrototypeOf(c, Comment.prototype); return c; }
  createDocumentFragment() { return new DocumentFragment(); }
  createEvent(t) { return new Event(''); }
  createRange() { return { setStart() {}, setEnd() {}, selectNodeContents() {}, collapse() {}, getBoundingClientRect() { return { left: 0, top: 0, width: 0, height: 0 }; }, createContextualFragment(h) { const f = new DocumentFragment(); f.innerHTML = h; return f; } }; }
  getElementById(id) { return qs(n.root(), '#' + CSS.escape(String(id))); }
  getElementsByTagName(t) { return qsa(n.root(), String(t).toLowerCase()); }
  getElementsByClassName(c) { return qsa(n.root(), '.' + String(c).trim().split(/\s+/).join('.')); }
  getElementsByName(nm) { return qsa(n.root(), '[name="' + nm + '"]'); }
  querySelector(s) { return qs(n.root(), s); } querySelectorAll(s) { return qsa(n.root(), s); }
  elementFromPoint() { return document.body; } elementsFromPoint() { return [document.body]; }
  getSelection() { return { rangeCount: 0, toString: () => '', removeAllRanges() {}, addRange() {}, getRangeAt() { return document.createRange(); } }; }
  execCommand() { return false; } queryCommandSupported() { return false; }
  open() { return this; } close() {}
  write(h) { const b = this.body; if (b) b.insertAdjacentHTML('beforeend', String(h)); } writeln(h) { this.write(h + '\n'); }
  adoptNode(x) { return x; } importNode(x, d) { return x.cloneNode(d); }
}
const cookieJar = [];
let readyState = 'loading';
const CSS = { escape: s => String(s).replace(/([^a-zA-Z0-9_ -￿-])/g, '\\$1'), supports: () => false };
const document = new Document();
nodes.set(n.root(), document.documentElement);

// ---------- location / history / navigator ----------
const location = {
  get href() { return n.url(); }, set href(v) { n.navigate(String(v)); },
  get protocol() { try { return new URL(n.url()).protocol; } catch (e) { return 'about:'; } },
  get host() { try { return new URL(n.url()).host; } catch (e) { return ''; } },
  get hostname() { try { return new URL(n.url()).hostname; } catch (e) { return ''; } },
  get port() { try { return new URL(n.url()).port; } catch (e) { return ''; } },
  get origin() { try { return new URL(n.url()).origin; } catch (e) { return 'null'; } },
  get pathname() { try { return new URL(n.url()).pathname; } catch (e) { return '/'; } },
  get search() { try { return new URL(n.url()).search; } catch (e) { return ''; } },
  get hash() { try { return new URL(n.url()).hash; } catch (e) { return ''; } }, set hash(v) {},
  assign(u) { n.navigate(String(u)); }, replace(u) { n.navigate(String(u)); }, reload() { n.navigate(n.url()); }, toString() { return n.url(); },
};
const history = { length: 1, state: null, scrollRestoration: 'auto', pushState(s, t, u) { this.state = s; }, replaceState(s, t, u) { this.state = s; }, back() {}, forward() {}, go() {} };
const navigator = { userAgent: 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Willows/1.0 Safari/537.36', appName: 'Netscape', appVersion: '5.0 (Willows)', platform: 'Win32', vendor: 'Willows', language: 'en-US', languages: ['en-US', 'en'], onLine: true, cookieEnabled: true, hardwareConcurrency: 1, maxTouchPoints: 0, deviceMemory: 1, doNotTrack: null, webdriver: false,
  clipboard: { writeText: () => Promise.resolve(), readText: () => Promise.resolve('') }, sendBeacon: () => true, serviceWorker: undefined, mediaDevices: undefined, permissions: { query: () => Promise.resolve({ state: 'denied' }) }, geolocation: { getCurrentPosition(a, e) { e && e({ code: 1, message: 'denied' }); } } };

// ---------- timers ----------
let timerId = 1, timers = [], raf = [];
const sync = () => n.timers(timers.length + raf.length > 0 ? 1 : 0);
g.setTimeout = (f, ms, ...a) => { const t = { id: timerId++, due: n.now() + Math.max(0, +ms || 0), f, a, every: -1 }; timers.push(t); sync(); return t.id; };
g.setInterval = (f, ms, ...a) => { ms = Math.max(4, +ms || 0); const t = { id: timerId++, due: n.now() + ms, f, a, every: ms }; timers.push(t); sync(); return t.id; };
g.clearTimeout = g.clearInterval = id => { timers = timers.filter(t => t.id !== id); sync(); };
g.requestAnimationFrame = f => { const id = timerId++; raf.push({ id, f }); sync(); return id; };
g.cancelAnimationFrame = id => { raf = raf.filter(r => r.id !== id); sync(); };
g.requestIdleCallback = f => g.setTimeout(() => f({ didTimeout: false, timeRemaining: () => 10 }), 1);
g.cancelIdleCallback = g.clearTimeout;
g.queueMicrotask = f => Promise.resolve().then(f);
g.__tick = () => {
  const now = n.now(); const due = timers.filter(t => t.due <= now).sort((a, b) => a.due - b.due);
  for (const t of due) { if (t.every >= 0) t.due = now + t.every; else timers = timers.filter(x => x !== t); try { typeof t.f === 'function' ? t.f(...t.a) : (0, eval)(String(t.f)); } catch (e) { console.error('timer: ' + e + (e && e.stack ? '\n' + e.stack : '')); } }
  const r = raf; raf = []; for (const x of r) { try { x.f(now); } catch (e) { console.error('raf: ' + e); } }
  sync();
};

// ---------- network ----------
class Headers { constructor(i) { this.m = new Map(); if (i) for (const [k, v] of (i instanceof Headers ? i.m : Array.isArray(i) ? i : Object.entries(i))) this.m.set(String(k).toLowerCase(), String(v)); } get(k) { return this.m.get(String(k).toLowerCase()) ?? null; } set(k, v) { this.m.set(String(k).toLowerCase(), String(v)); } has(k) { return this.m.has(String(k).toLowerCase()); } append(k, v) { this.set(k, v); } delete(k) { this.m.delete(String(k).toLowerCase()); } forEach(f) { this.m.forEach((v, k) => f(v, k)); } entries() { return this.m.entries(); } keys() { return this.m.keys(); } values() { return this.m.values(); } [Symbol.iterator]() { return this.m.entries(); } }
class Response { constructor(body, init) { init = init || {}; this._b = body == null ? '' : String(body); this.status = init.status === undefined ? 200 : init.status; this.ok = this.status >= 200 && this.status < 300; this.statusText = init.statusText || ''; this.headers = new Headers(init.headers); this.url = init.url || ''; this.type = 'basic'; this.redirected = false; this.bodyUsed = false; }
  text() { this.bodyUsed = true; return Promise.resolve(this._b); } json() { return this.text().then(t => JSON.parse(t)); } blob() { return this.text().then(t => ({ size: t.length, type: this.headers.get('content-type') || '', text: () => Promise.resolve(t) })); }
  arrayBuffer() { return this.text().then(t => new TextEncoder().encode(t).buffer); } clone() { return new Response(this._b, this); } }
const hdrString = h => { let s = ''; if (h) for (const [k, v] of (h instanceof Headers ? h.m : Array.isArray(h) ? h : Object.entries(h))) s += k + ': ' + v + '\r\n'; return s; };
const bodyOf = (b, h) => { if (b == null) return [null, '']; if (b instanceof URLSearchParams) return [b.toString(), 'application/x-www-form-urlencoded;charset=UTF-8']; if (typeof b === 'string') return [b, 'text/plain;charset=UTF-8']; if (b instanceof g.FormData) return [new URLSearchParams(b.l.map(x => [x[0], String(x[1])])).toString(), 'application/x-www-form-urlencoded;charset=UTF-8']; try { return [JSON.stringify(b), 'text/plain;charset=UTF-8']; } catch (e) { return [String(b), 'text/plain']; } };
g.fetch = (input, init) => new Promise((resolve, reject) => { init = init || {}; const url = typeof input === 'string' ? input : input.url || String(input); const method = String(init.method || (typeof input === 'object' && input.method) || 'GET').toUpperCase();
  const [body, dct] = method === 'GET' || method === 'HEAD' ? [null, ''] : bodyOf(init.body); let hs = hdrString(init.headers); if (body !== null && !/content-type:/i.test(hs)) hs += 'Content-Type: ' + dct + '\r\n';
  g.setTimeout(() => { const r = n.http(url, method, body, hs); if (!r.status) { reject(new TypeError('Failed to fetch')); return; } const h = { 'content-type': r.type }; resolve(new Response(r.body, { status: r.status, url: r.url, headers: h })); }, 0); });
g.XMLHttpRequest = class XMLHttpRequest extends EventTarget {
  constructor() { super(); this.readyState = 0; this.status = 0; this.statusText = ''; this.responseText = ''; this.response = ''; this.responseType = ''; this.timeout = 0; this.withCredentials = false; }
  open(m, u) { this._m = String(m).toUpperCase(); this._u = u; this._h = {}; this.readyState = 1; this.__fire('readystatechange'); }
  setRequestHeader(k, v) { this._h[k] = v; } overrideMimeType() {} abort() { this._abort = true; } getAllResponseHeaders() { return 'content-type: ' + (this._t || '') + '\r\n'; } getResponseHeader(k) { return String(k).toLowerCase() === 'content-type' ? this._t || null : null; }
  __fire(t) { const e = new Event(t); e.target = this; if (typeof this['on' + t] === 'function') try { this['on' + t](e); } catch (x) { console.error(x); } this.dispatchEvent(e); }
  send(body) { const [b, dct] = this._m === 'GET' || this._m === 'HEAD' ? [null, ''] : bodyOf(body); let hs = hdrString(this._h); if (b !== null && !/content-type:/i.test(hs)) hs += 'Content-Type: ' + dct + '\r\n';
    g.setTimeout(() => { if (this._abort) return; const r = n.http(this._u, this._m, b, hs); this._t = r.type; this.status = r.status; this.statusText = r.status === 200 ? 'OK' : ''; this.responseURL = r.url; this.responseText = r.body; this.response = this.responseType === 'json' ? (() => { try { return JSON.parse(r.body); } catch (e) { return null; } })() : r.body; this.readyState = 4; this.__fire('readystatechange'); this.__fire(r.status ? 'load' : 'error'); this.__fire('loadend'); }, 0); }
};
g.XMLHttpRequest.DONE = 4;
g.WebSocket = class WebSocket extends EventTarget { constructor(u) { super(); this.url = u; this.readyState = 3; g.setTimeout(() => { const e = new Event('error'); if (this.onerror) this.onerror(e); this.dispatchEvent(e); const c = new Event('close'); if (this.onclose) this.onclose(c); this.dispatchEvent(c); }, 0); } send() {} close() {} };
g.EventSource = class EventSource extends EventTarget { constructor(u) { super(); this.readyState = 2; } close() {} };

// ---------- utilities ----------
class URLSearchParams {
  constructor(i) { this.l = []; if (typeof i === 'string') { for (const p of i.replace(/^\?/, '').split('&')) if (p) { const k = p.indexOf('='); const dec = s => decodeURIComponent(s.replace(/\+/g, ' ')); this.l.push(k < 0 ? [dec(p), ''] : [dec(p.slice(0, k)), dec(p.slice(k + 1))]); } } else if (i) for (const [k, v] of (Array.isArray(i) ? i : Object.entries(i))) this.l.push([String(k), String(v)]); }
  get(k) { const e = this.l.find(x => x[0] === k); return e ? e[1] : null; } getAll(k) { return this.l.filter(x => x[0] === k).map(x => x[1]); } has(k) { return this.l.some(x => x[0] === k); }
  set(k, v) { const i = this.l.findIndex(x => x[0] === k); if (i < 0) this.l.push([k, String(v)]); else { this.l[i][1] = String(v); this.l = this.l.filter((x, j) => x[0] !== k || j === i); } } append(k, v) { this.l.push([k, String(v)]); } delete(k) { this.l = this.l.filter(x => x[0] !== k); }
  forEach(f) { this.l.forEach(([k, v]) => f(v, k, this)); } entries() { return this.l[Symbol.iterator](); } keys() { return this.l.map(x => x[0])[Symbol.iterator](); } values() { return this.l.map(x => x[1])[Symbol.iterator](); } [Symbol.iterator]() { return this.entries(); }
  toString() { return this.l.map(([k, v]) => encodeURIComponent(k) + '=' + encodeURIComponent(v)).join('&'); } sort() { this.l.sort((a, b) => a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0); }
}
class URL {
  constructor(u, base) {
    u = String(u); let m = /^([a-zA-Z][a-zA-Z0-9+.-]*):(\/\/([^\/?#]*))?([^?#]*)(\?[^#]*)?(#.*)?$/.exec(u);
    if (!m) { if (base === undefined) throw new TypeError('Invalid URL'); const b = new URL(base); if (u.startsWith('//')) return new URL(b.protocol + u); let path = u.startsWith('/') ? u : u.startsWith('?') || u.startsWith('#') || u === '' ? b.pathname + (u.startsWith('#') ? b.search : '') : b.pathname.replace(/[^\/]*$/, '') + u; let q = '', h = ''; let i = path.indexOf('#'); if (i >= 0) { h = path.slice(i); path = path.slice(0, i); } i = path.indexOf('?'); if (i >= 0) { q = path.slice(i); path = path.slice(0, i); } if (u.startsWith('?')) { path = b.pathname; }
      const out = []; for (const s of path.split('/')) { if (s === '..') out.length > 1 && out.pop(); else if (s !== '.') out.push(s); } return new URL(b.protocol + '//' + b.host + out.join('/') + q + h); }
    this.protocol = m[1].toLowerCase() + ':'; const auth = m[3] || ''; const at = auth.lastIndexOf('@'); const hp = at >= 0 ? auth.slice(at + 1) : auth; this.username = ''; this.password = ''; const c = hp.lastIndexOf(':'); this.hostname = (c >= 0 && !hp.endsWith(']') ? hp.slice(0, c) : hp).toLowerCase(); this.port = c >= 0 && !hp.endsWith(']') ? hp.slice(c + 1) : '';
    this.pathname = m[4] || (m[2] ? '/' : ''); this.search = m[5] && m[5] !== '?' ? m[5] : ''; this.hash = m[6] && m[6] !== '#' ? m[6] : ''; this.searchParams = new URLSearchParams(this.search);
  }
  get host() { return this.hostname + (this.port ? ':' + this.port : ''); } get origin() { return this.hostname ? this.protocol + '//' + this.host : 'null'; }
  get href() { const s = this.searchParams.toString(); return this.protocol + (this.hostname ? '//' + this.host : '') + this.pathname + (s ? '?' + s : this.search) + this.hash; } toString() { return this.href; } toJSON() { return this.href; }
  static createObjectURL() { return 'blob:willows'; } static revokeObjectURL() {}
}
class TextEncoder { get encoding() { return 'utf-8'; } encode(s) { s = unescape(encodeURIComponent(String(s))); const a = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) a[i] = s.charCodeAt(i); return a; } }
class TextDecoder { constructor(e) { this.encoding = e || 'utf-8'; } decode(b) { if (!b) return ''; const a = new Uint8Array(b.buffer || b); let s = ''; for (let i = 0; i < a.length; i++) s += String.fromCharCode(a[i]); try { return decodeURIComponent(escape(s)); } catch (e) { return s; } } }
const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
g.btoa = s => { s = String(s); let o = ''; for (let i = 0; i < s.length; i += 3) { const a = s.charCodeAt(i), b = s.charCodeAt(i + 1), c = s.charCodeAt(i + 2); o += B64[a >> 2] + B64[((a & 3) << 4) | (b >> 4 || 0)] + (i + 1 < s.length ? B64[((b & 15) << 2) | (c >> 6 || 0)] : '=') + (i + 2 < s.length ? B64[c & 63] : '='); } return o; };
g.atob = s => { s = String(s).replace(/[=\s]/g, ''); let o = '', bits = 0, v = 0; for (const ch of s) { v = (v << 6) | B64.indexOf(ch); bits += 6; if (bits >= 8) { bits -= 8; o += String.fromCharCode((v >> bits) & 255); } } return o; };
class Storage { constructor() { this.m = new Map(); } getItem(k) { return this.m.has(String(k)) ? this.m.get(String(k)) : null; } setItem(k, v) { this.m.set(String(k), String(v)); } removeItem(k) { this.m.delete(String(k)); } clear() { this.m.clear(); } key(i) { return [...this.m.keys()][i] ?? null; } get length() { return this.m.size; } }
class Observer { constructor(cb) { this.cb = cb; } observe() {} unobserve() {} disconnect() {} takeRecords() { return []; } }
const mq = { matches: false, media: '', addListener() {}, removeListener() {}, addEventListener() {}, removeEventListener() {}, onchange: null };

// ---------- the window ----------
class Window extends EventTarget {}
Object.setPrototypeOf(g, Window.prototype);
const sz = () => n.size();
Object.assign(g, { window: g, self: g, top: g, parent: g, frames: g, globalThis: g, document, location, history, navigator,
  Node, Element, HTMLElement, Document, Text, Comment, DocumentFragment, CharacterData, Event, UIEvent, MouseEvent, KeyboardEvent, InputEvent, FocusEvent, CustomEvent, ErrorEvent, MessageEvent, PromiseRejectionEvent, EventTarget, Headers, Response, URL, URLSearchParams, TextEncoder, TextDecoder, Storage, CSS,
  localStorage: new Storage(), sessionStorage: new Storage(),
  MutationObserver: Observer, ResizeObserver: Observer, IntersectionObserver: Observer, PerformanceObserver: Observer, ReportingObserver: Observer,
  Image: function () { return document.createElement('img'); }, Option: function (t, v) { const o = document.createElement('option'); o.textContent = t || ''; if (v !== undefined) o.value = v; return o; },
  screen: { width: 1280, height: 720, availWidth: 1280, availHeight: 720, colorDepth: 24, pixelDepth: 24, orientation: { type: 'landscape-primary', angle: 0 } },
  performance: { now: () => n.now() - t0, timeOrigin: Date.now(), mark() {}, measure() {}, getEntriesByType: () => [], getEntriesByName: () => [], getEntries: () => [], timing: { navigationStart: Date.now() } },
  crypto: { getRandomValues(a) { for (let i = 0; i < a.length; i++) a[i] = n.rand() & 255; return a; }, randomUUID() { const h = () => (n.rand() & 0xffff).toString(16).padStart(4, '0'); return h() + h() + '-' + h() + '-4' + h().slice(1) + '-a' + h().slice(1) + '-' + h() + h() + h(); }, subtle: {} },
  matchMedia: q => Object.assign({}, mq, { media: String(q) }), getComputedStyle: el => (el && el.style) || styleOf(document.body), getSelection: () => document.getSelection(),
  alert: m => n.alert(String(m)), confirm: m => { n.alert(String(m)); return true; }, prompt: (m, d) => d || null, print() {}, open() { return null; }, close() {}, focus() {}, blur() {}, stop() {},
  scrollTo(x, y) { n.scrollTo(typeof x === 'object' ? x.top || 0 : y || 0); }, scroll(x, y) { g.scrollTo(x, y); }, scrollBy(x, y) { n.scrollTo(n.size()[2] + (typeof x === 'object' ? x.top || 0 : y || 0)); },
  postMessage(d) { g.setTimeout(() => g.dispatchEvent(new MessageEvent('message', { data: d, origin: location.origin })), 0); },
  structuredClone: v => JSON.parse(JSON.stringify(v)), reportError: e => console.error(e), isSecureContext: true, origin: 'null', name: '', closed: false, opener: null, length: 0,
  addEventListener: EventTarget.prototype.addEventListener, removeEventListener: EventTarget.prototype.removeEventListener, dispatchEvent: EventTarget.prototype.dispatchEvent,
  Worker: class Worker extends EventTarget { constructor() { super(); throw new Error('Web Workers are not supported'); } }, BroadcastChannel: class BroadcastChannel extends EventTarget { postMessage() {} close() {} }, MessageChannel: class { constructor() { this.port1 = new EventTarget(); this.port2 = new EventTarget(); } },
  AbortController: class AbortController { constructor() { this.signal = { aborted: false, reason: undefined, addEventListener() {}, removeEventListener() {}, throwIfAborted() {} }; } abort(r) { this.signal.aborted = true; this.signal.reason = r; } },
  FormData: class FormData { constructor() { this.l = []; } append(k, v) { this.l.push([k, v]); } get(k) { const e = this.l.find(x => x[0] === k); return e ? e[1] : null; } has(k) { return this.l.some(x => x[0] === k); } },
  Blob: class Blob { constructor(p, o) { this.p = p || []; this.type = (o && o.type) || ''; this.size = this.p.reduce((a, x) => a + String(x).length, 0); } text() { return Promise.resolve(this.p.join('')); } },
  File: class File {}, FileReader: class FileReader extends EventTarget {},
});
const t0 = n.now();
// console
const fmt = a => a.map(x => { if (typeof x === 'string') return x; if (x instanceof Error) return x.stack || String(x); try { return typeof x === 'object' && x !== null ? JSON.stringify(x) : String(x); } catch (e) { return String(x); } }).join(' ');
g.console = { log: (...a) => n.log(fmt(a)), info: (...a) => n.log(fmt(a)), debug: (...a) => n.log(fmt(a)), warn: (...a) => n.log('[warn] ' + fmt(a)), error: (...a) => n.log('[error] ' + fmt(a)), trace() {}, dir: (...a) => n.log(fmt(a)), table: (...a) => n.log(fmt(a)), group() {}, groupEnd() {}, groupCollapsed() {}, time() {}, timeEnd() {}, timeLog() {}, assert(c, ...a) { if (!c) n.log('[assert] ' + fmt(a)); }, count() {}, clear() {} };
ev('click', 'dblclick', 'mousedown', 'mouseup', 'mousemove', 'mouseover', 'mouseout', 'mouseenter', 'mouseleave', 'keydown', 'keyup', 'keypress', 'input', 'change', 'submit', 'focus', 'blur', 'load', 'error', 'scroll', 'resize', 'wheel', 'contextmenu', 'message', 'hashchange', 'popstate', 'beforeunload', 'unload', 'readystatechange', 'DOMContentLoaded', 'touchstart', 'touchend', 'pointerdown', 'pointerup', 'pointermove', 'focusin', 'focusout');

// ---------- host entry points ----------
// returns true when a handler called preventDefault()
g.__dispatch = (id, type, x, y, key) => {
  if (type === '__domready') {
    readyState = 'interactive'; let e = new Event('readystatechange'); document.dispatchEvent(e);
    document.dispatchEvent(new Event('DOMContentLoaded', { bubbles: true }));
    readyState = 'complete'; document.dispatchEvent(new Event('readystatechange'));
    const l = new Event('load'); g.dispatchEvent(l); return false;
  }
  const target = id >= 0 ? W(id) : document;
  let e;
  if (type === 'click' || type === 'mousedown' || type === 'mouseup' || type === 'dblclick') e = new MouseEvent(type, { bubbles: true, cancelable: true, clientX: x, clientY: y, button: 0 });
  else if (type === 'keydown' || type === 'keyup' || type === 'keypress') e = new KeyboardEvent(type, { bubbles: true, cancelable: true, key: key >= 32 ? String.fromCharCode(key) : key === 13 ? 'Enter' : key === 8 ? 'Backspace' : key === 27 ? 'Escape' : '', keyCode: key });
  else if (type === 'input') e = new InputEvent('input', { bubbles: true });
  else e = new Event(type, { bubbles: type !== 'focus' && type !== 'blur', cancelable: type === 'submit' });
  e.isTrusted = true; e.target = target; target.dispatchEvent(e);
  if (type === 'click' && target.localName && !e.defaultPrevented) { /* default actions are handled by the browser */ }
  return e.defaultPrevented;
};
Object.defineProperty(g, 'innerWidth', { get: () => sz()[0] }); Object.defineProperty(g, 'innerHeight', { get: () => sz()[1] });
Object.defineProperty(g, 'outerWidth', { get: () => sz()[0] }); Object.defineProperty(g, 'outerHeight', { get: () => sz()[1] });
Object.defineProperty(g, 'scrollY', { get: () => sz()[2] }); Object.defineProperty(g, 'pageYOffset', { get: () => sz()[2] }); g.scrollX = 0; g.pageXOffset = 0; g.devicePixelRatio = 1;
})(globalThis);
