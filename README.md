# IrAutoX Launcher

لانچر Native برای پلتفرم بازی IrAutoX؛ ساخته‌شده با C++20 و Qt 6.

![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-1f6feb)
![C++](https://img.shields.io/badge/C%2B%2B-20-f97316)
![Qt](https://img.shields.io/badge/Qt-6.8-41cd52)

## امکانات

- رابط فارسی RTL با تم تیرهٔ یکپارچه و titlebar اختصاصی بدون کنترل‌های پیش‌فرض ویندوز
- آیکون‌های SVG اختصاصی برای منوها و کنترل‌های پنجره؛ بدون emoji در navigation جدید
- فونت فارسی Vazirmatn که هنگام build از منبع رسمی دریافت، Base64 و داخل Qt Resource جاسازی می‌شود
- ورود و ثبت‌نام سازگار با پروتکل فعلی سرور `irautox.ir:6768`
- نگهداری اختیاری رمز با Windows DPAPI؛ بدون ذخیرهٔ رمز به‌صورت متن ساده
- فروشگاه، جست‌وجو، صفحهٔ بازی، بنر، آیکون، نسخه و نظرات کاربران
- کتابخانهٔ محلی نسخه‌دار با مهاجرت خودکار از `library.json` لانچر پایتون
- دانلود صف‌بندی‌شده با توقف، ادامه، نمایش سرعت و Resume از HTTP Range
- بررسی اختیاری SHA-256 و جلوگیری از Zip Slip پیش از استخراج
- نصب تراکنشی: در آپدیت ناموفق، نسخهٔ سالم قبلی حفظ می‌شود
- بررسی بروزرسانی هر بازی پیش از اجرا و ثبت مدت‌زمان بازی
- شورتکات خودکار دسکتاپ برای بازی‌های نصب‌شده؛ آیکون از EXE همان بازی گرفته می‌شود
- اجرای Steam-style از طریق Game ID و single-instance IPC
- دوستان، درخواست دوستی، وضعیت آنلاین و بازی در حال اجرا
- پروفایل، آواتار، اعلان‌های Tray و اجرای خودکار همراه ویندوز
- `IrAutoXUpdater.exe` مستقل برای بررسی GitHub Releases و بروزرسانی Setup/Portable
- پنل مدیریت بازی‌ها و انتشار اطلاعیه برای نقش `admin`
- حذف ایمن بازی فقط با نشانگر نصب معتبر IrAutoX

## اجرای بازی با Command

شورتکات‌های بازی به خود لانچر اشاره می‌کنند و ID بازی را می‌فرستند:

```powershell
IrAutoXLauncher.exe --launch-game 42
```

اگر لانچر از قبل باز باشد، instance دوم از طریق IPC به instance اصلی پیام `launch:42` می‌دهد و خارج می‌شود. instance اصلی بعد از login/connection همان مسیر داخلی `check_update -> launch -> set_status/playtime` را اجرا می‌کند.

## Updater مستقل

خروجی ویندوز شامل دو فایل اصلی است:

- `IrAutoXLauncher.exe`
- `IrAutoXUpdater.exe`

Updater هر ۱۵ دقیقه آخرین GitHub Release را بررسی می‌کند. اگر نسخهٔ جدید پیدا شود، ابتدا به کاربر اطلاع می‌دهد و پس از تأیید:

- در نصب Setup، فایل `*-Setup.exe` مناسب را دانلود و به‌صورت silent اجرا می‌کند.
- در Portable، فایل `*-Portable.zip` را دانلود می‌کند، لانچر را می‌بندد، فایل‌ها را جایگزین می‌کند و لانچر جدید را دوباره اجرا می‌کند.

نسخهٔ Setup با فایل `setup.install` تشخیص داده می‌شود. Updater در Startup ویندوز با نام `IrAutoXUpdater` ثبت می‌شود و lock مستقل دارد تا چند نمونه همزمان اجرا نشوند.

## خروجی‌های ویندوز

هر اجرای GitHub Actions دو Artifact می‌سازد:

1. `IrAutoX-Launcher-vX.Y.Z-Portable.zip` — نسخهٔ پرتابل؛ شامل Launcher، Updater و Qt Runtime
2. `IrAutoX-Launcher-vX.Y.Z-Setup.exe` — اینستالر Windows با Uninstaller، Shortcut و Updater مستقل

روی Tagهایی مانند `v2.0.0` همین دو فایل به‌طور خودکار به GitHub Release متصل می‌شوند. نسخهٔ Tag با `IRAUTOX_VERSION_OVERRIDE` داخل Launcher و Updater کامپایل می‌شود.

## فونت و دارایی‌ها

قبل از build ویندوز، اسکریپت زیر Vazirmatn Regular را از repository رسمی دریافت و به Base64 تبدیل می‌کند:

```powershell
./scripts/embed-vazirmatn.ps1
```

فایل خروجی `resources/vazirmatn.b64` توسط Qt Resource داخل برنامه قرار می‌گیرد و با `QFontDatabase::addApplicationFontFromData` از حافظه لود می‌شود. اگر فایل Base64 موجود نباشد، لانچر به Tahoma/Segoe UI fallback می‌کند.

اطلاعات منبع و مجوز دارایی‌های شخص ثالث در [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) ثبت شده است.

## ساخت محلی در ویندوز

نیازمندی‌ها:

- Visual Studio 2022 Build Tools
- CMake 3.24 یا جدیدتر
- Qt 6.5 یا جدیدتر با MSVC 2022 64-bit

سپس:

```powershell
./scripts/embed-vazirmatn.ps1
./scripts/build-windows.ps1
```

یا به‌صورت دستی:

```powershell
./scripts/embed-vazirmatn.ps1
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DIRAUTOX_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix dist
windeployqt --release --compiler-runtime --no-translations dist/IrAutoXLauncher.exe
windeployqt --release --compiler-runtime --no-translations dist/IrAutoXUpdater.exe
```

## ساخت Release

پس از Merge روی `main`، یک Tag بسازید:

```bash
git tag v2.0.0
git push origin v2.0.0
```

Workflow فونت را embed می‌کند، Launcher و Updater را می‌سازد، تست‌ها را اجرا می‌کند و Portable/Setup را منتشر می‌کند.

## پروتکل و سازگاری سرور

پروتکل شبکه همان JSON طول‌دار نسخهٔ پایتون است: یک طول ۴ بایتی Big Endian و سپس UTF-8 JSON با فرم زیر:

```json
{"cmd":"login","data":{"username":"user","password":"pass"}}
```

فرمان‌های قبلی مانند `get_games`, `get_game_details`, `check_update`, `get_friends`, `publish_game` و `post_announcement` حفظ شده‌اند. فیلدهای `sha256` و `exe_sha256` اختیاری‌اند؛ بنابراین سرور قبلی بدون تغییر همچنان کار می‌کند.

## امنیت انتشار

این پروژه هنوز Code Signing ندارد. برای جلوگیری از هشدار SmartScreen در انتشار عمومی، گواهی Authenticode سازمان را به Workflow اضافه و فایل‌های Setup و Portable را پس از مرحلهٔ UPX امضا کنید.

## مجوز

[GNU Affero General Public License v3.0](LICENSE)
