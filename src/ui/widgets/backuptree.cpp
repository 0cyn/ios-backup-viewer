#include "backuptree.h"

#include <algorithm>
#include <deque>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include <unistd.h>

#include <QRegularExpression>
#include <QString>

#include "Magic.hpp"

BackupItemModel::BackupItemModel(const libbackup::Backup& backup, QObject* parent) : QAbstractItemModel(parent)
{
	buildSnapshot(backup);
	startTypeScan(backup);
}

BackupItemModel::~BackupItemModel()
{
	if (m_refreshTimer)
		m_refreshTimer->stop();
	m_stopWorkers.store(true);
	for (std::thread& worker : m_workers)
		worker.join();
}

QModelIndex BackupItemModel::index(int row, int column, const QModelIndex& parent) const
{
	if (column < 0 || column >= NumColumns)
		return {};

	const SnapshotNode* parentNode = parent.isValid() ? nodeOf(parent) : m_root;
	if (!parentNode || row < 0 || std::size_t(row) >= parentNode->children.size())
		return {};

	return createIndex(row, column, const_cast<SnapshotNode*>(parentNode->children[std::size_t(row)]));
}

QModelIndex BackupItemModel::parent(const QModelIndex& child) const
{
	if (!child.isValid())
		return {};

	const SnapshotNode* sn = nodeOf(child);
	if (!sn || !sn->parent || sn->parent == m_root)
		return {}; // domain

	return indexOf(sn->parent, child.column());
}

int BackupItemModel::rowCount(const QModelIndex& parent) const
{
	const SnapshotNode* sn = parent.isValid() ? nodeOf(parent) : m_root;
	return sn ? int(sn->children.size()) : 0;
}

int BackupItemModel::columnCount(const QModelIndex& parent) const
{
	Q_UNUSED(parent);
	return NumColumns;
}

QVariant BackupItemModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid())
		return {};

	const SnapshotNode* sn = nodeOf(index);
	const libbackup::ManifestDatabase::FilesRow* row = sn->node ? sn->node->associatedRow : nullptr;

	switch (role)
	{
	case Qt::DisplayRole:
	case Qt::EditRole:
		switch (index.column())
		{
		case ColName:
			return QString::fromStdString(sn->name);
		case ColType:
		{
			if (!row)
				return QVariant();
			std::lock_guard lock(m_typesMutex);
			const auto it = m_displayedTypes->find(row->fileID);
			if (it != m_displayedTypes->end())
				return QString::fromStdString(it->second);
			return QVariant();
		}
		case ColFileSize:
			return sn->fileSize.has_value() ? QVariant(static_cast<qlonglong>(*sn->fileSize)) : QVariant("");
		}
		return {};
	case Qt::ToolTipRole:
	case PathRole:
		return QString::fromStdString(fullPath(sn));
	case FileIDRole:
		return row ? QString::fromStdString(row->fileID) : QVariant();
	case DomainRole:
		return QString::fromStdString(domainKey(sn));
	case FlagsRole:
		return row ? QVariant(qlonglong(row->flags)) : QVariant();
	case IsDirectoryRole:
		return QVariant(isDirectoryNode(sn));
	}

	return {};
}

QVariant BackupItemModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return {};

	switch (section)
	{
	case ColName:
		return QStringLiteral("Name");
	case ColType:
		return QStringLiteral("Type");
	case ColFileSize:
		return QStringLiteral("File Size");
	}
	return {};
}

QMap<int, QVariant> BackupItemModel::itemData(const QModelIndex& index) const
{
	QMap<int, QVariant> out;
	if (!index.isValid())
		return out;

	for (int role : {int(Qt::DisplayRole), int(Qt::EditRole), int(Qt::ToolTipRole), int(PathRole), int(FileIDRole),
			 int(DomainRole), int(FlagsRole), int(IsDirectoryRole)})
	{
		QVariant value = data(index, role);
		if (value.isValid())
			out.insert(role, value);
	}
	return out;
}

QModelIndexList BackupItemModel::match(
	const QModelIndex& start, int role, const QVariant& value, int hits, Qt::MatchFlags flags) const
{
	QModelIndexList results;
	const int limit = hits > 0 ? hits : std::numeric_limits<int>::max();

	std::vector<const SnapshotNode*> stack;
	if (start.isValid())
	{
		const SnapshotNode* sn = nodeOf(start);
		if (flags.testFlag(Qt::MatchRecursive))
			stack.push_back(sn);
		else
			stack = sn->parent->children;
	}
	else
	{
		stack = m_root->children;
	}

	while (!stack.empty() && results.size() < size_t(limit))
	{
		const SnapshotNode* sn = stack.back();
		stack.pop_back();

		for (int column = 0; column < NumColumns && results.size() < size_t(limit); column++)
		{
			if (matchValue(data(indexOf(sn, column), role), value, flags))
				results.append(indexOf(sn, column));
		}

		if (flags.testFlag(Qt::MatchRecursive))
			stack.insert(stack.end(), sn->children.begin(), sn->children.end());
	}
	return results;
}

void BackupItemModel::buildSnapshot(const libbackup::Backup& backup)
{
	struct Work
	{
		const SnapshotNode* parent;
		std::string name;
		const libbackup::BackupFileNode* node;
	};

	m_nodes.emplace_back();
	m_root = &m_nodes.front();

	std::deque<Work> queue;
	for (const auto& [name, node] : backup.domains())
		queue.push_back({m_root, name, node.get()});
	std::ranges::sort(queue, {}, [](const Work& w) -> const std::string& { return w.name; });

	while (!queue.empty())
	{
		const Work work = queue.front();
		queue.pop_front();

		m_nodes.emplace_back();
		SnapshotNode* sn = &m_nodes.back();
		sn->name = work.name;

		sn->parent = work.parent;
		sn->node = work.node;

		if (sn->node->associatedRow && !sn->node->isDirectory)
		{
			const auto& filepath = backup.FilePathForFileID(work.node->associatedRow->fileID);
			if (!filepath.empty() && std::filesystem::exists(filepath))
				sn->fileSize = std::filesystem::file_size(filepath);
		}
		const_cast<SnapshotNode*>(work.parent)->children.push_back(sn);

		std::vector<const libbackup::BackupFileNode*> children;
		for (const auto& [name, node] : work.node->children)
			children.push_back(node.get());
		std::ranges::sort(children, {}, [](const libbackup::BackupFileNode* n) -> const std::string& {
			return n->filename;
		});

		for (const libbackup::BackupFileNode* child : children)
			queue.push_back({sn, child->filename, child});
	}
}

const BackupItemModel::SnapshotNode* BackupItemModel::nodeOf(const QModelIndex& index) const
{
	return static_cast<const SnapshotNode*>(index.internalPointer());
}

int BackupItemModel::rowOf(const SnapshotNode* sn) const
{
	return int(std::ranges::find(sn->parent->children, sn) - sn->parent->children.begin());
}

QModelIndex BackupItemModel::indexOf(const SnapshotNode* sn, int column) const
{
	return createIndex(rowOf(sn), column, const_cast<SnapshotNode*>(sn));
}

bool BackupItemModel::isDirectoryNode(const SnapshotNode* sn) const
{
	return sn->node->isDirectory || !sn->node->children.empty();
}

std::string BackupItemModel::domainKey(const SnapshotNode* sn) const
{
	while (sn->parent && sn->parent != m_root)
		sn = sn->parent;

	return sn->name;
}

std::string BackupItemModel::fullPath(const SnapshotNode* sn) const
{
	std::vector<const SnapshotNode*> chain;

	for (const SnapshotNode* cur = sn; cur && cur != m_root; cur = cur->parent)
		chain.push_back(cur);

	std::string path;
	for (auto it = chain.rbegin(); it != chain.rend(); ++it)
	{
		if (!path.empty())
			path += '/';
		path += (*it)->name;
	}
	return path;
}

bool BackupItemModel::matchValue(const QVariant& dataValue, const QVariant& value, Qt::MatchFlags flags)
{
	if (!dataValue.isValid())
		return false;
	if (dataValue == value && flags.testFlag(Qt::MatchExactly))
		return true;

	const QString haystack = dataValue.toString();
	const QString needle = value.toString();
	if (needle.isEmpty())
		return false;

	const Qt::CaseSensitivity cs = flags.testFlag(Qt::MatchCaseSensitive) ? Qt::CaseSensitive : Qt::CaseInsensitive;

	if (flags.testFlag(Qt::MatchWildcard) || flags.testFlag(Qt::MatchRegularExpression))
	{
		const QString pattern =
			flags.testFlag(Qt::MatchWildcard) ? QRegularExpression::wildcardToRegularExpression(needle) : needle;
		QRegularExpression re(pattern);
		if (cs == Qt::CaseInsensitive)
			re.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
		return re.match(haystack).hasMatch();
	}

	if (flags.testFlag(Qt::MatchExactly))
		return haystack.compare(needle, cs) == 0;
	if (flags.testFlag(Qt::MatchStartsWith))
		return haystack.startsWith(needle, cs);
	return haystack.contains(needle, cs);
}

void BackupItemModel::startTypeScan(const libbackup::Backup& backup)
{
	m_displayedTypes = std::make_unique<TypeMap>();
	m_pendingTypes = std::make_unique<TypeMap>();

	for (const SnapshotNode& sn : m_nodes)
	{
		const libbackup::ManifestDatabase::FilesRow* row = sn.node ? sn.node->associatedRow : nullptr;
		if (!row || sn.node->isDirectory)
			continue;

		const std::filesystem::path path = backup.FilePathForFileID(row->fileID);
		if (path.empty())
			continue;

		m_scanItems.push_back({row->fileID, path.string(), &sn});
	}

	if (m_scanItems.empty())
		return;

	m_refreshTimer = new QTimer(this);
	m_refreshTimer->setInterval(100);
	connect(m_refreshTimer, &QTimer::timeout, this, &BackupItemModel::refreshTypeMap);
	m_refreshTimer->start();

	const size_t workerCount = std::max<size_t>(1, std::thread::hardware_concurrency() * 30 / 100);
	for (size_t i = 0; i < workerCount; i++)
		m_workers.emplace_back(&BackupItemModel::typeScanWorker, this);
}

void BackupItemModel::typeScanWorker()
{
	FileTyping::Magic magic;

	while (!m_stopWorkers.load(std::memory_order_relaxed))
	{
		const size_t i = m_nextScanItem.fetch_add(1, std::memory_order_relaxed);
		if (i >= m_scanItems.size())
			break;
		const ScanItem& item = m_scanItems[i];

		std::string mime;
		if (::access(item.path.c_str(), R_OK) == 0)
		{
			try
			{
				magic.open(item.path);
				mime = magic.mime();
			}
			catch (const std::exception& e)
			{
				mime = e.what();
			}
		}

		{
			std::lock_guard lock(m_typesMutex);
			if (m_pendingTypes->try_emplace(item.fileID, mime).second)
				m_pendingNotify.push_back(item.node);
		}

		m_completedScanItems.fetch_add(1, std::memory_order_relaxed);
	}
}

void BackupItemModel::refreshTypeMap()
{
	std::vector<const SnapshotNode*> notify;
	size_t done;
	{
		std::lock_guard lock(m_typesMutex);
		m_displayedTypes->merge(*m_pendingTypes);
		m_pendingTypes->clear();
		notify.swap(m_pendingNotify);
		done = m_completedScanItems.load(std::memory_order_relaxed);
	}

	for (const SnapshotNode* sn : notify)
	{
		const QModelIndex idx = indexOf(sn, ColType);
		emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
	}

	emit typeScanProgress(static_cast<int>(done), static_cast<int>(m_scanItems.size()));

	if (done >= m_scanItems.size())
	{
		m_refreshTimer->stop();
		m_stopWorkers.store(true);
		for (std::thread& worker : m_workers)
			worker.join();
		m_workers.clear();
		emit typeScanFinished();
	}
}

BackupSortFilterProxyModel::BackupSortFilterProxyModel(QObject* parent) : QSortFilterProxyModel(parent) {}

bool BackupSortFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
	if (filterRegularExpression().pattern().isEmpty())
		return true;

	const QModelIndex idx = sourceModel()->index(sourceRow, filterKeyColumn(), sourceParent);
	if (matchesFilter(idx))
		return true;

	for (QModelIndex p = sourceParent; p.isValid(); p = p.parent())
	{
		if (matchesFilter(sourceModel()->index(p.row(), filterKeyColumn(), p.parent())))
			return true;
	}

	return anyDescendantMatches(idx);
}

bool BackupSortFilterProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
	const bool leftIsDirectory = left.data(BackupItemModel::IsDirectoryRole).toBool();
	const bool rightIsDirectory = right.data(BackupItemModel::IsDirectoryRole).toBool();

	if (leftIsDirectory != rightIsDirectory)
		return leftIsDirectory;

	return QSortFilterProxyModel::lessThan(left, right);
}

bool BackupSortFilterProxyModel::matchesFilter(const QModelIndex& sourceIndex) const
{
	if (!sourceIndex.isValid())
		return false;

	const QVariant value = sourceIndex.data(filterRole());
	if (!value.isValid())
		return false;

	return value.toString().contains(filterRegularExpression());
}

bool BackupSortFilterProxyModel::anyDescendantMatches(const QModelIndex& sourceIndex) const
{
	std::vector stack {sourceIndex};

	while (!stack.empty())
	{
		const QModelIndex idx = stack.back();
		stack.pop_back();

		for (int row = 0; row < sourceModel()->rowCount(idx); row++)
		{
			const QModelIndex child = sourceModel()->index(row, filterKeyColumn(), idx);
			if (matchesFilter(child))
				return true;
			stack.push_back(child);
		}
	}
	return false;
}
