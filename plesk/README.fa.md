# پنل ابری EvoHatch روی Plesk Windows

این پوشه یک برنامهٔ مستقل Node.js است: پنل روشن و فارسی، ورود مدیر، API سازگار با پنل روی ESP32 و کانال امن WebSocket برای اتصال خروجی برد. برنامه برای دامنهٔ `monitor.deuteriumstudio.ir` آماده شده، اما **DNS، گواهی SSL و تنظیمات خود هاست باید در Plesk انجام شوند**؛ از داخل این مخزن امکان تغییر هاست یا DNS وجود ندارد.

## محتویات

- `server.js` — سرور Express، API، ورود امن و WebSocket در مسیرهای `/ws/device` و `/ws/browser`
- `public/index.html` — نسخهٔ قابل میزبانی UI؛ منبع اصلی UI در `../web/index.html` است
- `package.json` و `package-lock.json` — وابستگی‌ها و فرمان‌های اجرا
- `.env.example` — نمونهٔ متغیرهای محرمانه؛ فایل واقعی `.env` را هرگز منتشر نکنید
- `test/` — تست ورود، API، WebSocket و کنترل دسترسی

این استقرار فعلی یک هاب ESP32 را با `DEVICE_ID` و `DEVICE_TOKEN` مدیریت می‌کند؛ شانزده دستگاه/ماشین متصل به همان هاب در پنل پشتیبانی می‌شوند.

## آماده‌سازی Plesk

1. در DNS دامنه، رکورد `A` برای `monitor.deuteriumstudio.ir` را به IP هاست Plesk وصل کنید. در Plesk یک subdomain با همین نام بسازید.
2. برای subdomain گواهی معتبر HTTPS نصب/فعال کنید. ESP32 به `wss://monitor.deuteriumstudio.ir:443/ws/device` وصل می‌شود.
3. در Plesk Windows افزونهٔ **Node.js** را فعال کنید، نسخهٔ Node.js 18.17 یا جدیدتر انتخاب کنید و پوشهٔ `plesk` را به‌عنوان Application Root قرار دهید.
4. تنظیمات برنامه را در Plesk چنین قرار دهید:
   - **Startup file:** `server.js`
   - **Document root:** پوشهٔ `public`
   - **Application mode:** `production`
   - سپس `npm install` را از دکمهٔ Plesk یا ترمینال برنامه اجرا کنید و برنامه را Restart کنید.
5. فعال بودن **IIS WebSocket Protocol** را در Windows Server بررسی کنید و مطمئن شوید proxy برنامه در Plesk، ارتقای اتصال WebSocket (`Upgrade` / `Connection`) را عبور می‌دهد. برای پنل وب HTTPS، مرورگر از WSS استفاده می‌کند؛ برای برد هم مسیر `/ws/device` باید به همین Node app برسد.
6. متغیرهای محیطی بخش بعد را در `Custom environment variables` افزونهٔ Node.js وارد کنید. فایل `.env` فقط برای توسعهٔ محلی است.

> پیکربندی دقیق منوهای Plesk ممکن است با نسخه و تنظیمات IIS فرق کند. پس از Restart ابتدا `https://monitor.deuteriumstudio.ir/healthz` را باز کنید؛ خروجی سالم شامل `"ok":true` است.

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
| `PORT` | اگر Plesk خودش PORT می‌سازد، آن را تغییر ندهید |

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

## به‌روزرسانی UI و تست

پس از تغییر `../web/index.html`، نسخهٔ داخل Firmware و نسخهٔ استقرار را همگام کنید:

```sh
python ../tools/html2header.py
npm run sync-ui
npm test
```

از پوشهٔ `plesk`، `npm test` باید سه تست API/ورود/WebSocket را سبز کند. برای توسعهٔ محلی می‌توانید `.env.example` را به `.env` کپی کرده و مقادیر تستی را جایگزین کنید؛ `.env` از Git مستثنا است.
