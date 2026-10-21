# پنل ابری EvoHatch روی Plesk Windows

این پوشه یک برنامهٔ مستقل Node.js است: پنل روشن و فارسی، ورود مدیر، API سازگار با پنل روی ESP32 و کانال امن WebSocket برای اتصال خروجی برد. برنامه برای دامنهٔ `monitor.deuteriumstudio.ir` آماده شده، اما **DNS، گواهی SSL و تنظیمات خود هاست باید در Plesk انجام شوند**؛ از داخل این مخزن امکان تغییر هاست یا DNS وجود ندارد.

## محتویات

- `server.js` — سرور Express، API، ورود امن و WebSocket در مسیرهای `/ws/device` و `/ws/browser`
- `web.config` — پیکربندی IIS/iisnode برای `server.js`، API و WebSocket
- `public/index.html` — نسخهٔ قابل میزبانی UI؛ منبع اصلی UI در `../web/index.html` است
- `package.json` و `package-lock.json` — وابستگی‌ها و فرمان‌های اجرا
- `.env.example` — نمونهٔ متغیرهای محرمانه؛ فایل واقعی `.env` را هرگز منتشر نکنید
- `test/` — تست ورود، API، WebSocket و کنترل دسترسی

این استقرار فعلی یک هاب ESP32 را با `DEVICE_ID` و `DEVICE_TOKEN` مدیریت می‌کند؛ شانزده دستگاه/ماشین متصل به همان هاب در پنل پشتیبانی می‌شوند.

## آماده‌سازی Plesk

1. در DNS دامنه، رکورد `A` برای `monitor.deuteriumstudio.ir` را به IP هاست Plesk وصل کنید و در Plesk یک subdomain با همین نام بسازید.
2. برای subdomain گواهی معتبر HTTPS نصب/فعال کنید. ESP32 به `wss://monitor.deuteriumstudio.ir:443/ws/device` وصل می‌شود.
3. **محتویات** پوشهٔ `plesk/` (فایل‌های `server.js`, `package.json`, `web.config` و پوشهٔ `public/`) را داخل ریشهٔ subdomain آپلود کنید؛ خود پوشهٔ `plesk` را یک لایهٔ اضافه توی ریشه نگذارید، مگر اینکه همان مسیر را در Plesk انتخاب کنید.
4. در Plesk Windows افزونهٔ **Node.js** را فعال کنید و این مقادیر را تنظیم کنید:
   - **Application Root** و **Document Root:** هر دو همان پوشه‌ای که `server.js` و `web.config` در آن هستند. در Plesk Windows این دو مسیر باید یکی باشند؛ `public/` را به‌عنوان Document Root نگذارید چون Express خودش UI را از آن پوشه سرو می‌کند.
   - **Startup file:** `server.js`
   - **Application mode:** `production`
   - سپس **NPM install** را اجرا کنید و برنامه را Restart کنید.
5. فایل `web.config` این پوشه را در همان Application/Document Root بگذارید. اگر Plesk برای برنامه خودش handler یا rewrite ساخته، `<rule>`ها را در همان `<rewrite><rules>` ادغام کنید و از تعریف دوبارهٔ یک handler/rule هم‌نام خودداری کنید. پیکربندی `web.config` همهٔ URLها، از جمله `/api/*` و `/ws/*` را به `server.js` می‌فرستد.
6. WebSocket Upgrade در IIS/iisnode باید عبور کند. فایل نمونه برای iisnode، WebSocket داخلی IIS را غیرفعال می‌کند تا با WebSocket خود Node تداخل نداشته باشد. اگر Plesk یا مدیر سرور این تنظیم را مدیریت می‌کند، از پشتیبانی هاست بخواهید WebSocket را برای Node app فعال و مسیرهای `/ws/device` و `/ws/browser` را عبور دهد.
7. متغیرهای محیطی بخش بعد را در `Custom environment variables` افزونهٔ Node.js وارد کنید. روی Plesk Windows **`PORT` را دستی روی 3000 نگذارید**: iisnode آن را به‌صورت named pipe به برنامه می‌دهد و `server.js` فعلی همان مقدار را مستقیم مصرف می‌کند. فایل `.env` فقط برای توسعهٔ محلی است.

> بعد از Restart ابتدا `https://monitor.deuteriumstudio.ir/healthz` را باز کنید؛ پاسخ سالم شامل `"ok":true` است. مستند Plesk می‌گوید در Windows، Application Root و Document Root یکی باشند و برای WebSocket باید rule مسیر WebSocket به startup file برسد: [راهنمای Node.js روی Plesk](https://support.plesk.com/hc/en-us/articles/12377084794263-How-to-install-and-run-Node-js-application-in-Plesk) و [راهنمای WebSocket روی Plesk](https://support.plesk.com/hc/en-us/articles/12377246437399-Does-Node-js-on-Plesk-support-WebSockets-socket-io).

## کلیدها و ورود

سه مقدار محرمانه را جداگانه بسازید. در ترمینال Node یا PowerShell:

```powershell
node -e "console.log(require('crypto').randomBytes(32).toString('hex'))"
```

این فرمان را برای `SESSION_SECRET` و `DEVICE_TOKEN` جداگانه اجرا کنید؛ هر دو باید تصادفی و متفاوت باشند. سپس در Plesk این متغیرها را تنظیم کنید:

| متغیر | مقدار |
|---|---|
| `NODE_ENV` | `production` |
| `PUBLIC_ORIGIN` | `https://monitor.deuteriumstudio.ir` |
| `ADMIN_USERNAME` | نام کاربری دلخواه مدیر |
| `ADMIN_PASSWORD` | رمز یکتای قوی، حداقل ۱۲ نویسه (ترجیحاً طولانی‌تر) |
| `SESSION_SECRET` | رشتهٔ تصادفی اول |
| `DEVICE_ID` | `hub-01` (یا شناسهٔ دلخواه، باید با تنظیم برد یکی باشد) |
| `DEVICE_TOKEN` | رشتهٔ تصادفی دوم |
| `PORT` | در Plesk Windows دستی تعریف نکنید؛ iisnode آن را به شکل named pipe تنظیم می‌کند |

سرور برای مقادیر محرمانهٔ لازم مقدار پیش‌فرض ناامن ندارد؛ اگر آن‌ها تنظیم نشوند، عمداً اجرا نمی‌شود. نشست مدیر در کوکی `HttpOnly`, `Secure` و `SameSite=Strict` نگهداری می‌شود و درخواست‌های تغییردهنده از مبدأ غیرمجاز رد می‌شوند. از رمز مدیر یا توکن برد در سورس عمومی، پیام‌رسان یا Git استفاده نکنید.

## اتصال ESP32

1. در مخزن پروژه، از فایل نمونه یک فایل محلی بسازید:

   ```powershell
   Copy-Item include/cloud_secrets.h.example include/cloud_secrets.h
   ```

2. محتوای فایل محلی `include/cloud_secrets.h` را با شناسه و **همان** `DEVICE_TOKEN` که در Plesk تنظیم کردید پر کنید:

   ```cpp
   #pragma once
   #define CLOUD_DEVICE_ID "hub-01"
   #define CLOUD_DEVICE_TOKEN "توکن-تصادفی-ساخته-شده-برای-همین-برد"
   ```

   `include/cloud_secrets.h` در `.gitignore` است و نباید Commit شود. میزبان و مسیر WSS در `src/cloud_config.h` روی دامنهٔ خواسته‌شده تنظیم شده‌اند. آن فایل گواهی ریشهٔ Let's Encrypt را برای اعتبارسنجی TLS دارد؛ اگر گواهی Plesk از CA دیگری باشد، گواهی ریشهٔ همان CA را جایگزین کنید، نه اینکه اعتبارسنجی TLS را خاموش کنید.

3. PlatformIO وابستگی WebSocket را از `platformio.ini` نصب می‌کند. سپس Firmware را بسازید و روی برد فلش کنید:

   ```powershell
   pio run -t upload
   ```

4. برد را به Wi-Fi دارای اینترنت وصل کنید. اتصال از ESP32 به سرور **خروجی** و رمزنگاری‌شده است؛ نیازی به باز کردن پورت ورودی روی مودم یا IP ثابت برای برد نیست. در `monitor.deuteriumstudio.ir` وارد پنل شوید؛ وضعیت اتصال ابری باید برقرار شود.

## رفتار و API

- وضعیت‌ها و لاگ مودم از برد به سرور ارسال و از WebSocket به صفحهٔ مدیر پخش می‌شوند.
- تنظیمات Wi-Fi، سرور، سیم‌کارت، دستگاه‌ها و فرمان‌های GSM از API به کانال WSS برد فرستاده می‌شوند. اگر برد آفلاین باشد، API خطای مناسب می‌دهد و فرمان را موفق جلوه نمی‌دهد.
- مسیرهای محلی `/api/state`, `/api/scan`, `/api/ping`, `/api/gsm`, `/api/wifi`, `/api/server`, `/api/sim`, `/api/device` در پنل ابری هم وجود دارند. `/api/settings` نیز فرمان تنظیمات تأییدشونده را به STM32 می‌فرستد.
- دادهٔ وضعیت زنده و لاگ‌های ابری در حافظهٔ برنامه هستند و با Restart سرور از بین می‌روند؛ تنظیمات اصلی همچنان در NVS برد و EEPROM/حافظهٔ STM32 باقی می‌مانند.
- داشبورد محلی `192.168.4.1` و داشبورد ابری هر دو از UI فارسی و روشن استفاده می‌کنند. پنل محلی همچنان بدون اینترنت قابل دسترس است.

## رفع خطای `iisnode 500.1002 / HRESULT 0x2`

این کد خطا به‌تنهایی علت قطعی را مشخص نمی‌کند؛ مسیر startup، تنظیم handler/rewrite، دسترسی لاگ و راه‌اندازی named pipe همگی باید بررسی شوند. در این پروژه یک ناسازگاری واقعی با PORT نام‌پایپی اصلاح شده است. موارد زیر را به‌ترتیب بررسی کنید:

1. در Plesk مطمئن شوید **Application Root** همان جایی است که `server.js`, `package.json` و `web.config` قرار دارند و **Application Startup File دقیقاً `server.js`** است.
2. در همان صفحهٔ Node.js روی **NPM install** بزنید تا `node_modules/express`, `ws` و `dotenv` نصب شوند.
3. `PORT` را در Plesk روی `3000` تنظیم نکنید. iisnode در Windows مقدار `PORT` را به شکل named pipe می‌دهد؛ نسخهٔ فعلی `server.js` این مقدار متنی را حفظ می‌کند و با همان `server.listen(path)` اجرا می‌شود. اگر از نسخهٔ قبلی برنامه استفاده می‌کنید، `plesk/server.js` جدید را دوباره آپلود کنید.
4. `web.config` را در Application/Document Root بگذارید. اگر فایل فعلی شما `<httpErrors>` با مسیرهای `error_docs` دارد، آن بخش را لازم نیست پاک کنید: ویژگی `existingResponse="PassThrough"` را به همان تگ اضافه کنید و handler/rewrite/`<iisnode>` فایل نمونه را در همان `<system.webServer>` ادغام کنید؛ `<system.webServer>` یا `<httpErrors>` تکراری نسازید. بخش‌های غیرمرتبط موجود مانند `<tracing>` و `<system.web>/<compilation>` را حفظ کنید.
5. در **Plesk > Node.js > Logs** یا پوشهٔ لاگ iisnode، stderr برنامه را ببینید. هویت Application Pool باید روی فایل‌های app دسترسی Read/Execute و روی پوشهٔ لاگ iisnode دسترسی Modify داشته باشد؛ اگر دسترسی ندارید، از پشتیبانی هاست بخواهید ACL را اصلاح کند. در `web.config` نمونه، `devErrorsEnabled="false"` برای production است؛ فقط هنگام عیب‌یابی موقتاً آن را `true` کنید و بعد دوباره `false` بگذارید.
6. بعد از Restart، `https://monitor.deuteriumstudio.ir/healthz` باید JSON برگرداند. اگر صفحهٔ IIS هنوز خطا می‌دهد، در Plesk بررسی کنید rewrite درخواست‌های `/api/*` و `/ws/*` را به همین `server.js` می‌رساند.

## به‌روزرسانی UI و تست

پس از تغییر `../web/index.html`، نسخهٔ داخل Firmware و نسخهٔ استقرار را همگام کنید:

```sh
python ../tools/html2header.py
npm run sync-ui
npm test
```

از پوشهٔ `plesk`، `npm test` باید همهٔ تست‌های API/ورود/WebSocket و تشخیص PORT نام‌پایپی را سبز کند. برای توسعهٔ محلی می‌توانید `.env.example` را به `.env` کپی کرده و مقادیر تستی را جایگزین کنید؛ `.env` از Git مستثنا است.
