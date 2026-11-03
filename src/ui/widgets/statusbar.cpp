#include "statusbar.h"

StatusBar::StatusBar(QWidget* parent) : QStatusBar(parent)
{
	m_label = new QLabel(this);
	addWidget(m_label);
}

void StatusBar::setStatusText(const QString& text)
{
	m_label->setText(text);
}

void StatusBar::setLingerText(const QString& text, std::chrono::milliseconds duration)
{
	showMessage(text, static_cast<int>(duration.count()));
}
