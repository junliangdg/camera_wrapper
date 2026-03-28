#include "main_window.h"

#include <QApplication>
#include <opencv2/core.hpp>

// cv::Mat must be registered as a Qt metatype to be passed through
// QueuedConnection signals across threads.
Q_DECLARE_METATYPE(cv::Mat)

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // Register cv::Mat so Qt's signal/slot system can queue it.
    qRegisterMetaType<cv::Mat>("cv::Mat");

    MainWindow window;
    window.show();

    return app.exec();
}
