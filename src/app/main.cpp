#include <QApplication>
#include <QCoreApplication>

#include "ui/main_window.h"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("OpenListSorter");
  QCoreApplication::setApplicationName("OpenListSorter");
  QCoreApplication::setApplicationVersion("0.1.0");

  MainWindow window;
  if (!window.ensureInitialConnection()) {
    return 0;
  }
  window.show();
  return app.exec();
}
