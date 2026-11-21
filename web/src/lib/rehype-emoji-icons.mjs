// Swaps emoji in the rendered docs for Phosphor icons. The emoji stay in the Markdown itself:
// README.md and CHANGELOG.md come from upstream, and editing them would conflict on every merge.
// Each mapped emoji becomes a labelled icon, so its meaning still reaches screen readers. An
// unmapped emoji is dropped with a build warning, so it gets noticed and added here.
import { iconHast } from "./phosphor.mjs";

const map = {
  "⚠": ["warning", "Warning"],
  "❤": ["heart", "love"],
  "✅": ["check-circle", "Done"],
  "✔": ["check", "Yes"],
  "❌": ["x-circle", "No"],
  "✖": ["x", "No"],
  "💡": ["lightbulb", "Tip"],
  "ℹ": ["info", "Note"],
  "🚀": ["rocket", ""],
  "🔧": ["wrench", ""],
  "🎛": ["sliders-horizontal", ""],
  "🎹": ["piano-keys", ""],
  "📝": ["note-pencil", ""],
  "📦": ["package", ""],
};

// Pictographic emoji plus the older symbol emoji, each with an optional variation selector.
const EMOJI = /(\p{Extended_Pictographic})️?/gu;
const HAS_EMOJI = /\p{Extended_Pictographic}/u;
// Unicode counts these as pictographic, but in docs they're text marks, not emoji.
const KEEP = new Set(["©", "®", "™"]);
const SKIP = new Set(["code", "pre", "script", "style", "svg"]);

export default function rehypeEmojiIcons() {
  return (tree, file) => {
    const visit = (node) => {
      if (!node.children || SKIP.has(node.tagName)) return;
      const out = [];
      for (const child of node.children) {
        if (child.type !== "text" || !HAS_EMOJI.test(child.value)) {
          visit(child);
          out.push(child);
          continue;
        }
        let last = 0;
        for (const m of child.value.matchAll(EMOJI)) {
          if (KEEP.has(m[1])) continue;
          if (m.index > last) out.push({ type: "text", value: child.value.slice(last, m.index) });
          const hit = map[m[1]];
          if (hit) out.push(iconHast(hit[0], { label: hit[1] || undefined, className: "ph-icon ph-inline" }));
          else console.warn(`[emoji-icons] no Phosphor icon for ${m[1]} (U+${m[1].codePointAt(0).toString(16)}) in ${file.path ?? "a doc"}, dropped it`);
          last = m.index + m[0].length;
        }
        if (last < child.value.length) out.push({ type: "text", value: child.value.slice(last).replace(/^ (?= )/, "") });
      }
      node.children = out;
    };
    visit(tree);
  };
}
