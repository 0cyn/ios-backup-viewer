#ifndef IOSBACKUPS_PLISTTREE_H
#define IOSBACKUPS_PLISTTREE_H

#include <deque>
#include <string>
#include <vector>

#include <QAbstractItemModel>
#include <QByteArray>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QString>
#include <QVariant>

#include "Plist.hpp"

class PlistTreeModel : public QAbstractItemModel
{
	Q_OBJECT

public:
	enum Column
	{
		ColName = 0,
		ColValue,
		NumColumns
	};

	enum Role
	{
		ContainerRole = Qt::UserRole + 1,
		UidRole,
		DataViewRole
	};

	explicit PlistTreeModel(QObject* parent = nullptr);

	bool load(const QString& filename);

	QModelIndex jumpTarget(const QModelIndex& sourceIndex) const;

	QByteArray dataBytes(const QModelIndex& sourceIndex) const;

	QModelIndex index(int row, int column, const QModelIndex& parent) const override;

	QModelIndex parent(const QModelIndex& child) const override;

	int rowCount(const QModelIndex& parent) const override;

	int columnCount(const QModelIndex& parent) const override;

	QVariant data(const QModelIndex& index, int role) const override;

	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
	struct Node
	{
		std::string name;
		std::string value;
		std::vector<char> data;
		bool container = false;
		long long uid = -1;
		const Node* parent = nullptr;
		std::vector<const Node*> children;
	};

	void appendChildren(const Node* parent, const boost::any& value);

	const Node* appendAny(const Node* parent, const std::string& name, const boost::any& value);

	static std::string displayValue(const boost::any& value);

	const Node* nodeOf(const QModelIndex& index) const;

	int rowOf(const Node* n) const;

	QModelIndex indexOf(const Node* n, int column) const;

	std::deque<Node> m_nodes;
	const Node* m_root = nullptr;
	const Node* m_objectsNode = nullptr;
};

class PlistSortFilterProxyModel : public QSortFilterProxyModel
{
	Q_OBJECT

public:
	explicit PlistSortFilterProxyModel(QObject* parent = nullptr);

protected:
	bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

	bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
	bool matchesFilter(const QModelIndex& sourceIndex) const;

	bool anyDescendantMatches(const QModelIndex& sourceIndex) const;
};

class PlistJumpDelegate : public QStyledItemDelegate
{
	Q_OBJECT

public:
	explicit PlistJumpDelegate(QObject* parent = nullptr);

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

	bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
		const QModelIndex& index) override;

signals:
	void jumpRequested(const QModelIndex& index);

private:
	QString linkSuffix(const QModelIndex& index) const;

	QRect linkRect(const QStyleOptionViewItem& option, const QModelIndex& index) const;
};

#endif
