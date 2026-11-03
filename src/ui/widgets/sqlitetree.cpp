#include "sqlitetree.h"

#include <QSqlRecord>
#include <QVariant>

#include <qtablesqlite.h>

// TODO: we have the ability to show bytes in the plisttree, port that here
// TODO: the vendored library is ok, but I feel like we could do a better
//			impl in-house

constexpr quintptr kIdOffset = 1;

SqliteTreeModel::SqliteTreeModel(QObject* parent) : QAbstractItemModel(parent), m_db(new QTableSQLite) {}

SqliteTreeModel::~SqliteTreeModel()
{
	delete m_db;
}

bool SqliteTreeModel::load(const QString& filename)
{
	m_db->setFilename(filename);
	m_db->setTable(QStringLiteral("sqlite_master"));

	const QSqlRecordList entries = m_db->rowsData(QStringLiteral("type IN ('table','view')"), false);

	beginResetModel();
	m_nodes.clear();
	m_roots.clear();
	for (const QSqlRecord& entry : entries)
	{
		m_roots.append(m_nodes.size());

		Node node;
		node.kind = Node::Kind::Table;
		node.name = entry.value(QStringLiteral("name")).toString();
		m_nodes.append(node);
	}
	endResetModel();

	return !m_nodes.isEmpty();
}

int SqliteTreeModel::nodeOf(const QModelIndex& index) const
{
	return index.isValid() ? static_cast<int>(index.internalId() - kIdOffset) : -1;
}

QString SqliteTreeModel::displayValue(const QVariant& value)
{
	if (!value.isValid() || value.isNull())
		return QStringLiteral("NULL");
	if (value.typeId() == QMetaType::QByteArray)
		return QStringLiteral("<%1 bytes>").arg(value.toByteArray().size());
	return value.toString();
}

QModelIndex SqliteTreeModel::index(int row, int column, const QModelIndex& parent) const
{
	if (!hasIndex(row, column, parent))
		return {};

	const QVector<int>& kids = parent.isValid() ? m_nodes[nodeOf(parent)].children : m_roots;
	return createIndex(row, column, static_cast<quintptr>(kids[row]) + kIdOffset);
}

QModelIndex SqliteTreeModel::parent(const QModelIndex& child) const
{
	if (!child.isValid())
		return {};

	const int id = m_nodes[nodeOf(child)].parent;
	if (id < 0)
		return {};

	const int pid = m_nodes[id].parent;
	const int row = pid < 0 ? m_roots.indexOf(id) : m_nodes[pid].children.indexOf(id);
	if (row < 0)
		return {};
	return createIndex(row, 0, static_cast<quintptr>(id) + kIdOffset);
}

int SqliteTreeModel::rowCount(const QModelIndex& parent) const
{
	if (parent.column() > 0)
		return 0;
	if (!parent.isValid())
		return m_roots.size();
	const Node& node = m_nodes[nodeOf(parent)];
	return node.kind == Node::Kind::Field ? 0 : node.children.size();
}

int SqliteTreeModel::columnCount(const QModelIndex&) const
{
	return 2;
}

QVariant SqliteTreeModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || role != Qt::DisplayRole)
		return {};

	const Node& node = m_nodes[nodeOf(index)];
	if (index.column() == 0)
		return node.name;
	return node.value;
}

QVariant SqliteTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return {};

	switch (section)
	{
	case 0:
		return QStringLiteral("Name");
	case 1:
		return QStringLiteral("Value");
	}
	return {};
}

bool SqliteTreeModel::canFetchMore(const QModelIndex& parent) const
{
	if (!parent.isValid())
		return false;

	const Node& node = m_nodes[nodeOf(parent)];
	return node.kind == Node::Kind::Table && !node.loaded;
}

void SqliteTreeModel::fetchMore(const QModelIndex& parent)
{
	if (!canFetchMore(parent))
		return;

	fetchTableRows(nodeOf(parent), parent);
}

bool SqliteTreeModel::hasChildren(const QModelIndex& parent) const
{
	return canFetchMore(parent) || QAbstractItemModel::hasChildren(parent);
}

void SqliteTreeModel::fetchTableRows(int tableId, const QModelIndex& parentIndex)
{
	m_db->setTable(m_nodes[tableId].name);
	const QSqlRecordList rows = m_db->rowsData(QString(), false);

	m_nodes[tableId].loaded = true;
	if (rows.isEmpty())
		return;

	beginInsertRows(parentIndex, 0, rows.size() - 1);
	for (int r = 0; r < rows.size(); r++)
	{
		const QSqlRecord& record = rows[r];

		const int rowId = m_nodes.size();
		Node rowNode;
		rowNode.kind = Node::Kind::Row;
		rowNode.name = QStringLiteral("row %1").arg(r + 1);
		rowNode.parent = tableId;
		m_nodes.append(rowNode);

		for (int i = 0; i < record.count(); i++)
		{
			const int fieldId = m_nodes.size();
			Node field;
			field.kind = Node::Kind::Field;
			field.name = record.fieldName(i);
			field.value = displayValue(record.value(i));
			field.parent = rowId;
			m_nodes.append(field);
			m_nodes[rowId].children.append(fieldId);
		}

		m_nodes[tableId].children.append(rowId);
	}

	const QModelIndex valueIndex = parentIndex.siblingAtColumn(1);
	m_nodes[tableId].value = QString::number(rows.size());
	endInsertRows();
	emit dataChanged(valueIndex, valueIndex, {Qt::DisplayRole});
}
