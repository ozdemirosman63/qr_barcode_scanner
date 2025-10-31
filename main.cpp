#include <opencv2/opencv.hpp>
#include <zbar.h>
#include <iostream>
#include <fstream>
#include <chrono>

using namespace cv;
using namespace std;

static const string LOG_FILE = "decoded.txt";

static void append_log(const string& type, const string& data) {
    ofstream f(LOG_FILE, ios::app);
    if (!f) return;

    auto now = chrono::system_clock::now();
    time_t t = chrono::system_clock::to_time_t(now);
    tm tm_now{};
#ifdef _WIN32
    localtime_s(&tm_now, &t);
#else
    localtime_r(&t, &tm_now);
#endif
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now);
    f << buf << ", " << type << ", " << data << "\n";
}

int main() {
    VideoCapture cap(0);
    if (!cap.isOpened()) {
        cerr << "ERROR: Camera could not be opened." << endl;
        return 1;
    }

    QRCodeDetector qr;
    zbar::ImageScanner scanner;
    scanner.set_config(zbar::ZBAR_NONE, zbar::ZBAR_CFG_ENABLE, 1);

    Mat frame;
    while (true) {
        if (!cap.read(frame)) break;
        Mat gray;
        cvtColor(frame, gray, COLOR_BGR2GRAY);

        // --- QR Code detection ---
        vector<Point> bbox;
        string data = qr.detectAndDecode(frame, bbox);
        if (!data.empty()) {
            polylines(frame, bbox, true, Scalar(0, 255, 0), 2);
            putText(frame, "QR: " + data, Point(20, 40), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 255, 0), 2);
            append_log("QR-Code", data);
        }
        // --- Barcode detection (ZBar) ---
        zbar::Image image(gray.cols, gray.rows, "Y800", gray.data, gray.cols * gray.rows);
        scanner.scan(image);
        for (auto it = image.symbol_begin(); it != image.symbol_end(); ++it) {
            string type = it->get_type_name();
            string text = it->get_data();
            cout << type << ": " << text << endl;

            append_log(type, text);
            putText(frame, type + ": " + text, Point(20, 80), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 128, 255), 2);
        }

        imshow("QR & Barcode Scanner", frame);
        int key = waitKey(1) & 0xFF;
        if (key == 'q') break;
    }

    cap.release();
    destroyAllWindows();
    return 0;
}