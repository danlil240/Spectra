/**
 * Export all doc diagrams (native + FigJam SVG) to 2x PNG for markdown / fallbacks.
 *
 * Setup (once): cd docs/images/figma && npm init -y && npm install playwright && npx playwright install chromium
 * Run: node docs/images/export-all-diagrams.mjs
 */
import { createRequire } from 'node:module';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
const { chromium } = require(path.join(root, 'figma/node_modules/playwright'));

const SCALE = 2;
const sources = [
    path.join(root, 'diagrams'),
    path.join(root, 'figma'),
];

async function exportSvg(page, svgPath, pngPath) {
    const svg = fs.readFileSync(svgPath, 'utf8');
    const encoded = Buffer.from(svg).toString('base64');

    await page.setContent(
        `<!doctype html><html><head><style>
      * { box-sizing: border-box; }
      body { margin: 0; padding: 32px; background: #f4f7fb; display: flex; justify-content: center; }
      img { display: block; width: auto; height: auto; max-width: none; }
    </style></head><body><img id="diagram" alt="" src="data:image/svg+xml;base64,${encoded}" /></body></html>`,
        { waitUntil: 'networkidle' },
    );

    const img = page.locator('#diagram');
    await img.waitFor({ state: 'visible' });
    const box = await img.boundingBox();
    if (!box) {
        throw new Error(`No bounding box for ${svgPath}`);
    }

    const pad = 32;
    await page.setViewportSize({
        width: Math.ceil(box.width + pad * 2),
        height: Math.ceil(box.height + pad * 2),
    });

    await img.screenshot({ path: pngPath, type: 'png', scale: 'device' });
}

const browser = await chromium.launch();
const context = await browser.newContext({ deviceScaleFactor: SCALE });
const page = await context.newPage();

for (const dir of sources) {
    if (!fs.existsSync(dir)) {
        continue;
    }
    for (const name of fs.readdirSync(dir).filter((f) => f.endsWith('.svg')).sort()) {
        const svgPath = path.join(dir, name);
        const pngPath = path.join(dir, name.replace(/\.svg$/, '.png'));
        await exportSvg(page, svgPath, pngPath);
        console.log(`[${SCALE}x] ${path.relative(root, svgPath)} -> ${path.basename(pngPath)}`);
    }
}

await browser.close();
