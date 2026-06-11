/**
 * Export FigJam SVG diagrams to crisp PNGs for static docs.
 * One-time setup (in this directory):
 *   npm init -y && npm install playwright && npx playwright install chromium
 *
 * Re-export after FigJam edits:
 *   node export-diagrams.mjs
 */
import { chromium } from 'playwright';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const dir = path.dirname(fileURLToPath(import.meta.url));
const svgs = fs.readdirSync(dir).filter((f) => f.endsWith('.svg')).sort();

const browser = await chromium.launch();
const page = await browser.newPage();

for (const name of svgs) {
    const svgPath = path.join(dir, name);
    const pngPath = path.join(dir, name.replace(/\.svg$/, '.png'));
    const svg = fs.readFileSync(svgPath, 'utf8');
    const encoded = Buffer.from(svg).toString('base64');

    await page.setContent(
        `<!doctype html><html><head><style>
      * { box-sizing: border-box; }
      body { margin: 0; padding: 24px; background: #f4f7fb; display: flex; justify-content: center; }
      img { display: block; max-width: none; }
    </style></head><body><img id="diagram" alt="" src="data:image/svg+xml;base64,${encoded}" /></body></html>`,
        { waitUntil: 'networkidle' },
    );

    const img = page.locator('#diagram');
    await img.waitFor({ state: 'visible' });
    const box = await img.boundingBox();
    if (!box) {
        throw new Error(`No bounding box for ${name}`);
    }

    const pad = 24;
    await page.setViewportSize({
        width: Math.ceil(box.width + pad * 2),
        height: Math.ceil(box.height + pad * 2),
    });

    await img.screenshot({ path: pngPath, type: 'png' });
    console.log(`exported ${name} -> ${path.basename(pngPath)}`);
}

await browser.close();
