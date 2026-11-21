// Phosphor icons (https://phosphoricons.com), inlined as SVG at build time so the site ships no
// icon font or script. Shared by the Icon component and the Markdown emoji swap.
import fs from "node:fs";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const cache = new Map();

// Phosphor's SVGs are one <svg> of self-closing shapes (path, rect, circle, line, polyline).
function read(name, weight) {
  const key = `${weight}/${name}`;
  if (!cache.has(key)) {
    const file = require.resolve(`@phosphor-icons/core/${weight}/${name}.svg`);
    const svg = fs.readFileSync(file, "utf8");
    const viewBox = /viewBox="([^"]+)"/.exec(svg)?.[1] ?? "0 0 256 256";
    const inner = svg.replace(/^[\s\S]*?<svg[^>]*>/, "").replace(/<\/svg>\s*$/, "");
    const shapes = [...inner.matchAll(/<(\w+)([^>]*?)\/>/g)].map(([, tag, attrs]) => ({
      tag,
      attrs: Object.fromEntries([...attrs.matchAll(/([\w:-]+)="([^"]*)"/g)].map(([, k, v]) => [k, v])),
    }));
    if (!shapes.length) throw new Error(`Phosphor icon ${key} has no shapes this reader understands`);
    cache.set(key, { viewBox, shapes });
  }
  return cache.get(key);
}

// A label makes the icon an image with that name; without one it's decorative and hidden.
function a11y(label) {
  return label ? { role: "img", "aria-label": label } : { "aria-hidden": "true" };
}

const esc = (s) => String(s).replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;");

export function iconSvg(name, { weight = "regular", label, className = "ph-icon" } = {}) {
  const { viewBox, shapes } = read(name, weight);
  const attrs = { class: className, viewBox, width: "1em", height: "1em", fill: "currentColor", focusable: "false", ...a11y(label) };
  const head = Object.entries(attrs).map(([k, v]) => `${k}="${esc(v)}"`).join(" ");
  const body = shapes
    .map(({ tag, attrs: a }) => `<${tag} ${Object.entries(a).map(([k, v]) => `${k}="${esc(v)}"`).join(" ")}/>`)
    .join("");
  return `<svg xmlns="http://www.w3.org/2000/svg" ${head}>${label ? `<title>${esc(label)}</title>` : ""}${body}</svg>`;
}

export function iconHast(name, { weight = "regular", label, className = "ph-icon" } = {}) {
  const { viewBox, shapes } = read(name, weight);
  const children = shapes.map(({ tag, attrs }) => ({ type: "element", tagName: tag, properties: { ...attrs }, children: [] }));
  if (label) children.unshift({ type: "element", tagName: "title", properties: {}, children: [{ type: "text", value: label }] });
  return {
    type: "element",
    tagName: "svg",
    properties: { className: [className], viewBox, width: "1em", height: "1em", fill: "currentColor", focusable: "false", ...a11y(label) },
    children,
  };
}

// The icon's shapes as SVG markup, for hosts that supply their own <svg> wrapper.
export function iconShapes(name, { weight = "regular" } = {}) {
  const { shapes } = read(name, weight);
  return shapes
    .map(({ tag, attrs }) => `<${tag} ${Object.entries(attrs).map(([k, v]) => `${k}="${esc(v)}"`).join(" ")}/>`)
    .join("");
}

// A data: URL of the icon, for CSS masks.
export function iconDataUrl(name, { weight = "regular" } = {}) {
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">${iconShapes(name, { weight })}</svg>`;
  return `url("data:image/svg+xml,${encodeURIComponent(svg)}")`;
}
