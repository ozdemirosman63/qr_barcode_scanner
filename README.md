📷 QR & Barcode Scanner from Paper (C++ / OpenCV + ZBar)

A real-time QR & barcode scanner written in C++, using OpenCV for camera & image processing and ZBar for decoding.
It detects codes printed on paper, applies bird’s-eye view (perspective) correction, and logs results to decoded.txt with timestamps. Also supports on-demand snapshots and duplicate-log cooldown.

✨ Features

✅ Real-time camera input with OpenCV

✅ Detects & decodes:

QR Codes

EAN-13 (product barcodes)

ISBN-13 (books)

✅ Perspective correction for tilted papers

✅ Automatic logging to decoded.txt (timestamped)

✅ Press S to save a snapshot into snapshots/

✅ Cooldown cache to avoid duplicate logs

📦 Project Structure
.
├─ CMakeLists.txt
├─ main.cpp
├─ decoded.txt          # (created at runtime)
└─ snapshots/           # (auto-created; holds saved frames)


Not: Varsayılan yollar kodda LOG_DIR ve SNAP_DIR makrolarıyla ayarlanabiliyor.

🧰 Requirements

C++17 compatible compiler (MSVC 2019+/Clang/GCC)

CMake 3.20+

OpenCV (videoio, imgproc, highgui, core)

ZBar (barcode/QR decoder)

⚙️ Build (Windows, vcpkg)

Aşağıdaki komutlar Developer PowerShell for VS 2022’de önerilir.

vcpkg ile bağımlılıkları yükle

# vcpkg klasöründeyken:
.\vcpkg.exe install opencv:x64-windows zbar:x64-windows


CMake konfigürasyon ve derleme

cmake -B build -S . ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows

cmake --build build --config Release


Çalıştırma

.\build\Release\qr_barcode_scanner.exe


📌 Eğer zbar portunu bulamıyorsan:

vcpkg update && vcpkg upgrade --no-dry-run deneyin,

veya .\vcpkg.exe search zbar ile port adını doğrulayın,

ya da ZBar’ı manuel kurup CMake’e ekleyin (aşağıdaki “Troubleshooting” bölümüne bakın).

⚙️ Build (Linux/macOS, system packages)

Ubuntu/Debian (örnek):

sudo apt update
sudo apt install -y build-essential cmake pkg-config \
                    libopencv-dev libzbar-dev
cmake -B build -S .
cmake --build build --config Release
./build/qr_barcode_scanner


macOS (Homebrew):

brew install cmake opencv zbar
cmake -B build -S .
cmake --build build --config Release
./build/qr_barcode_scanner

🕹️ Controls
Key	Action
q	Quit the program
s	Save snapshot to snapshots/
📝 Log format (decoded.txt)

Her başarılı decode sonrası bir satır yazılır:

YYYY-MM-DD HH:MM:SS, TYPE, DATA


Örnek:

2025-11-02 01:12:33, QR-CODE, https://example.com
2025-11-02 01:12:40, EAN-13, 9780306406157


Cooldown sistemi aynı verinin tekrar tekrar loglanmasını, kısa süre içinde, engeller.

🧩 CMake Note
