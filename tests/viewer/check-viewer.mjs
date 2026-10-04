// Opens an export's index.html over file:// in headless Chromium and checks
// that chats load, a known photo renders from media/, and the members panel
// of the fixture group lists its members.
//   node tests/viewer/check-viewer.mjs "<export folder>"
import { chromium } from "playwright";
import path from "node:path";
import { pathToFileURL } from "node:url";

const dir = process.argv[2];
if (!dir) { console.error("usage: check-viewer.mjs <export folder>"); process.exit(2); }
const exe = process.env.CHROMIUM_PATH || undefined;
const browser = await chromium.launch(exe ? { executablePath: exe } : {});
const page = await browser.newPage();
const errors = [];
page.on("pageerror", e => errors.push(String(e)));
const failed = [];
page.on("requestfailed", r => failed.push(r.url()));

const fail = msg => { console.error("FAIL: " + msg); process.exitCode = 1; };
const t0 = Date.now();
await page.goto(pathToFileURL(path.join(dir, "index.html")).href);
await page.waitForSelector("#list .loading", { state: "detached", timeout: 30000 });
console.log(`chat list ready in ${Date.now() - t0} ms`);

const listText = await page.textContent("#list");
for (const name of ["Alice Contact", "Bob WA", "WhatsApp Calls"])
  if (!listText.includes(name)) fail(`chat "${name}" not in the list`);
if (await page.isVisible("#pick")) fail("the chats.json picker is showing (chats.js not loaded)");

// A photo from media/
await page.click(`#list >> text=Alice Contact`);
const img = page.locator('#wall img[src*="WhatsApp%20Images/IMG-20260101-WA0001.jpg"], #wall img[src*="WhatsApp Images/IMG-20260101-WA0001.jpg"]');
await img.first().waitFor({ timeout: 10000 }).catch(() => fail("photo element not rendered"));
if (await img.count()) {
  await page.waitForFunction(el => el.complete, await img.first().elementHandle(), { timeout: 10000 }).catch(() => {});
  const w = await img.first().evaluate(el => el.naturalWidth);
  if (!(w > 0)) fail("photo did not load from media/");
}

// Group members panel (members.js)
const group = await page.evaluate(() => [...document.querySelectorAll("#list *")].find(e => /Family/.test(e.textContent) && e.children.length === 0)?.textContent);
await page.click(`#list >> text=${group ? group.trim() : "Family"}`);
if (!(await page.isVisible("#membtn"))) fail("members button hidden (members.js not loaded?)");
else {
  await page.click("#membtn");
  const members = await page.textContent("#pmembers");
  for (const n of ["Alice From VCF", "Bob WA"]) if (!members.includes(n)) fail(`member "${n}" missing`);
  if (!/5 members/.test(members)) fail(`unexpected member count: ${members.slice(0, 60)}`);
}
if (errors.length) fail("page errors: " + errors.join("; "));
const bad = failed.filter(u => !u.includes("VID-20250101")); // the deliberately missing video
if (bad.length) fail("failed requests: " + bad.join(", "));
await browser.close();
if (!process.exitCode) console.log("viewer OK");
