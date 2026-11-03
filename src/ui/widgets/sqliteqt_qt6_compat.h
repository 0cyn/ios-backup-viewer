#pragma once

#include <QtGlobal>
#include <QRandomGenerator>

inline int qrand()
{
	return static_cast<int>(QRandomGenerator::global()->bounded(RAND_MAX));
}
