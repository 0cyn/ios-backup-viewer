// UGH
// this just really needs to be a QPainter; I don't like how we have to handle rows.
// just easier to copy paste huge amounts of code this way, so much boilerplate.
//
// also, bplists with UID links are closer to graphs, just a lot needs thought about w/ display
// especially with searches, it does feel like at least an optional graph display
//		would make a lot more sense. TODO


#include "plisttree.h"

#include <algorithm>
#include <deque>
#include <exception>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <typeinfo>
#include <vector>

#include <QDebug>
#include <QEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QApplication>
#include <QRegularExpression>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QString>
#include <QVariant>

#include "Plist.hpp"

namespace {
	std::string inlineBytes(const std::vector<char>& data)
	{
		std::ostringstream out;
		out << std::hex << std::uppercase << std::setfill('0');
		for (size_t i = 0; i < data.size(); i++)
		{
			if (i > 0)
				out << ' ';
			out << std::setw(2) << int(static_cast<unsigned char>(data[i]));
		}
		return out.str();
	}
}  // namespace

PlistTreeModel::PlistTreeModel(QObject* parent) : QAbstractItemModel(parent) {}

bool PlistTreeModel::load(const QString& filename)
{
	boost::any root;

	std::ifstream stream(filename.toStdString(), std::ios::binary);
	if (!stream)
	{
		qWarning() << "plisttree: could not open" << filename;
		return false;
	}

	try
	{
		Plist::readPlist(stream, root);
	}
	catch (const std::exception& e)
	{
		qWarning() << "plisttree:" << filename << "-" << e.what();
		return false;
	}

	if (root.empty())
	{
		qWarning() << "plisttree:" << filename << "- empty plist document";
		return false;
	}

	beginResetModel();
	m_nodes.clear();
	m_nodes.emplace_back();
	m_root = &m_nodes.front();
	m_objectsNode = nullptr;

	if (root.type() == typeid(Plist::dictionary_type) || root.type() == typeid(Plist::array_type))
		appendChildren(m_root, root);
	else
		appendAny(m_root, std::string(), root);

	for (const Node* child : m_root->children)
	{
		if (child->container && child->name == "$objects")
		{
			m_objectsNode = child;
			break;
		}
	}
	endResetModel();

	return true;
}

QModelIndex PlistTreeModel::jumpTarget(const QModelIndex& sourceIndex) const
{
	if (!m_objectsNode || sourceIndex.column() != ColValue)
		return {};

	const Node* n = nodeOf(sourceIndex);
	if (!n || n->uid < 0 || std::size_t(n->uid) >= m_objectsNode->children.size())
		return {};

	return indexOf(m_objectsNode->children[std::size_t(n->uid)], ColName);
}

QByteArray PlistTreeModel::dataBytes(const QModelIndex& sourceIndex) const
{
	if (sourceIndex.column() != ColValue)
		return {};

	const Node* n = nodeOf(sourceIndex);
	if (!n || n->data.empty())
		return {};

	return QByteArray(n->data.data(), int(n->data.size()));
}

void PlistTreeModel::appendChildren(const Node* parent, const boost::any& value)
{
	if (value.type() == typeid(Plist::dictionary_type))
	{
		for (const auto& [key, child] : boost::any_cast<const Plist::dictionary_type&>(value))
			appendAny(parent, key, child);
		return;
	}

	if (value.type() == typeid(Plist::array_type))
	{
		const auto& array = boost::any_cast<const Plist::array_type&>(value);
		for (size_t i = 0; i < array.size(); i++)
			appendAny(parent, std::to_string(i), array[i]);
	}
}

const PlistTreeModel::Node* PlistTreeModel::appendAny(
	const Node* parent, const std::string& name, const boost::any& value)
{
	m_nodes.emplace_back();
	Node* n = &m_nodes.back();
	n->name = name;
	n->parent = parent;
	const_cast<Node*>(parent)->children.push_back(n);

	if (value.type() == typeid(Plist::uid_type))
		n->uid = static_cast<long long>(boost::any_cast<const Plist::uid_type&>(value).data);

	if (value.type() == typeid(Plist::data_type))
	{
		const auto& data = boost::any_cast<const Plist::data_type&>(value);
		if (data.size() <= 8)
			n->value = inlineBytes(data);
		else
		{
			n->data = data;
			n->value = " <" + std::to_string(data.size()) + " bytes>";
		}
		return n;
	}

	if (value.type() == typeid(Plist::dictionary_type) || value.type() == typeid(Plist::array_type))
	{
		n->container = true;
		appendChildren(n, value);
		n->value = std::to_string(n->children.size());
		return n;
	}

	n->value = displayValue(value);
	return n;
}

std::string PlistTreeModel::displayValue(const boost::any& value)
{
	if (value.empty())
		return "None";

	if (value.type() == typeid(Plist::uid_type))
		return " UID(" + std::to_string(boost::any_cast<const Plist::uid_type&>(value).data) + ")";

	if (value.type() == typeid(Plist::string_type))
		return boost::any_cast<const Plist::string_type&>(value);

	if (value.type() == typeid(Plist::integer_type))
		return std::to_string(boost::any_cast<const Plist::integer_type&>(value));

	if (value.type() == typeid(Plist::real_type))
	{
		std::ostringstream out;
		out << boost::any_cast<const Plist::real_type&>(value);
		return out.str();
	}

	if (value.type() == typeid(Plist::boolean_type))
		return boost::any_cast<const Plist::boolean_type&>(value) ? "true" : "false";

	if (value.type() == typeid(Plist::date_type))
		return boost::any_cast<const Plist::date_type&>(value).timeAsXMLConvention();

	return {};
}

QModelIndex PlistTreeModel::index(int row, int column, const QModelIndex& parent) const
{
	if (column < 0 || column >= NumColumns)
		return {};

	const Node* parentNode = parent.isValid() ? nodeOf(parent) : m_root;
	if (!parentNode || row < 0 || std::size_t(row) >= parentNode->children.size())
		return {};

	return createIndex(row, column, const_cast<Node*>(parentNode->children[std::size_t(row)]));
}

QModelIndex PlistTreeModel::parent(const QModelIndex& child) const
{
	if (!child.isValid())
		return {};

	const Node* n = nodeOf(child);
	if (!n || !n->parent || n->parent == m_root)
		return {};

	return indexOf(n->parent, child.column());
}

int PlistTreeModel::rowCount(const QModelIndex& parent) const
{
	const Node* n = parent.isValid() ? nodeOf(parent) : m_root;
	return n ? int(n->children.size()) : 0;
}

int PlistTreeModel::columnCount(const QModelIndex& parent) const
{
	Q_UNUSED(parent);
	return NumColumns;
}

QVariant PlistTreeModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid())
		return {};

	const Node* n = nodeOf(index);

	switch (role)
	{
	case Qt::DisplayRole:
	case Qt::EditRole:
		switch (index.column())
		{
		case ColName:
			return QString::fromStdString(n->name);
		case ColValue:
			if (n->uid >= 0 && m_objectsNode && std::size_t(n->uid) < m_objectsNode->children.size())
			{
				const int width =
					7 + int(QString::number(static_cast<qlonglong>(m_objectsNode->children.size() - 1)).size());
				return QString::fromStdString(n->value).leftJustified(width, QLatin1Char(' '));
			}
			return QString::fromStdString(n->value);
		}
		return {};
	case ContainerRole:
		return QVariant(n->container);
	case UidRole:
		if (index.column() == ColValue && n->uid >= 0 && m_objectsNode
			&& std::size_t(n->uid) < m_objectsNode->children.size())
			return QVariant::fromValue<qlonglong>(n->uid);
		return {};
	case DataViewRole:
		if (index.column() == ColValue && !n->data.empty())
			return QVariant(qlonglong(n->data.size()));
		return {};
	}

	return {};
}

QVariant PlistTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return {};

	switch (section)
	{
	case ColName:
		return QStringLiteral("Name");
	case ColValue:
		return QStringLiteral("Value");
	}
	return {};
}

const PlistTreeModel::Node* PlistTreeModel::nodeOf(const QModelIndex& index) const
{
	return static_cast<const Node*>(index.internalPointer());
}

int PlistTreeModel::rowOf(const Node* n) const
{
	return int(std::ranges::find(n->parent->children, n) - n->parent->children.begin());
}

QModelIndex PlistTreeModel::indexOf(const Node* n, int column) const
{
	return createIndex(rowOf(n), column, const_cast<Node*>(n));
}

PlistSortFilterProxyModel::PlistSortFilterProxyModel(QObject* parent) : QSortFilterProxyModel(parent) {}

bool PlistSortFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
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

bool PlistSortFilterProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
	const bool leftIsContainer = left.data(PlistTreeModel::ContainerRole).toBool();
	const bool rightIsContainer = right.data(PlistTreeModel::ContainerRole).toBool();

	if (leftIsContainer != rightIsContainer)
		return leftIsContainer;

	return QSortFilterProxyModel::lessThan(left, right);
}

bool PlistSortFilterProxyModel::matchesFilter(const QModelIndex& sourceIndex) const
{
	if (!sourceIndex.isValid())
		return false;

	const int role = filterRole();

	if (sourceIndex.siblingAtColumn(PlistTreeModel::ColName).data(role).toString().contains(filterRegularExpression()))
		return true;

	return sourceIndex.siblingAtColumn(PlistTreeModel::ColValue).data(role).toString().contains(filterRegularExpression());
}

bool PlistSortFilterProxyModel::anyDescendantMatches(const QModelIndex& sourceIndex) const
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

namespace {
	QString jumpSuffix()
	{
		return QStringLiteral("(Jump to referenced entry)");
	}
}  // namespace

PlistJumpDelegate::PlistJumpDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QString PlistJumpDelegate::linkSuffix(const QModelIndex& index) const
{
	if (index.data(PlistTreeModel::UidRole).isValid())
		return jumpSuffix();

	return {};
}
void PlistJumpDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	const QString suffix = linkSuffix(index);
	if (suffix.isEmpty())
	{
		QStyledItemDelegate::paint(painter, option, index);
		return;
	}

	QStyleOptionViewItem opt(option);
	initStyleOption(&opt, index);
	opt.text.clear();

	QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
	style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

	QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
	painter->save();
	painter->setClipRect(textRect);
	QPen pen;
	pen.setBrush(opt.palette.text());
	painter->setPen(pen);

	const QString base = index.data(Qt::DisplayRole).toString();
	QFontMetrics fm(opt.font);
	const int baseline = textRect.top() + (textRect.height() + fm.ascent() - fm.descent()) / 2;
	painter->drawText(QPoint(textRect.left(), baseline), base);

	QFont linkFont = opt.font;
	linkFont.setUnderline(true);
	painter->setFont(linkFont);
	painter->drawText(QPoint(textRect.left() + fm.horizontalAdvance(base), baseline), suffix);
	painter->restore();
}

QRect PlistJumpDelegate::linkRect(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	const QString suffix = linkSuffix(index);
	if (suffix.isEmpty())
		return {};

	QStyleOptionViewItem opt(option);
	initStyleOption(&opt, index);

	QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
	QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);

	QFontMetrics fm(opt.font);
	const int x = textRect.left() + fm.horizontalAdvance(index.data(Qt::DisplayRole).toString());
	return QRect(x, textRect.top(), fm.horizontalAdvance(suffix), textRect.height());
}

bool PlistJumpDelegate::editorEvent(
	QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index)
{
	if (event->type() == QEvent::MouseButtonPress)
	{
		QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
		if (mouse->button() == Qt::LeftButton && linkRect(option, index).contains(mouse->position().toPoint()))
		{
			emit jumpRequested(index);
			return true;
		}
	}

	return QStyledItemDelegate::editorEvent(event, model, option, index);
}
