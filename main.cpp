#include <opencv2/opencv.hpp>
#include <iostream>

using namespace cv;
using namespace std;

int main() {
    VideoCapture cap(0);
    if (!cap.isOpened()) {
        cerr << "ERROR: Camera could not be opened." << endl;
        return 1;
    }

    QRCodeDetector qr;
    Mat frame;

    while (true) {
        if (!cap.read(frame)) break;

        vector<Point> bbox;
        string data = qr.detectAndDecode(frame, bbox);

        if (!data.empty()) {
            cout << "QR Code detected: " << data << endl;
            polylines(frame, bbox, true, Scalar(0, 255, 0), 2);
            putText(frame, data, Point(20, 40), FONT_HERSHEY_SIMPLEX, 0.8, Scalar(0, 255, 0), 2);
        }

        imshow("QR Scanner", frame);
        int key = waitKey(1) & 0xFF;
        if (key == 'q') break;
    }

    cap.release();
    destroyAllWindows();
    return 0;
}