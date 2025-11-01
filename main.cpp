#include <opencv2/opencv.hpp>
#include <zbar.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>

using namespace cv;
using namespace std;
namespace fs = std::filesystem;

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

// ---- Paper (document) detection helpers ----
static bool find_paper_quad(const Mat& frame, vector<Point2f>& outQuad) {
    Mat gray, blurImg, edges;
    cvtColor(frame, gray, COLOR_BGR2GRAY);
    GaussianBlur(gray, blurImg, Size(5,5), 0);
    Canny(blurImg, edges, 50, 150);

    vector<vector<Point>> contours;
    findContours(edges, contours, RETR_LIST, CHAIN_APPROX_SIMPLE);

    double bestArea = 0;
    vector<Point2f> bestQuad;

    for (auto& c : contours) {
        double peri = arcLength(c, true);
        vector<Point> approx;
        approxPolyDP(c, approx, 0.02 * peri, true);

        if (approx.size() == 4 && isContourConvex(approx)) {
            double area = contourArea(approx);
            if (area > bestArea) {
                bestArea = area;
                bestQuad.clear();
                for (auto& p : approx) bestQuad.emplace_back((float)p.x, (float)p.y);
            }
        }
    }

    if (!bestQuad.empty()) { outQuad = bestQuad; return true; }
    return false;
}

static vector<Point2f> order_quad_points(const vector<Point2f>& pts) {


    // TL, TR, BR, BL sıralaması için klasik yöntem

    vector<Point2f> rect(4);
    Point2f tl, tr, br, bl;
    float smin=FLT_MAX, smax=-FLT_MAX, dmin=FLT_MAX, dmax=-FLT_MAX;
    for (auto& p : pts) {
        float s = p.x + p.y;
        float d = p.x - p.y;
        if (s < smin) { smin = s; tl = p; }
        if (s > smax) { smax = s; br = p; }
        if (d < dmin) { dmin = d; tr = p; }
        if (d > dmax) { dmax = d; bl = p; }
    }
    rect[0]=tl; rect[1]=tr; rect[2]=br; rect[3]=bl;
    return rect;
}

static Mat four_point_warp(const Mat& image, const vector<Point2f>& quad) {
    if (quad.size() != 4) return image;

    auto rect = order_quad_points(quad);
    Point2f tl=rect[0], tr=rect[1], br=rect[2], bl=rect[3];

    double widthA = norm(br - bl), widthB = norm(tr - tl);
    double heightA = norm(tr - br), heightB = norm(tl - bl);
    int maxWidth  = (int)round(max(widthA, widthB));
    int maxHeight = (int)round(max(heightA, heightB));
    if (maxWidth <= 0 || maxHeight <= 0) return image;

    vector<Point2f> dst = {
        {0.f,0.f},
        {(float)maxWidth-1, 0.f},
        {(float)maxWidth-1, (float)maxHeight-1},
        {0.f, (float)maxHeight-1}
    };

    Mat M = getPerspectiveTransform(rect, dst);
    Mat warped;
    warpPerspective(image, warped, M, Size(maxWidth, maxHeight));
    return warped;
}
// ---- /helpers ----

int main() {
    // snapshots klas�r�n� garanti et
    fs::create_directories("snapshots");

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

        // 1) Paper detection + warp
        vector<Point2f> quad;

        Mat processed = frame; // varsayılan: orijinal

        if (find_paper_quad(frame, quad)) {
            processed = four_point_warp(frame, quad);
            putText(processed, "Paper detected - perspective corrected",
                    Point(20,30), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(255,200,0), 2);
        }

        // 2) QR detection on processed
        vector<Point> bbox;
        string data = qr.detectAndDecode(processed, bbox);
        if (!data.empty()) {
            if (bbox.size() >= 4) polylines(processed, bbox, true, Scalar(0,255,0), 2);
            putText(processed, "QR: " + data, Point(20,60),
                    FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0,255,0), 2);
            append_log("QR-Code", data);
        }

        // 3) Barcode (ZBar) on processed gray
        Mat gray;
        cvtColor(processed, gray, COLOR_BGR2GRAY);
        zbar::Image image(gray.cols, gray.rows, "Y800", gray.data, (unsigned long)(gray.cols * gray.rows));
        scanner.scan(image);
        int y = 100;
        for (auto it = image.symbol_begin(); it != image.symbol_end(); ++it) {
            string type = it->get_type_name();
            string text = it->get_data();
            append_log(type, text);
            putText(processed, type + ": " + text, Point(20,y),
                    FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0,128,255), 2);
            y += 30;
        }


        // 4) Alt bilgi overlay
        putText(processed, "q: quit  |  s: snapshot  |  logging -> decoded.txt",
                Point(10, processed.rows - 10),
                FONT_HERSHEY_SIMPLEX, 0.6, Scalar(240, 240, 240), 2);

        // G�ster
        imshow("QR & Barcode Scanner (with perspective correction)", processed);

        int key = waitKey(1) & 0xFF;

        // 5) Tu�lar: q = ��k��, s = snapshot
        if (key == 'q') break;
        else if (key == 's') {
            auto now = chrono::system_clock::now();
            time_t t = chrono::system_clock::to_time_t(now);
            tm tm_now{};
#ifdef _WIN32
            localtime_s(&tm_now, &t);
#else
            localtime_r(&t, &tm_now);
#endif
            char ts[32];
            strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", &tm_now);
            string path = "snapshots/frame_" + string(ts) + ".png";
            imwrite(path, processed);
            cout << "Saved snapshot to " << path << endl;
        }
    }

    cap.release();
    destroyAllWindows();
    return 0;
}
