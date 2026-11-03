#ifndef IOSBACKUPS_SQLITETREE_H
#define IOSBACKUPS_SQLITETREE_H

#include <QAbstractItemModel>
#include <QString>
#include <QVector>

class QTableSQLite;

class SqliteTreeModel : public QAbstractItemModel
{
	Q_OBJECT

public:
	explicit SqliteTreeModel(QObject* parent = nullptr);

	~SqliteTreeModel() override;

	bool load(const QString& filename);

	QModelIndex index(int row, int column, const QModelIndex& parent) const override;

	QModelIndex parent(const QModelIndex& child) const override;

	int rowCount(const QModelIndex& parent) const override;

	int columnCount(const QModelIndex& parent) const override;

	QVariant data(const QModelIndex& index, int role) const override;

	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

	bool canFetchMore(const QModelIndex& parent) const override;

	void fetchMore(const QModelIndex& parent) override;

	bool hasChildren(const QModelIndex& parent) const override;

private:
	struct Node
	{
		enum class Kind
		{
			Table,
			Row,
			Field
		};

		Kind kind;
		QString name;
		QString value;
		int parent = -1;
		QVector<int> children;
		bool loaded = false;
	};

	static QString displayValue(const QVariant& value);

	int nodeOf(const QModelIndex& index) const;

	void fetchTableRows(int tableId, const QModelIndex& parentIndex);

	QTableSQLite* m_db;
	QVector<int> m_roots;
	QVector<Node> m_nodes;
};

#endif  // IOSBACKUPS_SQLITETREE_H
