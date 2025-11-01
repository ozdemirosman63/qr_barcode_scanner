#include <opencv2/opencv.hpp>
#include <zbar.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <chrono>

using namespace cv;
using namespace std;
namespace fs = std::filesystem;

static const string LOG_FILE = "decoded.txt";

// ---------- Yeni: Tekrar logu engellemek için cooldown cache ----------
struct RecentLogCache {
    int cooldown_frames;
    unordered_map<string, int> store;
    explicit RecentLogCache(int cooldown_frames_ = 120) : cooldown_frames(cooldown_frames_) {}
    void tick() {
        vector<string> eraseKeys;
        eraseKeys.reserve(store.size());
        for (auto& kv : store) {
            if (--kv.second <= 0) eraseKeys.push_back(kv.first);
        }
        for (auto& k : eraseKeys) store.erase(k);
    }
    bool should_log(const string& key) {
        if (store.find(key) != store.end()) return false;
        store[key] = cooldown_frames;
        return true;
    }
};

// ---------- Log yazıcı ----------
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

// ---------- Paper (document) detection helpers ----------
static bool find_paper_quad(const Mat& frame, vector<Point2f>& outQuad) {
    Mat gray, blurImg, edges;
    cvtColor(frame, gray, COLOR_BGR2GRAY);
    GaussianBlur(gray, blurImg, Size(5, 5), 0);
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
    // TL, TR, BR, BL
    vector<Point2f> rect(4);
    Point2f tl, tr, br, bl;
    float smin = FLT_MAX, smax = -FLT_MAX, dmin = FLT_MAX, dmax = -FLT_MAX;
    for (auto& p : pts) {
        float s = p.x + p.y;
        float d = p.x - p.y;
        if (s < smin) { smin = s; tl = p; }
        if (s > smax) { smax = s; br = p; }
        if (d < dmin) { dmin = d; tr = p; }
        if (d > dmax) { dmax = d; bl = p; }
    }
    rect[0] = tl; rect[1] = tr; rect[2] = br; rect[3] = bl;
    return rect;
}

static Mat four_point_warp(const Mat& image, const vector<Point2f>& quad) {
    if (quad.size() != 4) return image;

    auto rect = order_quad_points(quad);
    Point2f tl = rect[0], tr = rect[1], br = rect[2], bl = rect[3];

    double widthA = norm(br - bl), widthB = norm(tr - tl);
    double heightA = norm(tr - br), heightB = norm(tl - bl);
    int maxWidth = (int)round(max(widthA, widthB));
    int maxHeight = (int)round(max(heightA, heightB));
    if (maxWidth <= 0 || maxHeight <= 0) return image;

    vector<Point2f> dst = {
        {0.f,0.f},
        {(float)maxWidth - 1, 0.f},
        {(float)maxWidth - 1, (float)maxHeight - 1},
        {0.f, (float)maxHeight - 1}
    };

    Mat M = getPerspectiveTransform(rect, dst);
    Mat warped;
    warpPerspective(image, warped, M, Size(maxWidth, maxHeight));
    return warped;
}

// ---------- Yeni: netlik heuristiği (etikette skor göstermek için) ----------
static double heuristic_confidence(const Mat& roiBGRorGray) {
    if (roiBGRorGray.empty()) return 0.0;
    Mat gray;
    if (roiBGRorGray.channels() == 3) cvtColor(roiBGRorGray, gray, COLOR_BGR2GRAY);
    else gray = roiBGRorGray;
    Mat lap; Laplacian(gray, lap, CV_64F);
    Scalar mu, sigma; meanStdDev(lap, mu, sigma);
    double var = sigma[0] * sigma[0];
    double score = min(var / 1000.0, 1.0);   // 0..1 arası sıkıştır
    if (score < 0.0) score = 0.0;
    return score;
}

// ---------- Yeni: çokgen çizim yardımcı ----------
static void draw_poly(Mat& frame, const vector<Point2f>& pts, const Scalar& color, int thickness = 2) {
    if (pts.size() < 2) return;
    for (size_t i = 0; i < pts.size(); ++i) {
        line(frame, pts[i], pts[(i + 1) % pts.size()], color, thickness);
    }
}

int main() {
    // snapshots klasörünü garanti et
    fs::create_directories("snapshots");

    VideoCapture cap(0);
    if (!cap.isOpened()) {
        cerr << "ERROR: Camera could not be opened." << endl;
        return 1;
    }

    QRCodeDetector qr;
    zbar::ImageScanner scanner;
    scanner.set_config(zbar::ZBAR_NONE, zbar::ZBAR_CFG_ENABLE, 1);

    // ---------- Yeni: log tekrarını engellemek için cache ----------
    RecentLogCache cache(120); // ~120 frame (yaklaşık 4 sn @30fps)

    Mat frame;
    while (true) {
        if (!cap.read(frame)) break;

        // 1) Paper detection + warp
        vector<Point2f> quad;
        Mat processed = frame; // varsayılan: orijinal
        if (find_paper_quad(frame, quad)) {
            processed = four_point_warp(frame, quad);
            // orijinal kadraja çizmek istersen:
            for (auto& p : quad) circle(frame, p, 3, Scalar(255, 200, 0), FILLED);
            putText(processed, "Paper detected - perspective corrected",
                Point(20, 30), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(255, 200, 0), 2);
        }

        // 2) QR + Barkod bulgularını ortak topla (etiketleme ve cooldown log için)
        struct Item { string kind; string data; Rect box; };
        vector<Item> found;

        // 2a) QR detect
        vector<Point> bbox;
        string data = qr.detectAndDecode(processed, bbox);
        if (!data.empty()) {
            // bbox’u dikdörtgene çevir
            Rect r(0, 0, 0, 0);
            if (bbox.size() >= 4) r = boundingRect(bbox);
            if (r.area() == 0) r = Rect(10, 40, min(200, processed.cols - 10), 30);
            found.push_back({ "QR", data, r });
        }

        // 2b) Barkod (ZBar)
        Mat gray;
        cvtColor(processed, gray, COLOR_BGR2GRAY);
        zbar::Image image(gray.cols, gray.rows, "Y800", gray.data, (unsigned long)(gray.cols * gray.rows));
        scanner.scan(image);
        for (auto it = image.symbol_begin(); it != image.symbol_end(); ++it) {
            string type = it->get_type_name();
            string text = it->get_data();

            // zbar konumunu kutuya çevir
            vector<Point2f> pts;
            int n = it->get_location_size();
            for (int i = 0; i < n; ++i) pts.emplace_back((float)it->get_location_x(i), (float)it->get_location_y(i));
            Rect r = pts.size() >= 4 ? boundingRect(pts) : Rect(10, 80, min(220, processed.cols - 10), 30);

            found.push_back({ type, text, r });
        }

        // 3) Ekrana çiz + netlik skoru + cooldown log
        for (auto& it : found) {
            Rect roiRect = it.box & Rect(0, 0, processed.cols, processed.rows);
            if (roiRect.area() <= 0) continue;

            // netlik skoru
            double conf = heuristic_confidence(processed(roiRect));
            string shortData = it.data.size() > 40 ? it.data.substr(0, 40) + "..." : it.data;
            string label = it.kind + ": " + shortData + " (" + format("%.2f", conf) + ")";

            // kutu/çokgen
            rectangle(processed, roiRect, it.kind == "QR" ? Scalar(0, 255, 0) : Scalar(0, 128, 255), 2);
            putText(processed, label, Point(roiRect.x, max(20, roiRect.y - 8)),
                FONT_HERSHEY_SIMPLEX, 0.6, it.kind == "QR" ? Scalar(0, 255, 0) : Scalar(0, 128, 255), 2);

            // cooldown ile log
            string key = it.kind + "|" + it.data;
            if (cache.should_log(key)) append_log(it.kind, it.data);
        }
        cache.tick();

        // 4) Alt bilgi overlay
        putText(processed, "q: quit  |  s: snapshot  |  logging -> decoded.txt",
            Point(10, processed.rows - 10),
            FONT_HERSHEY_SIMPLEX, 0.6, Scalar(240, 240, 240), 2);

        // Göster
        imshow("QR & Barcode Scanner (with perspective correction)", processed);
        int key = waitKey(1) & 0xFF;

        // 5) Tuşlar: q = çıkış, s = snapshot
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