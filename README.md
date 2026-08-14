# IrAutoX Launcher

لانچر بومی و Native برای پلتفرم بازی IrAutoX؛ بازنویسی‌شده با C++20 و Qt 6.

![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-1f6feb)
![C++](https://img.shields.io/badge/C%2B%2B-20-f97316)
![Qt](https://img.shields.io/badge/Qt-6.8-41cd52)

## امکانات

- رابط مدرن فارسی، راست‌چین و واکنش‌گرا با طراحی تیرهٔ نارنجی
- ورود و ثبت‌نام سازگار با پروتکل فعلی سرور `irautox.ir:6768`
- نگهداری اختیاری رمز با Windows DPAPI؛ بدون ذخیرهٔ رمز به‌صورت متن ساده
- فروشگاه، جست‌وجو، صفحهٔ بازی، بنر، آیکون، نسخه و نظرات کاربران
- کتابخانهٔ محلی نسخه‌دار با مهاجرت خودکار از `library.json` لانچر پایتون
- دانلود صف‌بندی‌شده با توقف، ادامه، نمایش سرعت و Resume از HTTP Range
- بررسی اختیاری SHA-256 و جلوگیری از Zip Slip پیش از استخراج
- نصب تراکنشی: در آپدیت ناموفق، نسخهٔ سالم قبلی حفظ می‌شود
- بررسی بروزرسانی پیش از اجرا و ثبت مدت‌زمان بازی
- دوستان، درخواست دوستی، وضعیت آنلاین و بازی در حال اجرا
- پروفایل، آواتار، اعلان‌های Tray و اجرای خودکار همراه ویندوز
- پنل مدیریت بازی‌ها و انتشار اطلاعیه برای نقش `admin`
- حذف ایمن بازی فقط با نشانگر نصب معتبر IrAutoX

## خروجی‌های ویندوز

هر اجرای GitHub Actions دو Artifact می‌سازد:

1. `IrAutoX-Launcher-vX.Y.Z-Portable.zip` — نسخهٔ پرتابل؛ استخراج و اجرای مستقیم `IrAutoXLauncher.exe`
2. `IrAutoX-Launcher-vX.Y.Z-Setup.exe` — اینستالر استاندارد Windows با Uninstaller و Shortcut

روی Tagهایی مانند `v2.0.0` همین دو فایل به‌طور خودکار به GitHub Release متصل می‌شوند. فایل اجرایی لانچر پیش از بسته‌بندی با UPX فشرده می‌شود.

## ساخت محلی در ویندوز

نیازمندی‌ها:

- Visual Studio 2022 Build Tools
- CMake 3.24 یا جدیدتر و Ninja
- Qt 6.5 یا جدیدتر با MSVC 2022 64-bit

سپس:

```powershell
./scripts/build-windows.ps1
```

یا به‌صورت دستی:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DIRAUTOX_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix dist
windeployqt --release --compiler-runtime --no-translations dist/IrAutoXLauncher.exe
```

## ساخت Release

پس از Merge روی `main`، یک Tag بسازید:

```bash
git tag v2.0.0
git push origin v2.0.0
```

Workflow فایل پرتابل و Setup را می‌سازد، تست‌ها را اجرا می‌کند و Release را منتشر می‌کند.

## پروتکل و سازگاری سرور

پروتکل شبکه همان JSON طول‌دار نسخهٔ پایتون است: یک طول ۴ بایتی Big Endian و سپس UTF-8 JSON با فرم زیر:

```json
{"cmd":"login","data":{"username":"user","password":"pass"}}
```

فرمان‌های قبلی مانند `get_games`, `get_game_details`, `check_update`, `get_friends`, `publish_game` و `post_announcement` حفظ شده‌اند. فیلدهای جدید `sha256` و `exe_sha256` اختیاری‌اند؛ بنابراین سرور قبلی بدون تغییر همچنان کار می‌کند.

## امنیت انتشار

این پروژه هنوز Code Signing ندارد. برای جلوگیری از هشدار SmartScreen در انتشار عمومی، گواهی Authenticode سازمان را به Workflow اضافه و فایل‌های Setup و Portable را پس از مرحلهٔ UPX امضا کنید.

## مجوز

[GNU Affero General Public License v3.0](LICENSE)
