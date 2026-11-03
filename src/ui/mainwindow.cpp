#include "mainwindow.h"
#include "widgets/backuptree.h"
#include "widgets/plisttree.h"
#include "widgets/sqlitetree.h"
#include "widgets/statusbar.h"

#include <chrono>

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHexView/qhexview.h>
#include <QHexView/model/buffer/qmemorybuffer.h>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QTreeView>
#include <QTimer>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
	setWindowTitle("Backup Browser");

	m_splitter = new QSplitter(Qt::Horizontal, this);
	setCentralWidget(m_splitter);

	QFrame* leftFrame = new QFrame(m_splitter);
	QVBoxLayout* layout = new QVBoxLayout(leftFrame);

	m_filterEdit = new QLineEdit(leftFrame);
	m_filterEdit->setPlaceholderText("Filter");
	m_filterEdit->setClearButtonEnabled(true);
	layout->addWidget(m_filterEdit);

	m_backupView = new QTreeView(leftFrame);
	m_backupView->setSortingEnabled(true);
	m_backupView->setUniformRowHeights(true);
	m_backupView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_backupView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	layout->addWidget(m_backupView);

	m_splitter->addWidget(leftFrame);

	QWidget* rightPane = new QWidget(m_splitter);
	QVBoxLayout* rightLayout = new QVBoxLayout(rightPane);

	QHBoxLayout* toolbar = new QHBoxLayout();
	m_searchEdit = new QLineEdit(rightPane);
	m_searchEdit->setPlaceholderText("Search");
	m_searchEdit->setClearButtonEnabled(true);
	toolbar->addWidget(m_searchEdit, 1);
	m_exportButton = new QPushButton("Export", rightPane);
	m_exportButton->setEnabled(false);
	toolbar->addWidget(m_exportButton);
	rightLayout->addLayout(toolbar);

	m_detailsStack = new QStackedWidget(rightPane);
	m_detailsStack->addWidget(new QWidget(m_detailsStack));
	m_hexView = new QHexView(m_detailsStack);
	m_hexView->setFont(font());
	m_detailsStack->addWidget(m_hexView);
	m_sqliteModel = new SqliteTreeModel(this);
	m_sqliteView = new QTreeView(m_detailsStack);
	m_sqliteView->setModel(m_sqliteModel);
	m_sqliteView->setUniformRowHeights(true);
	m_sqliteView->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	m_detailsStack->addWidget(m_sqliteView);
	m_plistModel = new PlistTreeModel(this);
	m_plistProxy = new PlistSortFilterProxyModel(this);
	m_plistProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
	m_plistProxy->setSourceModel(m_plistModel);
	m_plistView = new QTreeView(m_detailsStack);
	m_plistView->setModel(m_plistProxy);
	m_plistView->setUniformRowHeights(true);
	m_plistView->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	PlistJumpDelegate* jumpDelegate = new PlistJumpDelegate(m_plistView);
	m_plistView->setItemDelegate(jumpDelegate);
	connect(jumpDelegate, &PlistJumpDelegate::jumpRequested, this, &MainWindow::onPlistJump);
	connect(m_plistView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
		&MainWindow::onPlistSelectionChanged);
	m_detailsStack->addWidget(m_plistView);
	m_rightSplitter = new QSplitter(Qt::Vertical, rightPane);
	rightLayout->addWidget(m_rightSplitter);
	m_rightSplitter->addWidget(m_detailsStack);

	m_dataHexView = new QHexView(m_rightSplitter);
	m_dataHexView->setFont(font());
	m_dataHexView->hide();
	m_rightSplitter->addWidget(m_dataHexView);

	QSettings settings;
	const QByteArray splitterState = settings.value(QLatin1String("rightSplitterState")).toByteArray();
	if (!splitterState.isEmpty())
	{
		m_rightSplitter->restoreState(splitterState);
		m_rightSplitterDefaulted = true;
	}
	connect(m_rightSplitter, &QSplitter::splitterMoved, this, [this](int, int) {
		QSettings().setValue(QLatin1String("rightSplitterState"), m_rightSplitter->saveState());
	});

	m_splitter->addWidget(rightPane);

	m_splitter->setStretchFactor(0, 1);
	m_splitter->setStretchFactor(1, 1);
	m_splitter->setCollapsible(0, false);
	m_splitter->setCollapsible(1, false);

	if (QScreen* screen = QGuiApplication::primaryScreen())
	{
		const QRect available = screen->availableGeometry();
		resize(static_cast<int>(available.width() * 0.75), static_cast<int>(available.height() * 0.75));
	}

	m_statusBar = new StatusBar(this);
	setStatusBar(m_statusBar);

	m_proxy = new BackupSortFilterProxyModel(this);
	m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
	m_backupView->setModel(m_proxy);

	connect(m_backupView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
		&MainWindow::onTreeSelectionChanged);

	connect(m_proxy, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex& top,
						 const QModelIndex& bottom, const QVector<int>&) {
		if (m_currentSelection.isValid() && bottom.row() == m_currentSelection.row() &&
			bottom.parent() == m_currentSelection.parent())
		{
			updateDetails();
		}
	});

	connect(m_exportButton, &QPushButton::clicked, this, &MainWindow::onExport);

	connect(m_filterEdit, &QLineEdit::textChanged, m_proxy, &QSortFilterProxyModel::setFilterFixedString);

	connect(m_searchEdit, &QLineEdit::textChanged, m_plistProxy, &QSortFilterProxyModel::setFilterFixedString);

	QTimer::singleShot(0, this, &MainWindow::promptForBackup);
}

void MainWindow::promptForBackup()
{
	const QString startDir = QSettings().value(QLatin1String("backupPath")).toString();

	activateWindow();
	raise();
	const QString backupPath = QFileDialog::getExistingDirectory(this, "Select backup folder", startDir);
	if (backupPath.isEmpty())
	{
		exit(0); // FIXME actual flow
	}

	QSettings().setValue(QLatin1String("backupPath"), backupPath);

	try
	{
		m_backup = std::make_unique<libbackup::Backup>(backupPath.toStdString());
	}
	catch (const libbackup::BackupException& e)
	{
		QMessageBox::critical(this, "Backup Browser", QString("Couldn't open backup:\n%1").arg(e.what()));
	}
	catch (const std::exception& e)
	{
		QMessageBox::critical(this, "Backup Browser", QString("Couldn't open backup:\n%1").arg(e.what()));
	}

	if (!m_backup)
		exit(0);

	refresh();
}

void MainWindow::refresh()
{
	m_proxy->setSourceModel(nullptr);
	delete m_model;
	m_model = nullptr;

	if (!m_backup)
		return;

	m_model = new BackupItemModel(*m_backup, m_proxy);
	connect(m_model, &BackupItemModel::typeScanProgress, this, [this](int done, int total) {
		m_statusBar->setStatusText(QString("Processing file types... %1/%2").arg(done).arg(total));
	});
	connect(m_model, &BackupItemModel::typeScanFinished, this, [this] {
		m_statusBar->setStatusText(QString());
		m_statusBar->setLingerText("processed file types.", std::chrono::seconds(2));
	});
	m_proxy->setSourceModel(m_model);
	m_backupView->expandToDepth(0);
	m_backupView->resizeColumnToContents(0);
}

void MainWindow::onTreeSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
{
	Q_UNUSED(deselected);

	m_currentSelection = selected.indexes().isEmpty() ? QModelIndex() : selected.indexes().first();
	updateDetails();
}

void MainWindow::updateDetails()
{
	QString path;
	QString name;
	QString type;
	if (m_backup && m_currentSelection.isValid() && !m_currentSelection.data(BackupItemModel::IsDirectoryRole).toBool())
	{
		const QString fileID = m_currentSelection.data(BackupItemModel::FileIDRole).toString();
		if (!fileID.isEmpty())
		{
			path = QString::fromStdString(m_backup->FilePathForFileID(fileID.toStdString()).string());
			name = m_currentSelection.siblingAtColumn(BackupItemModel::ColName).data(Qt::DisplayRole).toString();
			type = m_currentSelection.siblingAtColumn(BackupItemModel::ColType).data(Qt::DisplayRole).toString();
		}
	}

	const bool remap = path != m_currentPath;
	m_currentPath = path;
	m_currentName = name;
	m_exportButton->setEnabled(!m_currentPath.isEmpty());
	m_dataHexView->hide();

	if (m_currentPath.isEmpty())
	{
		m_detailsStack->setCurrentIndex(0);
		return;
	}

	if (type.compare(QLatin1String("application/vnd.sqlite3"), Qt::CaseInsensitive) == 0 &&
		m_sqliteModel->load(m_currentPath))
	{
		m_detailsStack->setCurrentWidget(m_sqliteView);
		return;
	}

	if ((type.compare(QLatin1String("application/x-bplist"), Qt::CaseInsensitive) == 0 ||
		 type.compare(QLatin1String("text/xml"), Qt::CaseInsensitive) == 0) &&
		m_plistModel->load(m_currentPath))
	{
		m_detailsStack->setCurrentWidget(m_plistView);
		return;
	}

	if (remap)
	{
		QHexDocument* doc = QHexDocument::fromMappedFile(m_currentPath, m_hexView);
		if (!doc)
		{
			m_currentPath.clear();
			m_currentName.clear();
			m_exportButton->setEnabled(false);
			m_detailsStack->setCurrentIndex(0);
			return;
		}

		QHexDocument* previous = m_hexView->hexDocument();
		m_hexView->setDocument(doc);
		delete previous;
	}

	m_detailsStack->setCurrentWidget(m_hexView);
}

void MainWindow::onExport()
{
	if (m_currentPath.isEmpty())
		return;

	activateWindow();
	raise();

	const QString suggestion = m_currentName.isEmpty() ? QFileInfo(m_currentPath).fileName() : m_currentName;
	const QString destination = QFileDialog::getSaveFileName(this, "Export", suggestion);
	if (destination.isEmpty())
		return;

	QFile input(m_currentPath);
	if (!input.open(QIODevice::ReadOnly))
	{
		QMessageBox::warning(this, "Export", QString("Could not read %1").arg(m_currentPath));
		return;
	}

	QFile output(destination);
	if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		QMessageBox::warning(this, "Export", QString("Could not write %1").arg(destination));
		return;
	}

	char buffer[65536];
	qint64 n;
	while ((n = input.read(buffer, sizeof(buffer))) > 0)
	{
		if (output.write(buffer, n) != n)
		{
			QMessageBox::warning(this, "Export", QString("Could not write %1").arg(destination));
			return;
		}
	}
}

void MainWindow::onPlistJump(const QModelIndex& index)
{
	const QModelIndex target = m_plistProxy->mapFromSource(m_plistModel->jumpTarget(m_plistProxy->mapToSource(index)));
	if (!target.isValid())
		return;

	m_plistView->expand(target.parent());
	m_plistView->selectionModel()->select(target, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
	m_plistView->scrollTo(target, QAbstractItemView::PositionAtCenter);
}

void MainWindow::onPlistSelectionChanged(const QItemSelection& selected, const QItemSelection&)
{
	QByteArray bytes;
	for (const QModelIndex& idx : selected.indexes())
	{
		if (idx.column() == PlistTreeModel::ColValue)
		{
			bytes = m_plistModel->dataBytes(m_plistProxy->mapToSource(idx));
			break;
		}
	}

	if (bytes.isEmpty())
	{
		m_dataHexView->hide();
		return;
	}

	QHexDocument* previous = m_dataHexView->hexDocument();
	m_dataHexView->setDocument(QHexDocument::fromMemory<QMemoryBuffer>(bytes, m_dataHexView));
	delete previous;
	m_dataHexView->show();
	if (!m_rightSplitterDefaulted)
	{
		m_rightSplitterDefaulted = true;
		const int height = QFontMetrics(m_dataHexView->font()).height() * 6 + m_dataHexView->frameWidth() * 2 +
			m_rightSplitter->handleWidth();
		const int rest = m_rightSplitter->height() - height;
		if (rest > 0)
			m_rightSplitter->setSizes({rest, height});
	}
}

