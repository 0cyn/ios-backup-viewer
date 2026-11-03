#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <memory>

#include <QMainWindow>
#include <QModelIndex>

#include "Backup.h"

class BackupItemModel;
class BackupSortFilterProxyModel;
class QLineEdit;
class QItemSelection;
class QSplitter;
class QStackedWidget;
class QTreeView;
class StatusBar;
class QHexView;
class QPushButton;
class SqliteTreeModel;
class PlistTreeModel;
class PlistSortFilterProxyModel;

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget* parent = nullptr);

private slots:
	void promptForBackup();
	void onTreeSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
	void onExport();
	void onPlistJump(const QModelIndex& index);
	void onPlistSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);

private:
	void refresh();
	void updateDetails();

	std::unique_ptr<libbackup::Backup> m_backup;
	QSplitter* m_splitter = nullptr;
	QSplitter* m_rightSplitter = nullptr;
	bool m_rightSplitterDefaulted = false;
	QLineEdit* m_filterEdit = nullptr;
	QTreeView* m_backupView = nullptr;
	QStackedWidget* m_detailsStack = nullptr;
	QHexView* m_hexView = nullptr;
	QHexView* m_dataHexView = nullptr;
	QTreeView* m_sqliteView = nullptr;
	SqliteTreeModel* m_sqliteModel = nullptr;
	QTreeView* m_plistView = nullptr;
	PlistTreeModel* m_plistModel = nullptr;
	PlistSortFilterProxyModel* m_plistProxy = nullptr;
	QPushButton* m_exportButton = nullptr;
	QLineEdit* m_searchEdit = nullptr;
	QString m_currentPath;
	QString m_currentName;
	QModelIndex m_currentSelection;
	StatusBar* m_statusBar = nullptr;
	BackupItemModel* m_model = nullptr;
	BackupSortFilterProxyModel* m_proxy = nullptr;
};

#endif  // MAINWINDOW_H
