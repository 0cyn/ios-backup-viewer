#include "include/Backup.h"
#include <filesystem>
#include "sqlite3.h"

#include <cassert>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#define dont abort

namespace fs = std::filesystem;

using namespace libbackup;

namespace {
	struct SqliteDbDeleter
	{
		void operator()(sqlite3* db) const noexcept
		{
			if (db)
				sqlite3_close(db);
		}
	};

	struct SqliteStmtDeleter
	{
		void operator()(sqlite3_stmt* stmt) const noexcept
		{
			if (stmt)
				sqlite3_finalize(stmt);
		}
	};

	using SqliteDbPtr = std::unique_ptr<sqlite3, SqliteDbDeleter>;
	using SqliteStmtPtr = std::unique_ptr<sqlite3_stmt, SqliteStmtDeleter>;
}  // namespace

Backup::Backup(const std::string& _pathString) : m_path(_pathString)
{
	Plist::readPlist((m_path / "Info.plist").c_str(), m_infoPlist);
	Plist::readPlist((m_path / "Manifest.plist").c_str(), m_manifestPlist);
	Plist::readPlist((m_path / "Status.plist").c_str(), m_statusPlist);

	LoadDatabase();
}


std::filesystem::path Backup::FilePathForFileID(const std::string& fileID) const
{
	if (!m_manifestDB.filesTable.contains(fileID))
		return {};

	auto row = m_manifestDB.filesTable.at(fileID).get();

	if ((row->flags & 2) != 0) // DIR
		return {};

	fs::path path = m_path / fileID.substr(0, 2) / fileID;
	assert(!path.empty());
	assert(std::string(path.c_str()).substr(0, 1) == "/");
	return path;
}


void Backup::LoadDatabase()
{
	auto dbPath = m_path / "Manifest.db";
	std::error_code ec;
	auto status = fs::status(dbPath, ec);
	if (ec)
	{
		throw BackupException("Couldn't access Manifest.db: " + ec.message());
	}
	if (!exists(status) || status.type() != fs::file_type::regular)
	{
		throw BackupException("Couldn't find Manifest.db");
	}

	sqlite3* rawDb = nullptr;
	if (sqlite3_open(dbPath.c_str(), &rawDb) != SQLITE_OK)
	{
		std::string message = "Couldn't open Manifest.db";
		if (rawDb)
			message += ": " + std::string {sqlite3_errmsg(rawDb)};
		else
			message += ": no database handle was created";

		if (rawDb)
			sqlite3_close(rawDb);

		throw BackupException(message);
	}

	SqliteDbPtr dbOwner {rawDb};
	sqlite3* db = dbOwner.get();

	sqlite3_stmt* rawStmt = nullptr;
	if (sqlite3_prepare_v2(db, "select * from Files", -1, &rawStmt, nullptr) != SQLITE_OK)
	{
		std::string message = "Couldn't prepare \"select * from Files\" on Manifest.db";
		if (db)
			message += ": " + std::string {sqlite3_errmsg(db)};

		if (rawStmt)
			sqlite3_finalize(rawStmt);

		throw BackupException(message);
	}

	SqliteStmtPtr stmtOwner {rawStmt};
	sqlite3_stmt* stmt = stmtOwner.get();

	using Value = std::variant<std::monostate, int64_t, double, std::string, std::vector<uint8_t>>;

	auto col = [](sqlite3_stmt* stmt, int i) -> Value {
		int n = sqlite3_column_bytes(stmt, i);
		switch (sqlite3_column_type(stmt, i))
		{
		case SQLITE_NULL:
			return Value {};
		case SQLITE_INTEGER:
			return Value {(sqlite3_column_int64(stmt, i))};
		case SQLITE_FLOAT:
			return Value {sqlite3_column_double(stmt, i)};
		case SQLITE_TEXT:
		{
			if (n <= 0)
				return Value {std::string {}};

			const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
			return Value {std::string {text, static_cast<size_t>(n)}};
		}
		case SQLITE_BLOB:
		{
			if (n <= 0)
				return Value {std::vector<uint8_t> {}};

			const void* data = sqlite3_column_blob(stmt, i);
			if (!data)
				return Value {std::vector<uint8_t> {}};

			const auto* bytes = static_cast<const uint8_t*>(data);
			return Value {std::vector(bytes, bytes + static_cast<size_t>(n))};
		}
		default:
			return Value {};
		}
	};

	// take an iterator pointing at the domain node and its placeholder root value
	// we're then going to traverse nodes and ensure all of the paths up to our file are created
	// while the backup process may define the paths later, we just create placeholders for directories
	// if we come across a row describing the folder later its info will be added by the parsing loop

	while (stmt && sqlite3_step(stmt) == SQLITE_ROW)
	{
		// for the sake of solid code we may need to handle
		// out of order path declarations
		// so as a result of this, nodes may be created with a name before
		//	we have the row information

		auto fileID = std::get<std::string>(col(stmt, 0));
		auto [fileRowIterator, rowInserted] =
			m_manifestDB.filesTable.insert_or_assign(fileID, std::make_unique<ManifestDatabase::FilesRow>());
		if (!rowInserted)
			throw BackupException("key field was a duplicate, seriously malformed backup database");
		fileRowIterator->second->fileID = fileID;
		fileRowIterator->second->domain = std::get<std::string>(col(stmt, 1));
		fileRowIterator->second->relativePath = std::get<std::string>(col(stmt, 2));
		fileRowIterator->second->flags = std::get<int64_t>(col(stmt, 3));

		auto [domainKVIterator, domainKVInserted] =
			m_domains.try_emplace(fileRowIterator->second->domain, std::make_unique<BackupFileNode>());

		// i think we're safe from running out of stack on recursion just based on what i remember
		//	of xnu vfs limitations, but i'd rather just not do recursion anywhere here in case.

		// Create parents for the given node, these may be placeholder empty ones
		//	in the event the rows are out of order

		// create per node
		BackupFileNode* parentNode = domainKVIterator->second.get();
		std::vector<std::string> pathComponents;
		fs::path path(fileRowIterator->second->relativePath);
		for (const auto& component : path)
			pathComponents.emplace_back(component.c_str());

		if (pathComponents.empty())
			// basically a domain declaration, we're done
			continue;

		if (!pathComponents.empty())
		{
			assert(!pathComponents.empty());  // ???

			while (pathComponents.size() > 1)
			{
				auto fsNode = pathComponents.front();

				auto [childNode, inserted] =
					parentNode->children.try_emplace(fsNode, std::make_unique<BackupFileNode>());
				if (inserted)
				{
					childNode->second->filename = fsNode;
				}
				parentNode = childNode->second.get();

				pathComponents.erase(pathComponents.begin());
			}
		}
		assert(parentNode);
		assert(pathComponents.size() == 1);

		auto targetBasename = pathComponents.at(0);

		auto [it, inserted] = parentNode->children.try_emplace(targetBasename, std::make_unique<BackupFileNode>());
		if (!inserted)
			assert(it->second->filename == targetBasename);

		switch (fileRowIterator->second->flags)
		{
		case 1:  // File
		case 4:  // ??? FIXME what is this? It looks like databases, why does it have its own flag
				 //			can probably intuit more looking at the 'file' column in manifest db
			it->second->filename = targetBasename;
			it->second->parent = parentNode;
			it->second->isDirectory = false;
			it->second->associatedRow = fileRowIterator->second.get();
			break;
		case 2:  // dir
			it->second->filename = targetBasename;
			it->second->parent = parentNode;
			it->second->isDirectory = true;
			it->second->associatedRow = fileRowIterator->second.get();
			break;
		default:
			[[maybe_unused]] const char* fail = "Unknown file row flag? file an issue";
			dont();  // FIXME
		}
	}
}
