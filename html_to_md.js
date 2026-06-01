#!/usr/bin/env node
/*
 * Convert a CMU 15-445 project HTML page to a clean GitHub-Flavored Markdown README.
 *
 * Usage: node html_to_md.js <input.html> <output.md> [assetsDir] [baseUrl]
 */
'use strict';

const fs = require('fs');
const path = require('path');
const { URL } = require('url');
const { JSDOM } = require('jsdom');
const TurndownService = require('turndown');
const { gfm } = require('turndown-plugin-gfm');

const [, , inputPath, outputPath, assetsDirArg, baseUrlArg] = process.argv;
if (!inputPath || !outputPath) {
    console.error('Usage: html_to_md.js <input.html> <output.md> [assetsDir] [baseUrl]');
    process.exit(2);
}

const outputDir = path.dirname(path.resolve(outputPath));
const assetsDir = assetsDirArg
    ? path.resolve(assetsDirArg)
    : path.join(outputDir, 'assets');

fs.mkdirSync(assetsDir, { recursive: true });

const html = fs.readFileSync(inputPath, 'utf8');
const dom = new JSDOM(html);
const doc = dom.window.document;

// Resolve baseUrl: explicit CLI arg → <meta property="og:url"> directory → fallback.
function deriveBaseUrlFromMeta() {
    const meta = doc.querySelector('meta[property="og:url"]');
    const content = meta && meta.getAttribute('content');
    if (!content) return null;
    try {
        const u = new URL(content);
        // Strip filename (e.g. /spring2026/project2/index.html → /spring2026/project2/)
        u.pathname = u.pathname.replace(/[^/]*$/, '');
        u.search = '';
        u.hash = '';
        return u.toString();
    } catch (_) {
        return null;
    }
}
const baseUrl =
    baseUrlArg ||
    deriveBaseUrlFromMeta() ||
    'https://15445.courses.cs.cmu.edu/spring2026/project4/';

// ---- 1. Pick the content root ----------------------------------------------
const root =
    doc.querySelector('.container.main-content') ||
    doc.querySelector('main') ||
    doc.querySelector('article') ||
    doc.body;

// ---- 2. Scrub layout / chrome / scripts -----------------------------------
const KILL_SELECTORS = [
    'script',
    'style',
    'noscript',
    'header',
    'footer',
    'nav',
    '.bg-image',
    '.published',
    '.page-header .float-end',
    '.dropdown',
    '.navbar',
    'svg.bi',
    'i.fa, i.fas, i.far, i.fab',
];
for (const sel of KILL_SELECTORS) {
    root.querySelectorAll(sel).forEach((n) => n.remove());
}

// Strip the giant page-title chrome — keep just the text.
const pageHeader = root.querySelector('h1.page-header');
if (pageHeader) {
    const span = pageHeader.querySelector('span');
    const title = (span ? span.textContent : pageHeader.textContent).trim();
    const h1 = doc.createElement('h1');
    h1.textContent = title;
    pageHeader.replaceWith(h1);
}

// ---- 3. Rewrite URLs --------------------------------------------------------
function absolutize(url) {
    try {
        return new URL(url, baseUrl).toString();
    } catch (_) {
        return url;
    }
}

root.querySelectorAll('a[href]').forEach((a) => {
    const href = a.getAttribute('href');
    if (!href) return;
    if (href.startsWith('#')) return; // in-page anchors
    a.setAttribute('href', absolutize(href));
    // strip the onclick GA tracking junk
    a.removeAttribute('onclick');
});

// ---- 4. Collect image references, schedule downloads -----------------------
const imageJobs = []; // { absUrl, localRel }
const seenImages = new Map(); // absUrl -> localRel

function planImage(absUrl) {
    if (seenImages.has(absUrl)) return seenImages.get(absUrl);
    const u = new URL(absUrl);
    const baseName = path.basename(u.pathname) || 'image';
    let localRel = path.posix.join('assets', baseName);
    // avoid collisions if two source paths share a basename
    let n = 1;
    const taken = new Set(seenImages.values());
    while (taken.has(localRel)) {
        const ext = path.extname(baseName);
        const stem = baseName.slice(0, -ext.length) || baseName;
        localRel = path.posix.join('assets', `${stem}-${n}${ext}`);
        n += 1;
    }
    seenImages.set(absUrl, localRel);
    imageJobs.push({ absUrl, localRel });
    return localRel;
}

root.querySelectorAll('img[src]').forEach((img) => {
    const src = img.getAttribute('src');
    if (!src) return;
    const abs = absolutize(src);
    // Skip course logo / icons — we only want diagrams inside the article.
    if (/cmu-db-group|cmudb-icon|favicon|apple-touch-icon|twitter-card/.test(abs)) {
        img.remove();
        return;
    }
    const local = planImage(abs);
    img.setAttribute('src', local);
    if (!img.getAttribute('alt')) {
        img.setAttribute('alt', path.basename(local, path.extname(local)));
    }
});

// Also rewrite <a href="img/..."> wrappers around images so the link points at the local file.
root.querySelectorAll('a[href]').forEach((a) => {
    const href = a.getAttribute('href') || '';
    if (/\.(png|jpe?g|gif|svg|webp)(\?.*)?$/i.test(href)) {
        const abs = absolutize(href);
        if (/cmu-db-group|cmudb-icon|favicon|apple-touch-icon|twitter-card/.test(abs)) return;
        const local = planImage(abs);
        a.setAttribute('href', local);
    }
});

// ---- 5. Configure Turndown --------------------------------------------------
const td = new TurndownService({
    headingStyle: 'atx',
    codeBlockStyle: 'fenced',
    fence: '```',
    bulletListMarker: '-',
    emDelimiter: '_',
    strongDelimiter: '**',
    linkStyle: 'inlined',
});
td.use(gfm);

// Preserve language hints on code blocks of the form <pre><code class="language-xxx">.
td.addRule('fencedCodeWithLang', {
    filter: (node) =>
        node.nodeName === 'PRE' &&
        node.firstChild &&
        node.firstChild.nodeName === 'CODE',
    replacement: (_content, node) => {
        const code = node.firstChild;
        const className = code.getAttribute('class') || '';
        const m = className.match(/language-([\w+-]+)/);
        const lang = m ? m[1] : '';
        const text = code.textContent.replace(/\n$/, '');
        return `\n\n\`\`\`${lang}\n${text}\n\`\`\`\n\n`;
    },
});

// <pre> without nested <code> (the page uses plain <pre class="shadow-sm p-2"> for shell snippets).
td.addRule('barePre', {
    filter: (node) => node.nodeName === 'PRE' && !(node.firstChild && node.firstChild.nodeName === 'CODE'),
    replacement: (_content, node) => {
        const text = node.textContent.replace(/^\n+|\n+$/g, '');
        return `\n\n\`\`\`\n${text}\n\`\`\`\n\n`;
    },
});

// Warning / callout boxes — render as a blockquote with a leading marker.
td.addRule('warningCallout', {
    filter: (node) =>
        node.nodeName === 'P' &&
        node.classList &&
        node.classList.contains('warning'),
    replacement: (content) => {
        const clean = content.replace(/\s+/g, ' ').trim();
        return `\n\n> **\u26A0\uFE0F WARNING:** ${clean}\n\n`;
    },
});

// ---- 6. Convert --------------------------------------------------------------
const markdownBody = td.turndown(root.innerHTML);

const pageTitle =
    (doc.querySelector('meta[property="og:title"]') &&
        doc.querySelector('meta[property="og:title"]').getAttribute('content')) ||
    (doc.querySelector('title') && doc.querySelector('title').textContent) ||
    'Project';
const cleanTitle = pageTitle.replace(/\s*\|.*$/, '').trim();

const header =
    `# ${cleanTitle}\n\n` +
    `> Source: <${baseUrl}>\n\n` +
    `---\n\n`;

// Tighten excessive blank-line runs that turndown sometimes emits.
const finalMd = (header + markdownBody)
    .replace(/\n{3,}/g, '\n\n')
    .replace(/[ \t]+$/gm, '')
    .trim() + '\n';

fs.writeFileSync(outputPath, finalMd);
console.log(`Wrote ${outputPath} (${finalMd.length} bytes)`);

// ---- 7. Download images -----------------------------------------------------
async function downloadOne({ absUrl, localRel }) {
    const dest = path.join(outputDir, localRel);
    if (fs.existsSync(dest)) {
        console.log(`  [skip] ${localRel} (already present)`);
        return;
    }
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    const res = await fetch(absUrl);
    if (!res.ok) {
        throw new Error(`fetch ${absUrl} -> HTTP ${res.status}`);
    }
    const buf = Buffer.from(await res.arrayBuffer());
    fs.writeFileSync(dest, buf);
    console.log(`  [ok]   ${localRel}  (${buf.length} bytes)`);
}

(async () => {
    console.log(`Downloading ${imageJobs.length} image(s) to ${path.relative(process.cwd(), assetsDir)}/`);
    for (const job of imageJobs) {
        try {
            await downloadOne(job);
        } catch (err) {
            console.error(`  [fail] ${job.absUrl}: ${err.message}`);
        }
    }
})();
