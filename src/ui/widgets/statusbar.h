//
// Created by Skye on 11/2/26.
//

#ifndef IOSBACKUPS_STATUSBAR_H
#define IOSBACKUPS_STATUSBAR_H

#include <chrono>

#include <QLabel>
#include <QStatusBar>

class StatusBar : public QStatusBar
{
	Q_OBJECT

public:
	explicit StatusBar(QWidget* parent = nullptr);

	void setStatusText(const QString& text);

	void setLingerText(const QString& text, std::chrono::milliseconds duration);

private:
	QLabel* m_label = nullptr;
};

#endif  // IOSBACKUPS_STATUSBAR_H
