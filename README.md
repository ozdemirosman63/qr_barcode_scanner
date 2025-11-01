# 📷 QR & Barcode Scanner from Paper (C++ / OpenCV + ZBar)

A real-time QR and Barcode scanner written in **C++**, using **OpenCV** for image processing and **ZBar** for barcode decoding.  
It detects QR codes or barcodes printed on paper, applies **bird’s-eye view correction**, and logs decoded results into a `decoded.txt` file automatically.

---

## Features

✅ Real-time camera input using **OpenCV**  
✅ Detects and decodes:
- **QR Codes**
- **EAN-13** (Product Barcodes)
- **ISBN-13** (Book Barcodes)
✅ **Perspective correction** (for tilted papers)  
✅ **Automatic logging** to `decoded.txt` with timestamps  
✅ Snapshot saving with key `S`  
✅ Cooldown system to prevent duplicate logs  

---

## ⚙️ Build Instructions (Windows)

### 1️⃣ Install dependencies via vcpkg
```bash
.\vcpkg.exe install opencv zbar:x64-windows

    2️⃣ Configure and build
    cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
     cmake --build build --config Release
    3️⃣ Run
    .\build\Release\qr_barcode_scanner.exe

    🎮 Controls
    Key	Action
    q	Quit the program
    s	Save snapshot (saved to snapshots/)
