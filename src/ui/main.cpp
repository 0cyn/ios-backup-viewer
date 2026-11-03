#include <QApplication>
#include <QFontDatabase>

#include "mainwindow.h"

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);
	app.setOrganizationName("me.cynder");
	app.setApplicationName("iosbackups");

	QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	font.setPointSize(font.pointSize() + 2);
	app.setFont(font);

	MainWindow window;
	window.show();

	return app.exec();
}
