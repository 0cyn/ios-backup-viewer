#ifndef IOSBACKUPS_BACKUPTREE_H
#define IOSBACKUPS_BACKUPTREE_H

// QAIM + QSFPM stink

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <QAbstractItemModel>
#include <QMap>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>

#include "Backup.h"

class BackupItemModel : public QAbstractItemModel
{
	Q_OBJECT

public:
	enum Column
	{
		ColName = 0,
		ColType,
		ColFileSize,
		NumColumns
	};

	enum Role
	{
		PathRole = Qt::UserRole + 1,
		FileIDRole,
		DomainRole,
		FlagsRole,
		IsDirectoryRole
	};

	explicit BackupItemModel(const libbackup::Backup& backup, QObject* parent = nullptr);

	~BackupItemModel() override;

	QModelIndex index(int row, int column, const QModelIndex& parent) const override;

	QModelIndex parent(const QModelIndex& child) const override;

	int rowCount(const QModelIndex& parent) const override;

	int columnCount(const QModelIndex& parent) const override;

	QVariant data(const QModelIndex& index, int role) const override;

	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

	QMap<int, QVariant> itemData(const QModelIndex& index) const override;

	QModelIndexList match(
		const QModelIndex& start, int role, const QVariant& value, int hits, Qt::MatchFlags flags) const override;

signals:
	void typeScanProgress(int done, int total);
	void typeScanFinished();

private:
	struct SnapshotNode
	{
		std::string name;
		std::optional<size_t> fileSize;
		const SnapshotNode* parent = nullptr;
		std::vector<const SnapshotNode*> children;
		const libbackup::BackupFileNode* node = nullptr;
	};

	void buildSnapshot(const libbackup::Backup& backup);

	const SnapshotNode* nodeOf(const QModelIndex& index) const;

	int rowOf(const SnapshotNode* sn) const;

	QModelIndex indexOf(const SnapshotNode* sn, int column) const;

	bool isDirectoryNode(const SnapshotNode* sn) const;

	std::string domainKey(const SnapshotNode* sn) const;

	std::string fullPath(const SnapshotNode* sn) const;

	static bool matchValue(const QVariant& dataValue, const QVariant& value, Qt::MatchFlags flags);

	struct ScanItem
	{
		std::string fileID;
		std::string path;
		const SnapshotNode* node;
	};

	using TypeMap = std::unordered_map<std::string, std::string>;

	void startTypeScan(const libbackup::Backup& backup);
	void typeScanWorker();
	void refreshTypeMap();

	std::vector<ScanItem> m_scanItems;
	std::atomic<size_t> m_nextScanItem = 0;
	std::atomic<size_t> m_completedScanItems = 0;
	std::atomic<bool> m_stopWorkers = false;
	std::vector<std::thread> m_workers;

	mutable std::mutex m_typesMutex;
	std::unique_ptr<TypeMap> m_displayedTypes;
	std::unique_ptr<TypeMap> m_pendingTypes;
	std::vector<const SnapshotNode*> m_pendingNotify;
	QTimer* m_refreshTimer = nullptr;

	std::deque<SnapshotNode> m_nodes;
	const SnapshotNode* m_root = nullptr;
};

class BackupSortFilterProxyModel : public QSortFilterProxyModel
{
	Q_OBJECT

public:
	explicit BackupSortFilterProxyModel(QObject* parent = nullptr);

protected:
	bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

	bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
	bool matchesFilter(const QModelIndex& sourceIndex) const;

	bool anyDescendantMatches(const QModelIndex& sourceIndex) const;
};

#endif  // IOSBACKUPS_BACKUPTREE_H
