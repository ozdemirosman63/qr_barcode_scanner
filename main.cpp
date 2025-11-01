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

// ---- LOG/SNAPSHOT konumunu proje köküne sabitle ----
#ifndef LOG_DIR
#define LOG_DIR "."
#endif
static const string LOG_FILE = (fs::path(LOG_DIR) / "decoded.txt").string();
static const string SNAP_DIR = (fs::path(LOG_DIR) / "snapshots").string();

struct RecentLogCache {
    int cooldown_frames;
    unordered_map<string, int> store;
    explicit RecentLogCache(int cooldown_frames_=120): cooldown_frames(cooldown_frames_) {}
    void tick() {
        vector<string> eraseKeys;
        for (auto &kv : store) {
            if (--kv.second <= 0) eraseKeys.push_back(kv.first);
        }
        for (auto &k : eraseKeys) store.erase(k);
    }
    bool should_log(const string& key) {
        if (store.find(key) != store.end()) return false;
        store[key] = cooldown_frames;
        return true;
    }
};

static void append_log(const string& code_type, const string& data) {
    ofstream f(LOG_FILE, ios::app);
    if (!f) return;
    // timestamp
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
    f << buf << ", " << code_type << ", " << data << "\n";
}

static vector<Point2f> order_quad_points(const vector<Point2f>& pts) {
    vector<Point2f> rect(4);
    Point2f tl, tr, br, bl;
    float s_min = FLT_MAX, s_max = -FLT_MAX, d_min = FLT_MAX, d_max = -FLT_MAX;
    for (auto &p : pts) {
        float s = p.x + p.y;
        float d = p.x - p.y;
        if (s < s_min){ s_min = s; tl = p; }
        if (s > s_max){ s_max = s; br = p; }
        if (d < d_min){ d_min = d; tr = p; }
        if (d > d_max){ d_max = d; bl = p; }
    }
    rect[0]=tl; rect[1]=tr; rect[2]=br; rect[3]=bl;
    return rect;
}

struct WarpResult {
    Mat warped;
    Mat M;        // src->dst
    Mat Minv;     // dst->src
    Size dstSize;
    bool ok=false;
};

static WarpResult four_point_warp(const Mat& image, const vector<Point2f>& quad) {
    WarpResult res;
    if (quad.size()!=4) return res;
    auto rect = order_quad_points(quad);
    Point2f tl=rect[0], tr=rect[1], br=rect[2], bl=rect[3];
    double widthA = norm(br - bl), widthB = norm(tr - tl);
    double heightA = norm(tr - br), heightB = norm(tl - bl);
    int maxWidth  = (int)round(max(widthA, widthB));
    int maxHeight = (int)round(max(heightA, heightB));
    if (maxWidth<=0 || maxHeight<=0) return res;

    vector<Point2f> dst = { {0.f,0.f}, {(float)maxWidth-1,0.f},
                            {(float)maxWidth-1,(float)maxHeight-1}, {0.f,(float)maxHeight-1} };
    res.M    = getPerspectiveTransform(rect, dst);
    res.Minv = getPerspectiveTransform(dst, rect);
    warpPerspective(image, res.warped, res.M, Size(maxWidth, maxHeight));
    res.dstSize = Size(maxWidth, maxHeight);
    res.ok = true;
    return res;
}

static bool find_paper_quad(const Mat& frame, vector<Point2f>& outQuad) {
    Mat gray, blur, edges;
    cvtColor(frame, gray, COLOR_BGR2GRAY);
    GaussianBlur(gray, blur, Size(5,5), 0);
    Canny(blur, edges, 50, 150);

    vector<vector<Point>> cnts;
    findContours(edges, cnts, RETR_LIST, CHAIN_APPROX_SIMPLE);

    double area_thresh = 0.15 * frame.cols * frame.rows;
    double best_area = 0.0;
    vector<Point2f> best;
    for (auto &c : cnts) {
        double peri = arcLength(c, true);
        vector<Point> approx;
        approxPolyDP(c, approx, 0.02 * peri, true);
        if (approx.size()==4 && isContourConvex(approx)) {
            double area = contourArea(approx);
            if (area > best_area && area > area_thresh) {
                best_area = area;
                best.clear();
                for (auto &p : approx) best.emplace_back((float)p.x, (float)p.y);
            }
        }
    }
    if (!best.empty()) { outQuad = best; return true; }
    return false;
}

static double heuristic_confidence(const Mat& roiBGRorGray) {
    if (roiBGRorGray.empty()) return 0.0;
    Mat gray;
    if (roiBGRorGray.channels()==3) cvtColor(roiBGRorGray, gray, COLOR_BGR2GRAY);
    else gray = roiBGRorGray;
    Mat lap; Laplacian(gray, lap, CV_64F);
    Scalar mu, sigma; meanStdDev(lap, mu, sigma);
    double var = sigma[0]*sigma[0];
    double score = min(var/1000.0, 1.0);
    if (score < 0.0) score = 0.0;
    return score;
}

static void draw_poly(Mat& frame, const vector<Point2f>& pts, const Scalar& color, int thickness=2) {
    if (pts.size()<2) return;
    for (size_t i=0;i<pts.size();++i) {
        line(frame, pts[i], pts[(i+1)%pts.size()], color, thickness);
    }
}

int main() {
    // snapshots klasörü (proje kökünde)
    std::error_code ec;
    fs::create_directories(SNAP_DIR, ec);

    VideoCapture cap(0);
    if (!cap.isOpened()) {
        cerr << "ERROR: Camera could not be opened." << endl;
        return 1;
    }

    QRCodeDetector qr;
    RecentLogCache cache(120); // aynı kodu 120 frame içinde tekrar yazma
    zbar::ImageScanner scanner;
    scanner.set_config(zbar::ZBAR_NONE, zbar::ZBAR_CFG_ENABLE, 1);

    Mat frame;
    while (true) {
        if (!cap.read(frame)) break;
        Mat preview = frame.clone();
        int H = frame.rows, W = frame.cols;

        // Kağıdı bul ve warp et
        vector<Point2f> quad;
        Mat decode_img = frame;
        WarpResult warp;
        if (find_paper_quad(frame, quad)) {
            warp = four_point_warp(frame, quad);
            draw_poly(preview, quad, Scalar(255,200,0), 3);
            putText(preview, "Paper detected - perspective corrected", Point(10,25),
                    FONT_HERSHEY_SIMPLEX, 0.7, Scalar(255,200,0), 2);
            if (warp.ok) decode_img = warp.warped;
        }

        // Bulunanları topla
        struct Item { string ctype; string data; double conf; };
        vector<Item> found;

        // --- QR (OpenCV) ---
        try {
            vector<string> decoded_info;
            vector<vector<Point2f>> points;
            bool ok = qr.detectAndDecodeMulti(decode_img, decoded_info, points);
            if (ok && !decoded_info.empty()) {
                for (size_t i=0;i<decoded_info.size();++i) {
                    const string& data = decoded_info[i];
                    if (data.empty()) continue;
                    vector<Point2f> pts = points[i];

                    Rect bbox;
                    Point p0;
                    if (warp.ok) {
                        vector<Point2f> pts_h;
                        perspectiveTransform(pts, pts_h, warp.Minv);
                        draw_poly(preview, pts_h, Scalar(0,255,0), 3);
                        bbox = boundingRect(pts_h);
                        p0 = pts_h.empty()? Point(10,40) : Point((int)pts_h[0].x,(int)pts_h[0].y);
                    } else {
                        draw_poly(preview, pts, Scalar(0,255,0), 3);
                        bbox = boundingRect(pts);
                        p0 = pts.empty()? Point(10,40) : Point((int)pts[0].x,(int)pts[0].y);
                    }

                    bbox &= Rect(0,0,W,H);
                    if (bbox.width <= 0 || bbox.height <= 0) continue; // güvenlik
                    Mat roi = preview(bbox);
                    double conf = heuristic_confidence(roi);
                    string shortData = data.size()>40 ? data.substr(0,40)+"..." : data;
                    string label = "QR: " + shortData + " (" + format("%.2f", conf) + ")";
                    putText(preview, label, p0, FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0,255,0), 2);
                    found.push_back({"QR", data, conf});
                }
            }
        } catch(...) { /* yut */ }

        // --- Barkodlar (ZBar) ---
        try {
            Mat gray;
            cvtColor(decode_img, gray, COLOR_BGR2GRAY);
            // data length = width*height; unsigned long'a güvenli cast
            zbar::Image image(gray.cols, gray.rows, "Y800", gray.data,
                              (unsigned long)(gray.cols * gray.rows));
            scanner.scan(image);

            for (auto it = image.symbol_begin(); it != image.symbol_end(); ++it) {
                string data = it->get_data();
                string code_type = it->get_type_name();

                vector<Point2f> pts;
                int n = it->get_location_size();
                for (int i=0;i<n;i++) {
                    pts.emplace_back((float)it->get_location_x(i), (float)it->get_location_y(i));
                }
                if (pts.size() >= 4) {
                    Rect bbox;
                    Point p0;
                    vector<Point2f> drawPts = pts;

                    if (warp.ok) {
                        perspectiveTransform(pts, drawPts, warp.Minv);
                    }
                    draw_poly(preview, drawPts, Scalar(0,128,255), 3);
                    bbox = boundingRect(drawPts);
                    bbox &= Rect(0,0,W,H);
                    if (bbox.width <= 0 || bbox.height <= 0) continue; // güvenlik
                    p0 = drawPts.empty()? Point(10,60) : Point((int)drawPts[0].x,(int)drawPts[0].y);

                    Mat roi = preview(bbox);
                    double conf = heuristic_confidence(roi);
                    string shortData = data.size()>40 ? data.substr(0,40)+"..." : data;
                    string label = code_type + ": " + shortData + " (" + format("%.2f", conf) + ")";
                    putText(preview, label, p0, FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0,128,255), 2);
                    found.push_back({code_type, data, conf});
                }
            }
        } catch(...) { /* yut */ }

        // Log kontrolü (cooldown)
        for (auto &it : found) {
            string key = it.ctype + "|" + it.data;
            if (cache.should_log(key)) append_log(it.ctype, it.data);
        }
        cache.tick();

        putText(preview, "q: quit  |  s: snapshot  |  logging -> decoded.txt",
                Point(10, H-10), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(240,240,240), 2);

        imshow("QR/Barcode Scanner (with perspective correction)", preview);
        int key = waitKey(1) & 0xFF;
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
            string path = (fs::path(SNAP_DIR) / (string("frame_") + ts + ".png")).string();
            imwrite(path, preview);
            cout << "Saved snapshot to " << path << endl;
        }
    }

    cap.release();
    destroyAllWindows();
    return 0;
}
