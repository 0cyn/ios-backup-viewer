
#ifndef IOSBACKUPS_BACKUP_H
#define IOSBACKUPS_BACKUP_H

#include <string>
#include <ctime>

#include <Plist.hpp>

// typedef std::map<std::string, boost::any> Plist::dictionary_type

namespace libbackup {

	class BackupException : std::exception
	{
		std::string m_message;

	public:
		BackupException(const std::string& message) : m_message(message) {}
		const char* what() const noexcept override { return m_message.c_str(); }
	};

	struct ManifestDatabase
	{
		struct FilesRow
		{
			std::string fileID;
			std::string domain;
			std::string relativePath;
			int64_t flags;
		};

		std::unordered_map<std::string, std::unique_ptr<FilesRow>> filesTable;
	};

	// hi :)
	struct BackupFileNode
	{
		std::string filename;
		bool isDirectory;
		ManifestDatabase::FilesRow* associatedRow;  // owned by ManifestDatabase
		BackupFileNode* parent; // owned by parent->parent; nullptr on root;
		std::unordered_map<std::string, std::unique_ptr<BackupFileNode>> children;
	};

	class Backup
	{
	public:
		struct Status
		{
			// enum BackupState { new, ?? } backupState;
			std::chrono::time_point<std::chrono::system_clock> date;
			bool isFullBackup;
			// enum SnapshotState { finished, ?? } snapshotState;
			std::string uuid;
			std::string version;
		};

	private:
		std::filesystem::path m_path;

		Plist::dictionary_type m_infoPlist;
		Plist::dictionary_type m_manifestPlist;
		Plist::dictionary_type m_statusPlist;
		ManifestDatabase m_manifestDB;

		std::unordered_map<std::string, std::unique_ptr<BackupFileNode>> m_domains;

		void LoadDatabase();

	public:
		Backup(const std::string& path);

		[[nodiscard]] const ManifestDatabase& manifestDatabase() const { return m_manifestDB; }
		[[nodiscard]] const std::unordered_map<std::string, std::unique_ptr<BackupFileNode>>& domains() const { return m_domains; }

		std::filesystem::path FilePathForFileID(const std::string& fileID) const ;

		class Debug
		{
		public:
			static std::string GetInfoPlistString(const Backup& backup)
			{
				std::vector<char> p;
				Plist::writePlistXML(p, backup.m_infoPlist);
				return {p.begin(), p.end()};
			}
			static std::string GetManifestPlistString(const Backup& backup)
			{
				std::vector<char> p;
				Plist::writePlistXML(p, backup.m_manifestPlist);
				return {p.begin(), p.end()};
			}
			static std::string GetStatusPlistString(const Backup& backup)
			{
				std::vector<char> p;
				Plist::writePlistXML(p, backup.m_statusPlist);
				return {p.begin(), p.end()};
			}
		};
	};
}  // namespace libbackup


#endif  // IOSBACKUPS_BACKUP_H
