// Renders the .dc.html mockup sources in design/mockups/source into plain static HTML
// (design/mockups/html) and PNG screenshots (design/screenshots) with headless Edge.
//
// Usage: node design/tools/render-mockups.mjs [--no-png]
//
// The sources use a small template format from the design canvas: {{path}} holes,
// <sc-for list="{{x}}" as="y"> loops and <sc-if value="{{x}}"> conditionals, with the
// data coming from the renderVals() method in each file's <script data-dc-script>.

import { readFileSync, writeFileSync, readdirSync, mkdirSync, existsSync, statSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, dirname, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { execFileSync } from "node:child_process";

const designDir = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const srcDir = join(designDir, "mockups", "source");
const htmlDir = join(designDir, "mockups", "html");
const shotDir = join(designDir, "screenshots");
const withPng = !process.argv.includes("--no-png");

const canvas = JSON.parse(readFileSync(join(srcDir, "canvas.json"), "utf8"));

class DCLogic {
  constructor() { this.props = {}; this.state = {}; }
  setState(s) { Object.assign(this.state, s); }
  forceUpdate() {}
}

const between = (s, open, close) => {
  const a = s.indexOf(open);
  const b = s.indexOf(close, a + open.length);
  return a < 0 || b < 0 ? "" : s.slice(a + open.length, b);
};

const escapeHtml = (v) => String(v ?? "").replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");

function lookup(scope, path) {
  path = path.trim();
  if (path === "true") return true;
  if (path === "false") return false;
  return path.split(".").reduce((o, k) => (o == null ? undefined : o[k]), scope);
}

const holes = (s, scope) => s.replace(/\{\{\s*([^}]+?)\s*\}\}/g, (_, p) => escapeHtml(lookup(scope, p)));

function render(tpl, scope) {
  const open = /<sc-(for|if)\b([^>]*)>/.exec(tpl);
  if (!open) return holes(tpl, scope);
  const tag = `sc-${open[1]}`;
  const start = open.index + open[0].length;
  const re = new RegExp(`<${tag}\\b[^>]*>|</${tag}>`, "g");
  re.lastIndex = start;
  let depth = 1, m;
  while ((m = re.exec(tpl))) {
    depth += m[0].startsWith("</") ? -1 : 1;
    if (depth === 0) break;
  }
  if (!m) throw new Error(`unclosed <${tag}>`);
  const inner = tpl.slice(start, m.index);
  const attr = (name) => (new RegExp(`${name}="\\{\\{\\s*([^}]+?)\\s*\\}\\}"`).exec(open[2]) || [])[1];
  let out = "";
  if (open[1] === "for") {
    const as = (/as="([^"]+)"/.exec(open[2]) || [])[1] || "item";
    (lookup(scope, attr("list")) || []).forEach((item, i) => { out += render(inner, { ...scope, [as]: item, $index: i }); });
  } else if (lookup(scope, attr("value"))) {
    out = render(inner, scope);
  }
  return holes(tpl.slice(0, open.index), scope) + out + render(tpl.slice(m.index + m[0].length), scope);
}

function edgePath() {
  const candidates = [
    "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
    "C:/Program Files/Microsoft/Edge/Application/msedge.exe",
    "C:/Program Files/Google/Chrome/Application/chrome.exe",
  ];
  return candidates.find(existsSync);
}

// msedge.exe can return before the headless browser has written the PNG. Starting the next
// screenshot before then makes the two runs collide, so wait until the file is new and stable.
function waitForScreenshot(png, started, timeoutMs = 30000) {
  const sleep = (ms) => Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, ms);
  let lastSize = -1;
  for (const end = Date.now() + timeoutMs; Date.now() < end; sleep(250)) {
    if (!existsSync(png)) continue;
    const { mtimeMs, size } = statSync(png);
    if (mtimeMs >= started - 1000 && size > 0 && size === lastSize) return;
    lastSize = mtimeMs >= started - 1000 ? size : -1;
  }
  throw new Error(`screenshot not written within ${timeoutMs / 1000} s: ${png}`);
}

mkdirSync(htmlDir, { recursive: true });
mkdirSync(join(shotDir, "explorations"), { recursive: true });
const browser = withPng ? edgePath() : null;
if (withPng && !browser) throw new Error("Edge or Chrome not found; run with --no-png");
// A private profile keeps headless runs independent of any browser window the user has open;
// sharing the default profile makes the screenshot step exit without writing a file.
const profileDir = browser ? mkdtempSync(join(tmpdir(), "recto-render-")) : null;

for (const file of readdirSync(srcDir).filter((f) => f.endsWith(".dc.html"))) {
  const src = readFileSync(join(srcDir, file), "utf8");
  const xdc = between(src, "<x-dc>", "</x-dc>");
  const helmet = between(xdc, "<helmet>", "</helmet>");
  const body = xdc.replace(/<helmet>[\s\S]*?<\/helmet>/, "");
  const script = /<script type="text\/x-dc" data-dc-script[^>]*>([\s\S]*?)<\/script>/.exec(src)[1];
  const Component = new Function("DCLogic", `${script}\nreturn Component;`)(DCLogic);
  const vals = new Component().renderVals();
  const title = between(src, "<title>", "</title>");
  const html = `<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n<title>${title}</title>\n${helmet.trim()}\n</head>\n<body>\n${render(body, vals).trim()}\n</body>\n</html>\n`
    .replace(/href="([\w-]+)\.dc\.html"/g, 'href="$1.html"');
  const name = file.replace(/\.dc\.html$/, "");
  const htmlPath = join(htmlDir, `${name}.html`);
  writeFileSync(htmlPath, html);

  if (browser) {
    const board = canvas.boards[file];
    const png = join(shotDir, name.startsWith("D-") ? "" : "explorations", `${name}.png`);
    const started = Date.now();
    execFileSync(browser, ["--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
      `--user-data-dir=${profileDir}`, `--window-size=${board.w},${board.h}`, "--virtual-time-budget=8000",
      `--screenshot=${png}`, pathToFileURL(htmlPath).href], { stdio: ["ignore", "ignore", "pipe"] });
    waitForScreenshot(png, started);
  }
  console.log(name);
}
if (profileDir) rmSync(profileDir, { recursive: true, force: true });
