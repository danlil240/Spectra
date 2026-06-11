/**
 * Visual smoke test for Spectra docs diagram embeds.
 *
 *   # terminal 1
 *   cd docs && python3 -m http.server 8766
 *
 *   # terminal 2
 *   npx playwright install chromium
 *   node docs/scripts/verify-diagrams.mjs
 */
import { createRequire } from 'node:module';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const figmaDir = path.join(path.dirname(fileURLToPath(import.meta.url)), '../images/figma');
const { chromium } = require(path.join(figmaDir, 'node_modules/playwright'));

const baseUrl = process.env.DOCS_URL ?? 'http://127.0.0.1:8766';
const pages = ['diagrams.html', 'architecture.html', 'index.html', 'ros2-adapter.html'];
const outDir = path.join(path.dirname(fileURLToPath(import.meta.url)), '../.playwright-verify');

fs.mkdirSync(outDir, { recursive: true });

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });

let failed = 0;

for (const slug of pages) {
    const url = `${baseUrl}/${slug}`;
    await page.goto(url, { waitUntil: 'networkidle' });

    const broken = await page.evaluate(() =>
        [...document.querySelectorAll('img[src*="images/diagrams/"], img[src*="images/figma/"]')].filter((img) => !img.complete || img.naturalWidth === 0).map((img) => img.getAttribute('src')),
    );

    if (broken.length) {
        console.error(`FAIL ${slug}: broken diagram images`, broken);
        failed += 1;
    } else {
        const count = await page.locator('img[src*="images/diagrams/"], img[src*="images/figma/"]').count();
        console.log(`OK   ${slug}: ${count} diagram image(s) loaded`);
    }

    await page.screenshot({ path: path.join(outDir, slug.replace('.html', '.png')), fullPage: true });
}

await browser.close();

if (failed) {
    process.exit(1);
}

console.log(`Screenshots written to ${outDir}`);
